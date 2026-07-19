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

