/*
 * examples/multi_sensor.c — Multiple sensors with history.
 *
 * Build:  cc -o multi_sensor examples/multi_sensor.c -Iinclude -lm
 * Run:    ./multi_sensor
 */

#define PLATO_ENGINE_IMPL
#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static double sensor_sine(void *ud) {
    double *phase = (double *)ud;
    double val = sin(*phase) * 50.0 + 50.0;
    *phase += 0.3;
    return val;
}

static double sensor_cosine(void *ud) {
    double *phase = (double *)ud;
    double val = cos(*phase) * 50.0 + 50.0;
    *phase += 0.3;
    return val;
}

int main(void) {
    plato_engine_t eng;
    plato_init(&eng);

    double phase1 = 0.0, phase2 = 0.0;
    int s1 = plato_add_sensor(&eng, "sine",   sensor_sine,   &phase1);
    int s2 = plato_add_sensor(&eng, "cosine", sensor_cosine, &phase2);

    printf("Multi-Sensor Demo — 20 ticks, then history\n\n");

    for (int i = 0; i < 20; i++) {
        plato_tick(&eng);
    }

    /* print history */
    printf("Sine history (last 10):\n  ");
    for (int j = 0; j < 10 && j < eng.history[s1].count; j++) {
        printf("%.1f ", eng.history[s1].values[
            (eng.history[s1].head - 1 - j + PLATO_MAX_HISTORY) % PLATO_MAX_HISTORY]);
    }
    printf("\n\nCosine history (last 10):\n  ");
    for (int j = 0; j < 10 && j < eng.history[s2].count; j++) {
        printf("%.1f ", eng.history[s2].values[
            (eng.history[s2].head - 1 - j + PLATO_MAX_HISTORY) % PLATO_MAX_HISTORY]);
    }
    printf("\n");

    printf("\nTotal ticks: %llu\n", (unsigned long long)eng.tick_num);
    return 0;
}
