/*
 * Task 3 - File System Operations and Security
 * Scenario: Airport Staff Secure Document Vault (same airport theme as Tasks 1 & 2)
 *
 * The idea: airport staff need somewhere to create, read, write and delete
 * documents (staff records, security manifests, incident reports, etc.),
 * but not everyone should be able to touch every file. So this program
 * layers a small Unix-style permission system and a login step on top of
 * plain file operations, plus basic encryption for the genuinely sensitive
 * stuff and an audit log so every access attempt is traceable afterwards.
 *
 * What's implemented:
 *   1. Create / read / write / delete files
 *   2. Username + password login before anything else is allowed
 *   3. Owner / group / other read-write-execute permission bits, like
 *      classic Unix "rwxrwxrwx" file permissions
 *   4. XOR-cipher encrypt/decrypt for sensitive files (see the big comment
 *      near xor_crypt() for why this is NOT production-grade crypto and
 *      what a real system should use instead)
 *   5. An audit log (audit.log) that timestamps every login attempt and
 *      every file operation, successful or denied
 *
 * IMPORTANT - things that are simplified for a coursework demo and are
 * called out explicitly rather than pretended to be secure:
 *   - the "user database" below is a hardcoded array in the source code.
 *     A real system would never ship credentials in source; they'd live
 *     in a proper user store with per-user random salts.
 *   - passwords are hashed with a simple djb2 hash purely so a plaintext
 *     password is never compared or stored directly - djb2 is fast and
 *     NOT resistant to brute forcing, unlike bcrypt/scrypt/Argon2 which
 *     are deliberately slow. This is discussed more in the security
 *     analysis write-up.
 *   - the encryption is XOR with a fixed key, which is trivially
 *     reversible if the key is guessed or leaks. A real system would use
 *     something like AES-256-GCM through a proper crypto library.
 *
 * Compile : gcc task_3.c -o task_3
 * Run     : ./task_3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>

#define MAX_USERS       5
#define MAX_FILES       50
#define MAX_NAME_LEN    32
#define MAX_FILENAME    64
#define MAX_LINE        512
#define VAULT_DIR       "vault"
#define AUDIT_LOG_FILE  "audit.log"

/* Fixed demo XOR key - see the big comment above xor_crypt() for why this
   is only good enough for a classroom demo, not real security. */
#define XOR_KEY "AirportVaultDemoKey2026"

/* ---------------------------------------------------------------------
 * User accounts (hardcoded for this demo - see file header comment)
 * group "security" = elevated staff who can view the audit log, delete
 * any file, and manage permissions on anything; everyone else is a
 * normal account that only gets special access to files they own or
 * files whose group matches their own group.
 * -------------------------------------------------------------------*/
typedef struct {
    char username[MAX_NAME_LEN];
    unsigned long password_hash;
    char group[MAX_NAME_LEN];
} User;

User users[MAX_USERS] = {
    {"alice", 0, "security"},
    {"bob",   0, "staff"},
    {"carol", 0, "staff"},
    {"guest", 0, "guest"},
};
int user_count = 4;

/* ---------------------------------------------------------------------
 * File metadata table. The actual bytes live on disk under vault/, this
 * table just tracks who owns each file, what group it belongs to, and
 * the rwx permission bits for owner/group/other - same layout idea as
 * a Unix inode's permission bits, just kept in memory + a metadata file
 * instead of inside a real filesystem.
 * -------------------------------------------------------------------*/
typedef struct {
    char filename[MAX_FILENAME];
    char owner[MAX_NAME_LEN];
    char group[MAX_NAME_LEN];
    int perm_owner;   /* 0-7, bit 4=read 2=write 1=execute */
    int perm_group;
    int perm_other;
    int encrypted;    /* 1 if currently XOR-encrypted on disk */
} FileMeta;

FileMeta files[MAX_FILES];
int file_count = 0;

User *current_user = NULL;

/* ======================================================================
 * SMALL HELPERS
 * ===================================================================== */

/* djb2 - a quick, well-known non-cryptographic hash. Used here only so
   we're not literally storing/comparing plaintext passwords in this demo.
   It is NOT a substitute for a real password hash - see file header. */
unsigned long hash_password(const char *pw) {
    unsigned long hash = 5381;
    int c;
    while ((c = *pw++)) hash = ((hash << 5) + hash) + (unsigned long)c;
    return hash;
}

void strip_newline(char *s) {
    size_t len = strlen(s);
    if (len > 0 && s[len - 1] == '\n') s[len - 1] = '\0';
}

/* turns terminal echo off/on so a password isn't shown while typed -
   only does anything when stdin is an actual terminal, so the program
   still works fine when input is piped/redirected for testing */
void set_echo(int enable) {
    if (!isatty(STDIN_FILENO)) return;
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    if (enable) t.c_lflag |= ECHO;
    else        t.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

/* REQUIREMENT 5: AUDIT LOGGING
   appends one line to audit.log with a timestamp - called after every
   login attempt and every file operation, success or failure, so there's
   always a trail of who did what and when */
void log_action(const char *username, const char *action, const char *target, int success) {
    FILE *f = fopen(AUDIT_LOG_FILE, "a");
    if (!f) return; /* if logging itself fails there's not much else we can do here */

    time_t now = time(NULL);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(f, "[%s] user=%-8s action=%-10s target=%-20s result=%s\n",
            timebuf, username ? username : "-", action, target ? target : "-",
            success ? "SUCCESS" : "DENIED");
    fclose(f);
}

/* ======================================================================
 * REQUIREMENT 2: USER AUTHENTICATION MECHANISM
 * ===================================================================== */

User *find_user(const char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) return &users[i];
    }
    return NULL;
}

/* Prompts for username + password and returns the matching user, or NULL
   if the credentials didn't match anything. Every attempt gets logged,
   including failed ones - that's often the more important half of an
   audit trail since it's what shows someone trying to break in. */
User *login_prompt(void) {
    char username[MAX_NAME_LEN];
    char password[MAX_NAME_LEN];

    printf("\nUsername (or type 'quit' to exit): ");
    if (!fgets(username, sizeof(username), stdin)) return NULL;
    strip_newline(username);

    if (strcmp(username, "quit") == 0) return NULL;

    printf("Password: ");
    set_echo(0);
    if (!fgets(password, sizeof(password), stdin)) { set_echo(1); return NULL; }
    set_echo(1);
    printf("\n");
    strip_newline(password);

    User *u = find_user(username);
    unsigned long typed_hash = hash_password(password);

    if (u && u->password_hash == typed_hash) {
        log_action(username, "LOGIN", "-", 1);
        return u;
    }

    log_action(username, "LOGIN", "-", 0);
    printf("Login failed: invalid username or password.\n");
    return NULL;
}

/* ======================================================================
 * REQUIREMENT 3: FILE PERMISSION SYSTEM (owner / group / other, rwx)
 * ===================================================================== */

FileMeta *find_file(const char *filename) {
    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].filename, filename) == 0) return &files[i];
    }
    return NULL;
}

/* Works out which of the three permission classes (owner/group/other)
   applies to the current user for this file, then checks whether the
   requested bit ('r', 'w', or 'x') is set - exactly the same logic the
   real Unix permission model uses. */
int has_permission(User *u, FileMeta *f, char action) {
    int perm;
    if (strcmp(u->username, f->owner) == 0)      perm = f->perm_owner;
    else if (strcmp(u->group, f->group) == 0)    perm = f->perm_group;
    else                                          perm = f->perm_other;

    int bit = (action == 'r') ? 4 : (action == 'w') ? 2 : 1;
    return (perm & bit) != 0;
}

int is_owner_or_admin(User *u, FileMeta *f) {
    return (strcmp(u->username, f->owner) == 0) || (strcmp(u->group, "security") == 0);
}