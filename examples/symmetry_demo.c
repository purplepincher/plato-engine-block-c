/*
 * examples/symmetry_demo.c — Symmetry-Sensing Demo.
 *
 * Demonstrates the TernARY upgrade features:
 *   1. Ternary-Continuous Shim — conviction → trit mapping
 *   2. Veto System — PLATO_VETO severity blocks actuator writes
 *   3. Symmetry Check — cross-correlation between multi-sensor pairs
 *
 * Three sensors: sine, cosine, and a "drift" sensor that starts in-phase
 * with sine then decouples — triggering a symmetry alarm + veto.
 *
 * Build:  cc -o symmetry_demo examples/symmetry_demo.c -Iinclude -lm
 * Run:    ./symmetry_demo
 */

#define PLATO_ENGINE_IMPL
#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ---- Sensor callbacks ---- */

static double sensor_sine(void *ud) {
    double *phase = (double *)ud;
    double val = sin(*phase) * 10.0 + 50.0;   /* 40–60, centred on 50 */
    *phase += 0.2;
    return val;
}

static double sensor_cosine(void *ud) {
    double *phase = (double *)ud;
    double val = cos(*phase) * 10.0 + 50.0;   /* 40–60, 90° out */
    *phase += 0.2;
    return val;
}

static double sensor_drift(void *ud) {
    /* Starts tracking sine, then "drifts" into a flat line after tick 15 */
    double *tick = (double *)ud;
    double t = *tick;
    if (t < 15.0) {
        return sin(t * 0.2) * 10.0 + 50.0;    /* in phase with sine */
    }
    return 50.0;   /* flatline — breaks symmetry */
}

/* ---- Actuator callbacks ---- */

static int g_actuator_vals_written = 0;
static double g_last_write_val = 0.0;

static int actuator_demo(const char *name, double value, void *ud) {
    (void)name; (void)ud;
    g_actuator_vals_written++;
    g_last_write_val = value;
    printf("[ACTUATOR] %s set to %.2f\n", name, value);
    return 0;
}

int main(void) {
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Plato TernARY — Symmetry-Sensing Demo\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    /* ---- setup engine ---- */
    plato_engine_t eng;
    plato_init(&eng);

    double phase1 = 0.0, phase2 = 0.0, drift_tick = 0.0;

    int s_sine   = plato_add_sensor(&eng, "sine",   sensor_sine,   &phase1);
    int s_cosine = plato_add_sensor(&eng, "cosine", sensor_cosine, &phase2);
    int s_drift  = plato_add_sensor(&eng, "drift",  sensor_drift,  &drift_tick);

    /* Add a symmetry pair for passive monitoring */
    plato_add_symmetry_pair(&eng, "sine-drift", s_sine, s_drift, 0.75);

    /* Alarm: standard threshold on sine exceeding 55 */
    plato_add_alarm(&eng, "sine_high", s_sine, PLATO_GT, 55.0, PLATO_WARN);

    /* Alarm: symphony break — sine vs drift decoupled → VETO */
    plato_add_symmetry_alarm(&eng, "drift_veto", s_sine, s_drift,
                              0.75, PLATO_VETO);

    /* Actuator: a cooling fan that will be blocked by veto */
    plato_add_actuator(&eng, "cooling_fan", actuator_demo, NULL);

    /* ---- run ticks ---- */
    printf("Running 25 ticks...\n");
    printf("Ticks 0–14: drift tracks sine (symmetric)\n");
    printf("Ticks 15+:  drift flatlines (breaks symmetry → VETO)\n\n");

    char resp[PLATO_RESP_BUF];

    for (int i = 0; i < 25; i++) {
        plato_tick(&eng);
        drift_tick += 1.0;

        /* Try to write actuator every tick (will be blocked by veto later) */
        plato_handle_command(&eng, "cooling_fan 75", resp, sizeof(resp));

        /* ---- Report this tick ---- */
        printf("── tick %2llu ──\n", (unsigned long long)eng.tick_num);
        printf("  sine=%.2f  cosine=%.2f  drift=%.2f\n",
               eng.last_tick[s_sine],
               eng.last_tick[s_cosine],
               eng.last_tick[s_drift]);

        /* Symmetry pair state */
        printf("  sym 'sine-drift': r=%.3f  %s\n",
               eng.symmetry_pairs[0].last_correlation,
               eng.symmetry_pairs[0].symmetric ? "✓ SYMMETRIC" : "✗ BROKEN");

        /* Alarms firing */
        for (int a = 0; a < eng.alarm_count; a++) {
            if (eng.alarms[a].firing) {
                printf("  ⚡ ALARM '%s'  [%s]\n",
                       eng.alarms[a].name,
                       severity_str(eng.alarms[a].severity));
            }
        }

        /* Veto state */
        if (eng.veto_active) {
            printf("  ⛔ VETO ACTIVE — source: '%s'\n", eng.veto_source_name);
            printf("  ⛔ Actuator writes BLOCKED\n");
        }

        /* ---- Ternary-Continuous demo: show conviction ↔ trit ---- */
        plato_conviction_t c = plato_normalise_conviction(
            eng.last_tick[s_sine], 40.0, 60.0);
        plato_trit_t trit = plato_conviction_to_trit(c);
        printf("  sac conv=%.3f → trit=%s  %s\n",
               c, trit_str(trit),
               plato_is_lemminal(c) ? "(⚡ Leminal Zone)" : "");

        printf("\n");
    }

    /* ---- Summary ---- */
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Demo Summary\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Total ticks:              %llu\n", (unsigned long long)eng.tick_num);
    printf("  Symmetry 'sine-drift':    r=%.3f (last)\n",
           eng.symmetry_pairs[0].last_correlation);
    printf("  Symmetry final state:     %s\n",
           eng.symmetry_pairs[0].symmetric ? "✓ SYMMETRIC" : "✗ BROKEN");
    printf("  Actuator writes allowed:  %d\n", g_actuator_vals_written);
    printf("  Actuator writes blocked:  %d (by veto)\n",
           g_actuator_vals_written > 0 ? 25 - g_actuator_vals_written : 25);
    printf("  Veto was active:          %s\n",
           eng.veto_active ? "yes" : "no");

    printf("\n  Final conviction for sine:\n");
    plato_conviction_t fc = plato_normalise_conviction(
        eng.last_tick[s_sine], 40.0, 60.0);
    printf("    normalised = %.3f  trit = %s\n", fc,
           trit_str(plato_conviction_to_trit(fc)));

    printf("\nDone.\n");
    return 0;
}
