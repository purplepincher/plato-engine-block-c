/*
 * sensors_dummy.c — Example sensor and actuator implementations.
 *
 * Provides CPU-temp simulator, random, and constant sensors, plus
 * a no-op actuator for testing.
 */

/* #define PLATO_ENGINE_IMPL — provided by the including compilation unit */
#include "plato_engine.h"

#include <math.h>
#include <time.h>

/* ---- sensors ---- */

static double sensor_cpu_temp(void *ud) {
    /* Simulated CPU temperature: sinusoidal + noise, 45-85°C */
    (void)ud;
    static bool seeded = false;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = true; }
    double base = 65.0 + 10.0 * sin((double)clock() / CLOCKS_PER_SEC * 0.1);
    double noise = ((double)rand() / RAND_MAX - 0.5) * 4.0;
    return base + noise;
}

static double sensor_random(void *ud) {
    (void)ud;
    static bool seeded = false;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = true; }
    return ((double)rand() / RAND_MAX) * 100.0;
}

static double sensor_constant(void *ud) {
    double *val = (double *)ud;
    return *val;
}

/* ---- actuators ---- */

static int actuator_noop(const char *name, double value, void *ud) {
    (void)name; (void)value; (void)ud;
    return 0;  /* success — does nothing */
}

/* ---- public helper: populate an engine with demo sensors ---- */

void plato_load_dummy_sensors(plato_engine_t *eng) {
    static double const_val = 42.0;

    plato_add_sensor(eng, "cpu_temp",  sensor_cpu_temp,  NULL);
    plato_add_sensor(eng, "random",    sensor_random,    NULL);
    plato_add_sensor(eng, "constant",  sensor_constant,  &const_val);

    plato_add_actuator(eng, "fan_speed", actuator_noop, NULL);
    plato_add_actuator(eng, "led",       actuator_noop, NULL);

    plato_add_alarm(eng, "overheat",  0, PLATO_GT,  80.0, PLATO_CRIT);
    plato_add_alarm(eng, "chilly",    0, PLATO_LT,  50.0, PLATO_WARN);
    plato_add_alarm(eng, "lucky",     1, PLATO_GTE, 90.0, PLATO_INFO);
}
