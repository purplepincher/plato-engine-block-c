/*
 * examples/minimal.c — Minimal example: 1 sensor, print ticks.
 *
 * Build:  cc -o minimal examples/minimal.c -Iinclude
 * Run:    ./minimal
 */

#define PLATO_ENGINE_IMPL
#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>

static double sensor_counter(void *ud) {
    double *count = (double *)ud;
    return (*count)++;
}

int main(void) {
    plato_engine_t eng;
    plato_init(&eng);

    double counter = 0.0;
    plato_add_sensor(&eng, "counter", sensor_counter, &counter);

    printf("Minimal Plato Engine — 5 ticks\n\n");

    char resp[PLATO_RESP_BUF];
    for (int i = 0; i < 5; i++) {
        plato_handle_command(&eng, "tick", resp, sizeof(resp));
        printf("%s\n", resp);
    }

    printf("\nDone.\n");
    return 0;
}
