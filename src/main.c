/*
 * main.c — Standalone Plato Engine daemon.
 *
 * Runs a poll()-based tick loop on stdin, reading commands line-by-line.
 * Use for quick testing without a TCP server.
 *
 * Usage:
 *   ./plato_engine          — interactive, one tick per 'tick' command
 *   ./plato_engine -a N     — auto-tick every N ms
 */

#define PLATO_ENGINE_IMPL
#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>

/* Pull in dummy sensors */
extern void plato_load_dummy_sensors(plato_engine_t *eng);

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char **argv) {
    int auto_ms = 0;
    if (argc >= 3 && strcmp(argv[1], "-a") == 0) {
        auto_ms = atoi(argv[2]);
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    plato_engine_t eng;
    plato_init(&eng);
    plato_load_dummy_sensors(&eng);

    printf("Plato Engine Block — standalone daemon\n");
    printf("Sensors: %d  Actuators: %d  Alarms: %d\n",
           eng.sensor_count, eng.actuator_count, eng.alarm_count);
    printf("Auto-tick: %s\n", auto_ms > 0 ? argv[2] : "off");
    printf("Type 'help' for commands.\n\n");

    char line[PLATO_CMD_BUF];
    char resp[PLATO_RESP_BUF];

    while (g_running) {
        if (auto_ms > 0) {
            struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
            int ret = poll(&pfd, 1, auto_ms);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                if (!fgets(line, sizeof(line), stdin)) break;
                line[strcspn(line, "\r\n")] = '\0';
                if (strcmp(line, "quit") == 0) break;
                int n = plato_handle_command(&eng, line, resp, sizeof(resp));
                printf("%s\n", resp);
                fflush(stdout);
            } else if (ret == 0) {
                /* timeout → auto tick */
                int n = plato_handle_command(&eng, "tick", resp, sizeof(resp));
                printf("%s\n", resp);
                fflush(stdout);
            }
        } else {
            /* blocking mode */
            if (!fgets(line, sizeof(line), stdin)) break;
            line[strcspn(line, "\r\n")] = '\0';
            if (strlen(line) == 0) continue;
            if (strcmp(line, "quit") == 0) break;
            int n = plato_handle_command(&eng, line, resp, sizeof(resp));
            printf("%s\n", resp);
            fflush(stdout);
        }
    }

    printf("\nShutting down. %llu ticks served.\n",
           (unsigned long long)eng.tick_num);
    return 0;
}
