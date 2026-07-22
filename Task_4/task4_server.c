/*
 * task4_server.c
 * Task 4 - Network Programming and IPC
 * Scenario: Airport Control Tower Server (same airport theme as Tasks 1-3)
 *
 * A TCP server that airport terminal clients connect to for live flight
 * info. Each connecting client gets its own thread, so many terminals can
 * be looking things up (or an admin pushing a status update) at the same
 * time, all handled concurrently by one server process.
 *
 * Requirements:
 *   1. TCP server (socket/bind/listen/accept)
 *   2. A simple line-based text protocol (see the big comment block below)
 *   3. One pthread per connected client -> handles multiple concurrent
 *      clients without one slow client blocking anyone else
 *   4. Basic security: login required before any flight command works,
 *      role-based restriction on UPDATE, and every incoming command is
 *      validated (unknown flights, wrong argument counts, bad characters
 *      all get rejected with an error response instead of crashing)
 *   5. Error handling on every socket call + clean connection teardown,
 *      including when a client disconnects mid-conversation
 *
 * PROTOCOL (plain text, one command/response per line, newline-terminated):
 *   Client -> Server:  AUTH <username> <password>
 *   Server -> Client:  OK AUTH <role>                  (success)
 *                      ERR AUTH invalid credentials     (failure)
 *
 *   (only after a successful AUTH does the server accept the below)
 *   Client -> Server:  LIST
 *   Server -> Client:  FLIGHT <flight_no> <status> <gate>   (one per flight)
 *                      END                                   (end marker)
 *
 *   Client -> Server:  STATUS <flight_no>
 *   Server -> Client:  OK STATUS <flight_no> <status>  /  ERR STATUS unknown flight
 *
 *   Client -> Server:  GATE <flight_no>
 *   Server -> Client:  OK GATE <flight_no> <gate>      /  ERR GATE unknown flight
 *
 *   Client -> Server:  UPDATE <flight_no> <status>     (admin role only)
 *   Server -> Client:  OK UPDATE <flight_no> <status>  /  ERR PERMISSION admin only
 *                                                       /  ERR UPDATE unknown flight
 *
 *   Client -> Server:  QUIT
 *   Server -> Client:  OK BYE   (then closes the socket)
 *
 *   Anything malformed gets:  ERR BADCMD <reason>
 *
 * Compile : gcc task4_server.c -o task4_server
 * Run     : ./task4_server
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define SERVER_PORT   9090
#define BUF_SIZE      512
#define MAX_FLIGHTS   5

/* ---------------------------------------------------------------------
 * Reads one newline-terminated line from a socket, one byte at a time.
 * TCP is a byte stream, not a message stream - a single send() on one end
 * is not guaranteed to arrive as a single recv() on the other, so we read
 * until we see '\n' rather than assuming "one send = one recv".
 * Returns bytes read (not counting the null terminator) on success,
 * 0 if the peer closed before sending anything, -1 on a socket error.
 * -------------------------------------------------------------------*/
ssize_t recv_line(int sock, char *buf, size_t maxlen) {
    size_t pos = 0;
    while (pos < maxlen - 1) {
        char c;
        ssize_t n = recv(sock, &c, 1, 0);
        if (n < 0) return -1;
        if (n == 0) { if (pos == 0) return 0; break; }
        if (c == '\n') break;
        if (c != '\r') buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

/* sends one line, appending the newline the protocol expects */
int send_line(int sock, const char *line) {
    char out[BUF_SIZE + 2];
    size_t len = strlen(line);
    if (len > BUF_SIZE) len = BUF_SIZE;
    memcpy(out, line, len);
    out[len] = '\n';
    size_t total = len + 1, sent = 0;
    while (sent < total) {
        ssize_t n = send(sock, out + sent, total - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * Demo user accounts. Hardcoded for this coursework demo only - a real
 * system would never ship credentials in source code.
 * -------------------------------------------------------------------*/
typedef struct {
    char username[32];
    char password[32];
    char role[16];   /* "admin" can UPDATE flights; everyone else is read-only */
} User;

User users[] = {
    {"alice", "alice123", "admin"},
    {"bob",   "bob123",   "staff"},
    {"guest", "guest123", "guest"},
};
int user_count = 3;

/* ---------------------------------------------------------------------
 * Shared flight data, protected by a mutex since multiple client threads
 * read it concurrently and UPDATE writes to it.
 * -------------------------------------------------------------------*/
typedef struct {
    char flight_no[8];
    char status[16];
    char gate[8];
} Flight;

Flight flights[MAX_FLIGHTS] = {
    {"AA101", "ON TIME",   "G12"},
    {"BA202", "DELAYED",   "G5"},
    {"QR303", "BOARDING",  "G22"},
    {"EK404", "ON TIME",   "G9"},
    {"SQ505", "CANCELLED", "G3"},
};
pthread_mutex_t flight_mutex = PTHREAD_MUTEX_INITIALIZER;

int active_connections = 0;
pthread_mutex_t conn_count_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_line(const char *fmt, ...) {
    time_t now = time(NULL);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    printf("[%s] ", timebuf);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

/* data validation: only letters/digits allowed in a flight number, and a
   sane max length - never trust anything a client sends without checking */
int is_valid_flight_no(const char *s) {
    size_t len = strlen(s);
    if (len == 0 || len > 7) return 0;
    for (size_t i = 0; i < len; i++) if (!isalnum((unsigned char)s[i])) return 0;
    return 1;
}

int is_valid_status(const char *s) {
    size_t len = strlen(s);
    if (len == 0 || len > 15) return 0;
    for (size_t i = 0; i < len; i++) if (!isprint((unsigned char)s[i])) return 0;
    return 1;
}

Flight *find_flight(const char *flight_no) {
    for (int i = 0; i < MAX_FLIGHTS; i++)
        if (strcasecmp(flights[i].flight_no, flight_no) == 0) return &flights[i];
    return NULL;
}

User *find_user(const char *username) {
    for (int i = 0; i < user_count; i++)
        if (strcmp(users[i].username, username) == 0) return &users[i];
    return NULL;
}

/* ---------------------------------------------------------------------
 * Per-client thread
 * -------------------------------------------------------------------*/
typedef struct {
    int fd;
    char ip[INET_ADDRSTRLEN];
    int port;
} ClientInfo;

void *handle_client(void *arg) {
    ClientInfo *info = (ClientInfo *)arg;
    int fd = info->fd;
    char peer[64];
    snprintf(peer, sizeof(peer), "%s:%d", info->ip, info->port);

    pthread_mutex_lock(&conn_count_mutex);
    active_connections++;
    log_line("Client connected from %s (active connections: %d)", peer, active_connections);
    pthread_mutex_unlock(&conn_count_mutex);

    char line[BUF_SIZE];
    User *auth_user = NULL;

    while (1) {
        ssize_t n = recv_line(fd, line, sizeof(line));
        if (n < 0)  { log_line("Read error from %s, closing connection", peer); break; }
        if (n == 0) { log_line("Client %s disconnected", peer); break; }

        char cmd[32] = {0}, arg1[64] = {0}, arg2[64] = {0};
        int parts = sscanf(line, "%31s %63s %63s", cmd, arg1, arg2);

        if (parts <= 0) { send_line(fd, "ERR BADCMD empty command"); continue; }

        if (strcmp(cmd, "AUTH") == 0) {
            if (parts != 3) { send_line(fd, "ERR BADCMD AUTH requires a username and password"); continue; }
            User *u = find_user(arg1);
            if (u && strcmp(u->password, arg2) == 0) {
                auth_user = u;
                char resp[BUF_SIZE];
                snprintf(resp, sizeof(resp), "OK AUTH %s", u->role);
                send_line(fd, resp);
                log_line("%s authenticated as '%s' (role=%s)", peer, u->username, u->role);
            } else {
                send_line(fd, "ERR AUTH invalid credentials");
                log_line("%s failed authentication (username='%s')", peer, arg1);
            }
            continue;
        }

        if (strcmp(cmd, "QUIT") == 0) { send_line(fd, "OK BYE"); break; }

        if (!auth_user) { send_line(fd, "ERR AUTH login required before using this command"); continue; }

        if (strcmp(cmd, "LIST") == 0) {
            pthread_mutex_lock(&flight_mutex);
            for (int i = 0; i < MAX_FLIGHTS; i++) {
                char resp[BUF_SIZE];
                snprintf(resp, sizeof(resp), "FLIGHT %s %s %s",
                         flights[i].flight_no, flights[i].status, flights[i].gate);
                send_line(fd, resp);
            }
            pthread_mutex_unlock(&flight_mutex);
            send_line(fd, "END");

        } else if (strcmp(cmd, "STATUS") == 0) {
            if (parts != 2 || !is_valid_flight_no(arg1)) {
                send_line(fd, "ERR BADCMD STATUS requires a valid flight number"); continue;
            }
            pthread_mutex_lock(&flight_mutex);
            Flight *f = find_flight(arg1);
            char resp[BUF_SIZE];
            if (f) snprintf(resp, sizeof(resp), "OK STATUS %s %s", f->flight_no, f->status);
            else   snprintf(resp, sizeof(resp), "ERR STATUS unknown flight");
            pthread_mutex_unlock(&flight_mutex);
            send_line(fd, resp);

        } else if (strcmp(cmd, "GATE") == 0) {
            if (parts != 2 || !is_valid_flight_no(arg1)) {
                send_line(fd, "ERR BADCMD GATE requires a valid flight number"); continue;
            }
            pthread_mutex_lock(&flight_mutex);
            Flight *f = find_flight(arg1);
            char resp[BUF_SIZE];
            if (f) snprintf(resp, sizeof(resp), "OK GATE %s %s", f->flight_no, f->gate);
            else   snprintf(resp, sizeof(resp), "ERR GATE unknown flight");
            pthread_mutex_unlock(&flight_mutex);
            send_line(fd, resp);

        } else if (strcmp(cmd, "UPDATE") == 0) {
            if (strcmp(auth_user->role, "admin") != 0) {
                send_line(fd, "ERR PERMISSION admin only");
                log_line("%s (%s) denied UPDATE - not an admin", peer, auth_user->username);
                continue;
            }
            if (parts != 3 || !is_valid_flight_no(arg1) || !is_valid_status(arg2)) {
                send_line(fd, "ERR BADCMD UPDATE requires a valid flight number and status"); continue;
            }
            pthread_mutex_lock(&flight_mutex);
            Flight *f = find_flight(arg1);
            char resp[BUF_SIZE];
            if (f) {
                strncpy(f->status, arg2, sizeof(f->status) - 1);
                f->status[sizeof(f->status) - 1] = '\0';
                snprintf(resp, sizeof(resp), "OK UPDATE %s %s", f->flight_no, f->status);
            } else {
                snprintf(resp, sizeof(resp), "ERR UPDATE unknown flight");
            }
            pthread_mutex_unlock(&flight_mutex);
            send_line(fd, resp);
            if (f) log_line("%s (%s) updated %s to '%s'", peer, auth_user->username, arg1, arg2);

        } else {
            send_line(fd, "ERR BADCMD unknown command");
        }
    }

    close(fd);
    pthread_mutex_lock(&conn_count_mutex);
    active_connections--;
    log_line("Connection with %s closed (active connections: %d)", peer, active_connections);
    pthread_mutex_unlock(&conn_count_mutex);

    free(info);
    return NULL;
}

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt"); close(server_fd); return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    if (listen(server_fd, 16) < 0) {
        perror("listen"); close(server_fd); return 1;
    }

    printf("############################################################\n");
    printf("#   Airport Control Tower Server - listening on port %d     #\n", SERVER_PORT);
    printf("############################################################\n");
    log_line("Server ready. Waiting for terminal connections...");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        ClientInfo *info = malloc(sizeof(ClientInfo));
        info->fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, info->ip, sizeof(info->ip));
        info->port = ntohs(client_addr.sin_port);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, info) != 0) {
            perror("pthread_create"); close(client_fd); free(info); continue;
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}