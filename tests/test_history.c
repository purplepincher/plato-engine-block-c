/*
 * tests/test_history.c — History buffer tests.
 *
 * Build:  cc -o test_history tests/test_history.c -Iinclude
 * Run:    ./test_history
 */

#define PLATO_ENGINE_IMPL
#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) do { tests_total++; printf("  %-50s", #name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)

static double history_get_test(const plato_history_t *h, int n) {
    if (n < 0 || n >= h->count) return 0.0;
    int idx = (h->head - 1 - n + PLATO_MAX_HISTORY) % PLATO_MAX_HISTORY;
    return h->values[idx];
}

/* file-scope sensor helpers */
static double g_val;
static double sensor_val(void *ud) { (void)ud; return g_val; }

static double g_v1, g_v2;
static double s1(void *ud) { (void)ud; return g_v1; }
static double s2(void *ud) { (void)ud; return g_v2; }

int main(void) {
    printf("=== History Buffer Tests ===\n\n");

    TEST("push and retrieve single value");
    {
        plato_history_t h = {0};
        h.values[h.head] = 42.0;
        h.head = (h.head + 1) % PLATO_MAX_HISTORY;
        h.count = 1;
        assert(history_get_test(&h, 0) == 42.0);
    }
    PASS();

    TEST("push multiple values, most recent first");
    {
        plato_history_t h = {0};
        for (int i = 0; i < 5; i++) {
            h.values[h.head] = (double)i;
            h.head = (h.head + 1) % PLATO_MAX_HISTORY;
            if (h.count < PLATO_MAX_HISTORY) h.count++;
        }
        assert(history_get_test(&h, 0) == 4.0);
        assert(history_get_test(&h, 1) == 3.0);
        assert(history_get_test(&h, 4) == 0.0);
    }
    PASS();

    TEST("out-of-bounds get returns 0");
    {
        plato_history_t h = {0};
        h.values[0] = 1.0; h.head = 1; h.count = 1;
        assert(history_get_test(&h, 5) == 0.0);
        assert(history_get_test(&h, -1) == 0.0);
    }
    PASS();

    TEST("buffer wraps at MAX_HISTORY");
    {
        plato_history_t h = {0};
        for (int i = 0; i < PLATO_MAX_HISTORY + 20; i++) {
            h.values[h.head] = (double)i;
            h.head = (h.head + 1) % PLATO_MAX_HISTORY;
            if (h.count < PLATO_MAX_HISTORY) h.count++;
        }
        assert(h.count == PLATO_MAX_HISTORY);
        assert(history_get_test(&h, 0) == (double)(PLATO_MAX_HISTORY + 19));
        assert(history_get_test(&h, PLATO_MAX_HISTORY - 1) == 20.0);
    }
    PASS();

    TEST("engine history via plato_tick");
    {
        plato_engine_t eng;
        plato_init(&eng);
        plato_add_sensor(&eng, "x", sensor_val, NULL);

        g_val = 10.0; plato_tick(&eng);
        g_val = 20.0; plato_tick(&eng);
        g_val = 30.0; plato_tick(&eng);

        assert(eng.history[0].count == 3);
        assert(history_get_test(&eng.history[0], 0) == 30.0);
        assert(history_get_test(&eng.history[0], 1) == 20.0);
        assert(history_get_test(&eng.history[0], 2) == 10.0);
    }
    PASS();

    TEST("engine history accumulates across many ticks");
    {
        plato_engine_t eng;
        plato_init(&eng);
        plato_add_sensor(&eng, "x", sensor_val, NULL);

        for (int i = 0; i < 500; i++) {
            g_val = (double)i;
            plato_tick(&eng);
        }
        assert(eng.tick_num == 500);
        assert(eng.history[0].count == PLATO_MAX_HISTORY);
        assert(history_get_test(&eng.history[0], 0) == 499.0);
    }
    PASS();

    TEST("multiple sensors have independent history");
    {
        plato_engine_t eng;
        plato_init(&eng);
        int i1 = plato_add_sensor(&eng, "s1", s1, NULL);
        int i2 = plato_add_sensor(&eng, "s2", s2, NULL);

        g_v1 = 100.0; g_v2 = 200.0; plato_tick(&eng);
        g_v1 = 101.0; g_v2 = 201.0; plato_tick(&eng);

        assert(history_get_test(&eng.history[i1], 0) == 101.0);
        assert(history_get_test(&eng.history[i2], 0) == 201.0);
        assert(history_get_test(&eng.history[i1], 1) == 100.0);
        assert(history_get_test(&eng.history[i2], 1) == 200.0);
    }
    PASS();

    printf("\n%d/%d tests passed.\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
