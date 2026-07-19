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

