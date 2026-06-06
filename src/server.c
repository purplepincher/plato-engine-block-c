/*
 * server.c — Multi-client TCP server for the Plato Engine Block.
 *
 * Listens on a configurable port, accepts multiple clients via poll(),
 * broadcasts ticks to subscribers, and handles commands line-by-line.
 *
 * Usage:
 *   ./plato_server [port]       — default port 7070
 *   ./plato_server 9090         — custom port
 */

#define _POSIX_C_SOURCE 200809L
#define PLATO_ENGINE_IMPL
#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Pull in dummy sensors */
extern void plato_load_dummy_sensors(plato_engine_t *eng);

#define MAX_CLIENTS    64
#define BUF_SIZE       4096
#define TICK_INTERVAL  2000   /* ms between auto-ticks */

static volatile sig_atomic_t g_running = 1;
static void on_signal(int sig) { (void)sig; g_running = 0; }

typedef struct {
    int  fd;
    bool subscribed;
    char rbuf[BUF_SIZE];
    int  rlen;
} client_t;

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void broadcast(plato_engine_t *eng, client_t *clients, int nclients,
                      const char *msg, int msglen) {
    for (int i = 0; i < nclients; i++) {
        if (clients[i].fd >= 0 && clients[i].subscribed) {
            send(clients[i].fd, msg, msglen, MSG_NOSIGNAL);
            send(clients[i].fd, "\n", 1, MSG_NOSIGNAL);
        }
    }
}

int main(int argc, char **argv) {
    int port = 7070;
    if (argc >= 2) port = atoi(argv[1]);
    if (port <= 0) port = 7070;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    /* init engine */
    plato_engine_t eng;
    plato_init(&eng);
    plato_load_dummy_sensors(&eng);

    /* create listening socket */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblock(lfd);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(lfd); return 1;
    }
    if (listen(lfd, 16) < 0) {
        perror("listen"); close(lfd); return 1;
    }

    printf("Plato Engine Block — TCP server on :%d\n", port);
    printf("Sensors: %d  Actuators: %d  Alarms: %d\n",
           eng.sensor_count, eng.actuator_count, eng.alarm_count);

    /* client state */
    client_t clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].subscribed = false;
        clients[i].rlen = 0;
    }

    char resp[PLATO_RESP_BUF];

    while (g_running) {
        /* build poll set: listener + all clients */
        struct pollfd fds[1 + MAX_CLIENTS];
        int nfds = 0;

        fds[0].fd = lfd;
        fds[0].events = POLLIN;
        nfds = 1;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd >= 0) {
                fds[nfds].fd     = clients[i].fd;
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }

        int ret = poll(fds, nfds, TICK_INTERVAL);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll"); break;
        }

        /* auto-tick on timeout */
        if (ret == 0) {
            int n = plato_handle_command(&eng, "tick", resp, sizeof(resp));
            broadcast(&eng, clients, MAX_CLIENTS, resp, n);
            continue;
        }

        /* accept new connections */
        if (fds[0].revents & POLLIN) {
            int cfd = accept(lfd, NULL, NULL);
            if (cfd >= 0) {
                set_nonblock(cfd);
                const char *welcome = "Plato Engine Block — type 'help' for commands\n";
                send(cfd, welcome, strlen(welcome), MSG_NOSIGNAL);
                /* find slot */
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd < 0) { slot = i; break; }
                }
                if (slot >= 0) {
                    clients[slot].fd = cfd;
                    clients[slot].subscribed = false;
                    clients[slot].rlen = 0;
                    printf("client connected (slot %d, fd %d)\n", slot, cfd);
                } else {
                    const char *full = "err server full\n";
                    send(cfd, full, strlen(full), MSG_NOSIGNAL);
                    close(cfd);
                }
            }
        }

        /* handle client data */
        int fi = 1;
        for (int i = 0; i < MAX_CLIENTS && fi < nfds; i++) {
            if (clients[i].fd < 0) continue;

            /* find matching pollfd */
            while (fi < nfds && fds[fi].fd != clients[i].fd) fi++;
            if (fi >= nfds) break;

            if (!(fds[fi].revents & (POLLIN | POLLHUP | POLLERR))) { fi++; continue; }

            ssize_t r = recv(clients[i].fd,
                             clients[i].rbuf + clients[i].rlen,
                             BUF_SIZE - clients[i].rlen - 1, 0);
            if (r <= 0) {
                /* disconnect */
                printf("client disconnected (slot %d)\n", i);
                close(clients[i].fd);
                clients[i].fd = -1;
                clients[i].subscribed = false;
                fi++;
                continue;
            }
            clients[i].rlen += r;
            clients[i].rbuf[clients[i].rlen] = '\0';

            /* process complete lines */
            char *line = clients[i].rbuf;
            char *nl;
            while ((nl = strchr(line, '\n')) != NULL) {
                *nl = '\0';
                /* trim \r */
                char *cr = strchr(line, '\r');
                if (cr) *cr = '\0';

                if (strlen(line) == 0) { line = nl + 1; continue; }

                if (strcmp(line, "quit") == 0) {
                    close(clients[i].fd);
                    clients[i].fd = -1;
                    clients[i].subscribed = false;
                    break;
                }

                /* handle subscribe/unsubscribe locally */
                if (strcmp(line, "subscribe") == 0) {
                    clients[i].subscribed = true;
                    const char *ok = "ok subscribed\n";
                    send(clients[i].fd, ok, strlen(ok), MSG_NOSIGNAL);
                } else if (strcmp(line, "unsubscribe") == 0) {
                    clients[i].subscribed = false;
                    const char *ok = "ok unsubscribed\n";
                    send(clients[i].fd, ok, strlen(ok), MSG_NOSIGNAL);
                } else {
                    int n = plato_handle_command(&eng, line, resp, sizeof(resp));
                    send(clients[i].fd, resp, n, MSG_NOSIGNAL);
                    send(clients[i].fd, "\n", 1, MSG_NOSIGNAL);
                }
                line = nl + 1;
            }

            /* shift remaining partial line */
            if (clients[i].fd >= 0) {
                int remaining = clients[i].rlen - (line - clients[i].rbuf);
                if (remaining > 0 && line != clients[i].rbuf) {
                    memmove(clients[i].rbuf, line, remaining);
                }
                clients[i].rlen = remaining;
            }

            fi++;
        }
    }

    /* cleanup */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) close(clients[i].fd);
    }
    close(lfd);
    printf("\nShut down. %llu ticks served.\n",
           (unsigned long long)eng.tick_num);
    return 0;
}
