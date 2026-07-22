/*
 * task4_client.c
 * Task 4 - Network Programming and IPC
 * Scenario: Airport Terminal Client (connects to task4_server.c)
 *
 * Interactive client for the airport control tower server: logs in, then
 * lets you look up flights, check gates, and (if you're an admin) push
 * status updates - all over a real TCP socket using the line-based
 * protocol documented in task4_server.c.
 *
 * Compile : gcc task4_client.c -o task4_client
 * Run     : ./task4_client [server_ip] [server_port]
 *           (defaults to 127.0.0.1 and port 9090)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define SERVER_PORT 9090
#define BUF_SIZE    512

/* same line-reading approach as the server: TCP is a byte stream, not a
   message stream, so we read one byte at a time until '\n' rather than
   assuming a single recv() lines up with a single send() on the other end */
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

void strip_newline(char *s) {
    size_t len = strlen(s);
    if (len > 0 && s[len - 1] == '\n') s[len - 1] = '\0';
}

/* sends one line and prints the single-line reply - used for STATUS,
   GATE, UPDATE, AUTH */
void send_and_print(int sock, const char *line) {
    if (send_line(sock, line) < 0) { printf("Connection lost while sending. Exiting.\n"); exit(1); }
    char resp[BUF_SIZE];
    ssize_t n = recv_line(sock, resp, sizeof(resp));
    if (n <= 0) { printf("Server closed the connection unexpectedly.\n"); exit(1); }
    printf("Server: %s\n", resp);
}

/* LIST gets multiple FLIGHT lines followed by an END marker, so keep
   reading until we see it */
void do_list(int sock) {
    if (send_line(sock, "LIST") < 0) { printf("Connection lost while sending. Exiting.\n"); exit(1); }
    printf("%-8s %-12s %-6s\n", "Flight", "Status", "Gate");
    printf("--------------------------------\n");
    char resp[BUF_SIZE];
    while (1) {
        ssize_t n = recv_line(sock, resp, sizeof(resp));
        if (n <= 0) { printf("Server closed the connection unexpectedly.\n"); exit(1); }
        if (strcmp(resp, "END") == 0) break;
        char tag[16], fno[16], status[16], gate[16];
        if (sscanf(resp, "%15s %15s %15s %15s", tag, fno, status, gate) == 4)
            printf("%-8s %-12s %-6s\n", fno, status, gate);
    }
}

int main(int argc, char *argv[]) {
    const char *server_ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int server_port = (argc > 2) ? atoi(argv[2]) : SERVER_PORT;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP address: %s\n", server_ip);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        printf("Could not reach the airport server at %s:%d. Is it running?\n", server_ip, server_port);
        close(sock);
        return 1;
    }

    printf("Connected to Airport Control Tower Server at %s:%d\n", server_ip, server_port);

    /* --- login before anything else is allowed --- */
    char username[64], password[64];
    int authenticated = 0;
    while (!authenticated) {
        printf("Username: ");
        if (!fgets(username, sizeof(username), stdin)) { close(sock); return 0; }
        strip_newline(username);

        printf("Password: ");
        if (!fgets(password, sizeof(password), stdin)) { close(sock); return 0; }
        strip_newline(password);

        char line[BUF_SIZE];
        snprintf(line, sizeof(line), "AUTH %s %s", username, password);
        if (send_line(sock, line) < 0) { printf("Connection lost.\n"); close(sock); return 1; }

        char resp[BUF_SIZE];
        ssize_t n = recv_line(sock, resp, sizeof(resp));
        if (n <= 0) { printf("Server closed the connection.\n"); close(sock); return 1; }

        printf("Server: %s\n", resp);
        if (strncmp(resp, "OK AUTH", 7) == 0) authenticated = 1;
        else printf("Login failed, try again.\n");
    }

    /* --- main menu loop --- */
    int running = 1;
    while (running) {
        printf("\n=== Airport Terminal ===\n");
        printf(" 1. List all flights\n");
        printf(" 2. Check flight status\n");
        printf(" 3. Check flight gate\n");
        printf(" 4. Update flight status (admin only)\n");
        printf(" 5. Quit\n");
        printf("Choose an option: ");

        char choice_line[16];
        if (!fgets(choice_line, sizeof(choice_line), stdin)) break;
        int choice = atoi(choice_line);

        char flight_no[64], status[64], line[BUF_SIZE];

        switch (choice) {
            case 1:
                do_list(sock);
                break;

            case 2:
                printf("Flight number: ");
                if (!fgets(flight_no, sizeof(flight_no), stdin)) break;
                strip_newline(flight_no);
                snprintf(line, sizeof(line), "STATUS %s", flight_no);
                send_and_print(sock, line);
                break;

            case 3:
                printf("Flight number: ");
                if (!fgets(flight_no, sizeof(flight_no), stdin)) break;
                strip_newline(flight_no);
                snprintf(line, sizeof(line), "GATE %s", flight_no);
                send_and_print(sock, line);
                break;

            case 4:
                printf("Flight number: ");
                if (!fgets(flight_no, sizeof(flight_no), stdin)) break;
                strip_newline(flight_no);
                printf("New status (e.g. \"DELAYED\"): ");
                if (!fgets(status, sizeof(status), stdin)) break;
                strip_newline(status);
                snprintf(line, sizeof(line), "UPDATE %s %s", flight_no, status);
                send_and_print(sock, line);
                break;

            case 5: {
                send_line(sock, "QUIT");
                char resp[BUF_SIZE];
                recv_line(sock, resp, sizeof(resp)); /* read the OK BYE, ignore result */
                running = 0;
                break;
            }

            default:
                printf("Not a valid option.\n");
        }
    }

    printf("Disconnecting.\n");
    close(sock);
    return 0;
}