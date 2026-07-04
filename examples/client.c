/*
 * examples/client.c — TCP client for the Plato Engine Block server.
 *
 * Connects to the poll()-based server (default 127.0.0.1:7070), reads the
 * welcome banner, runs a short real command exchange (help, tick, history,
 * quit), and prints every byte it receives. Exits 0 on success.
 *
 * This uses the single-header API only for shared constants/buffer sizes
 * (PLATO_RESP_BUF, PLATO_CMD_BUF) — the engine itself runs in the server.
 *
 * Build:  cc -o client examples/client.c -Iinclude -lm
 * Run:    ./plato_server 7070        # in another terminal
 *         ./client                   # default host 127.0.0.1, port 7070
 *         ./client 127.0.0.1 9090
 */

#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define RECV_TIMEOUT_MS 800

/* read all data currently available; stops on timeout or close */
static int recv_response(int fd, char *buf, size_t buflen) {
    size_t total = 0;
    while (total < buflen - 1) {
        ssize_t r = recv(fd, buf + total, buflen - 1 - total, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; /* timeout */
            return -1;
        }
        if (r == 0) break;  /* server closed */
        total += (size_t)r;
    }
    buf[total] = '\0';
    return (int)total;
}

static int send_cmd(int fd, const char *cmd) {
    char line[PLATO_CMD_BUF];
    size_t len = strlen(cmd);
    if (len + 2 > sizeof(line)) return -1;
    memcpy(line, cmd, len);
    line[len] = '\n';
    line[len + 1] = '\0';
    size_t off = 0;
    while (off < len + 1) {
        ssize_t s = send(fd, line + off, len + 1 - off, 0);
        if (s < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)s;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *host = (argc >= 2) ? argv[1] : "127.0.0.1";
    int port = (argc >= 3) ? atoi(argv[2]) : 7070;
    if (port <= 0) port = 7070;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct timeval tv = { .tv_sec = RECV_TIMEOUT_MS / 1000,
                          .tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid host '%s' (use an IPv4 address)\n", host);
        close(fd); return 1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd); return 1;
    }

    printf("Connected to %s:%d\n\n", host, port);

    char buf[PLATO_RESP_BUF * 2];
    int rc = 0;

    /* 1. welcome banner */
    if (recv_response(fd, buf, sizeof(buf)) <= 0) {
        fprintf(stderr, "error: no welcome banner\n"); rc = 1; goto done;
    }
    printf("<< %s", buf);

    /* 2. help — multi-line response */
    if (send_cmd(fd, "help") < 0) { perror("send help"); rc = 1; goto done; }
    if (recv_response(fd, buf, sizeof(buf)) <= 0) {
        fprintf(stderr, "error: no help response\n"); rc = 1; goto done;
    }
    printf(">> help\n<< %s\n", buf);

    /* 3. tick — a real sensor read from the server */
    if (send_cmd(fd, "tick") < 0) { perror("send tick"); rc = 1; goto done; }
    if (recv_response(fd, buf, sizeof(buf)) <= 0) {
        fprintf(stderr, "error: no tick response\n"); rc = 1; goto done;
    }
    printf(">> tick\n<< %s", buf);
    if (strncmp(buf, "tick ", 5) != 0) {
        fprintf(stderr, "error: unexpected tick response\n"); rc = 1; goto done;
    }

    /* 4. history — exercises the server-side history buffer */
    if (send_cmd(fd, "history 5") < 0) { perror("send history"); rc = 1; goto done; }
    if (recv_response(fd, buf, sizeof(buf)) <= 0) {
        fprintf(stderr, "error: no history response\n"); rc = 1; goto done;
    }
    printf(">> history 5\n<< %s\n", buf);

    /* 5. quit — server must reply "bye" then close */
    if (send_cmd(fd, "quit") < 0) { perror("send quit"); rc = 1; goto done; }
    int n = recv_response(fd, buf, sizeof(buf));
    if (n <= 0) {
        fprintf(stderr, "error: no 'bye' received on quit\n"); rc = 1; goto done;
    }
    printf(">> quit\n<< %s", buf);
    if (strncmp(buf, "bye", 3) != 0) {
        fprintf(stderr, "error: expected 'bye'\n"); rc = 1; goto done;
    }

    printf("\nSuccess — sent/received real messages, got 'bye', done.\n");

done:
    close(fd);
    return rc;
}
