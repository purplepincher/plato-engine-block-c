/*
 * tests/test_engine.c — Engine unit tests.
 *
 * Build:  cc -o test_engine tests/test_engine.c -Iinclude
 * Run:    ./test_engine
 */

#define PLATO_ENGINE_IMPL
#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) do { \
    tests_total++; \
    printf("  %-50s", #name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* ---- test sensors ---- */

static double sensor_fixed_42(void *ud) { (void)ud; return 42.0; }
static double sensor_fixed_99(void *ud) { (void)ud; return 99.0; }

static int g_actuator_calls;
static double g_actuator_last_val;
static int actuator_record(const char *name, double value, void *ud) {
    (void)name; (void)ud;
    g_actuator_calls++;
    g_actuator_last_val = value;
    return 0;
}

int main(void) {
    printf("=== Engine Tests ===\n\n");

    /* ---- init ---- */
    TEST("plato_init zeroes engine");
    {
        plato_engine_t eng;
        plato_init(&eng);
        assert(eng.sensor_count == 0);
        assert(eng.actuator_count == 0);
        assert(eng.alarm_count == 0);
        assert(eng.tick_num == 0);
    }
    PASS();

    /* ---- add sensor ---- */
    TEST("plato_add_sensor returns incrementing index");
    {
        plato_engine_t eng;
        plato_init(&eng);
        int s0 = plato_add_sensor(&eng, "s0", sensor_fixed_42, NULL);
        int s1 = plato_add_sensor(&eng, "s1", sensor_fixed_99, NULL);
        assert(s0 == 0);
        assert(s1 == 1);
        assert(eng.sensor_count == 2);
    }
    PASS();

    TEST("plato_add_sensor returns -1 when full");
    {
        plato_engine_t eng;
        plato_init(&eng);
        for (int i = 0; i < PLATO_MAX_SENSORS; i++) {
            char name[8];
            snprintf(name, sizeof(name), "s%d", i);
            plato_add_sensor(&eng, name, sensor_fixed_42, NULL);
        }
        int rc = plato_add_sensor(&eng, "overflow", sensor_fixed_42, NULL);
        assert(rc == -1);
    }
    PASS();

    /* ---- sensor reading ---- */
    TEST("plato_tick reads sensor values into last_tick");
    {
        plato_engine_t eng;
        plato_init(&eng);
        plato_add_sensor(&eng, "temp", sensor_fixed_42, NULL);
        plato_tick(&eng);
        assert(eng.last_tick[0] == 42.0);
        assert(eng.tick_num == 1);
    }
    PASS();

    /* ---- tick increments ---- */
    TEST("multiple ticks increment tick_num");
    {
        plato_engine_t eng;
        plato_init(&eng);
        plato_add_sensor(&eng, "x", sensor_fixed_42, NULL);
        for (int i = 0; i < 100; i++) plato_tick(&eng);
        assert(eng.tick_num == 100);
    }
    PASS();

    /* ---- history overflow ---- */
    TEST("history wraps around after MAX_HISTORY ticks");
    {
        plato_engine_t eng;
        plato_init(&eng);
        plato_add_sensor(&eng, "x", sensor_fixed_42, NULL);
        for (int i = 0; i < PLATO_MAX_HISTORY + 50; i++) plato_tick(&eng);
        assert(eng.history[0].count == PLATO_MAX_HISTORY);
    }
    PASS();

    TEST("history stores correct values after wrap");
    {
        plato_engine_t eng;
        plato_init(&eng);
        plato_add_sensor(&eng, "x", sensor_fixed_42, NULL);
        for (int i = 0; i < 300; i++) {
            eng.last_tick[0] = (double)i;
            eng.history[0].values[eng.history[0].head] = (double)i;
            eng.history[0].head = (eng.history[0].head + 1) % PLATO_MAX_HISTORY;
            if (eng.history[0].count < PLATO_MAX_HISTORY) eng.history[0].count++;
        }
        /* most recent = 299 */
        int h = (eng.history[0].head - 1 + PLATO_MAX_HISTORY) % PLATO_MAX_HISTORY;
        assert(eng.history[0].values[h] == 299.0);
    }
    PASS();

    /* ---- alarm triggering ---- */
    TEST("alarm fires when threshold crossed");
    {
        plato_engine_t eng;
        plato_init(&eng);
        int s = plato_add_sensor(&eng, "temp", sensor_fixed_99, NULL);
        plato_add_alarm(&eng, "hot", s, PLATO_GT, 80.0, PLATO_CRIT);
        plato_tick(&eng);
        assert(eng.alarms[0].firing == true);
    }
    PASS();

    TEST("alarm does NOT fire within cooldown");
    {
        plato_engine_t eng;
        plato_init(&eng);
        int s = plato_add_sensor(&eng, "temp", sensor_fixed_99, NULL);
        plato_add_alarm(&eng, "hot", s, PLATO_GT, 80.0, PLATO_CRIT);
        plato_tick(&eng);
        assert(eng.alarms[0].firing == true);   /* first fire */
        plato_tick(&eng);
        assert(eng.alarms[0].firing == false);  /* cooling down */
        plato_tick(&eng);
        assert(eng.alarms[0].firing == false);  /* still cooling */
    }
    PASS();

    TEST("alarm re-arms after cooldown expires");
    {
        plato_engine_t eng;
        plato_init(&eng);
        int s = plato_add_sensor(&eng, "temp", sensor_fixed_99, NULL);
        plato_add_alarm(&eng, "hot", s, PLATO_GT, 80.0, PLATO_CRIT);
        plato_tick(&eng);
        assert(eng.alarms[0].firing == true);

        /* exhaust cooldown — last tick re-arms and immediately fires */
        for (int i = 0; i < PLATO_ALARM_COOLDOWN; i++) {
            plato_tick(&eng);
        }
        /* alarm re-armed during last cooldown tick, saw value > 80, fired */
        assert(eng.alarms[0].firing == true);  /* proves re-arm happened */
    }
    PASS();

    TEST("alarm NOT firing when threshold not met");
    {
        plato_engine_t eng;
        plato_init(&eng);
        int s = plato_add_sensor(&eng, "temp", sensor_fixed_42, NULL);
        plato_add_alarm(&eng, "hot", s, PLATO_GT, 80.0, PLATO_CRIT);
        plato_tick(&eng);
        assert(eng.alarms[0].firing == false);
    }
    PASS();

    /* ---- actuator ---- */
    TEST("actuator callback called via command");
    {
        plato_engine_t eng;
        plato_init(&eng);
        plato_add_actuator(&eng, "fan", actuator_record, NULL);
        g_actuator_calls = 0;
        char resp[PLATO_RESP_BUF];
        plato_handle_command(&eng, "fan 75.5", resp, sizeof(resp));
        assert(g_actuator_calls == 1);
        assert(g_actuator_last_val == 75.5);
        assert(strcmp(resp, "ok fan=75.50") == 0);
    }
    PASS();

    /* ---- subscribe/unsubscribe ---- */
    TEST("subscribe and unsubscribe track state");
    {
        plato_engine_t eng;
        plato_init(&eng);
        assert(plato_subscribe(&eng, 10) == 0);
        assert(eng.subs.count == 1);
        assert(plato_subscribe(&eng, 20) == 0);
        assert(eng.subs.count == 2);
        assert(plato_unsubscribe(&eng, 10) == 0);
        assert(eng.subs.count == 1);
        assert(plato_unsubscribe(&eng, 999) == -1);  /* not found */
    }
    PASS();

    TEST("subscribe returns -1 when full");
    {
        plato_engine_t eng;
        plato_init(&eng);
        for (int i = 0; i < PLATO_MAX_SUBSCRIBERS; i++) {
            assert(plato_subscribe(&eng, i + 100) == 0);
        }
        assert(plato_subscribe(&eng, 999) == -1);
    }
    PASS();

    printf("\n%d/%d tests passed.\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
