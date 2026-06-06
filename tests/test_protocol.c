/*
 * tests/test_protocol.c — Protocol parsing tests.
 *
 * Build:  cc -o test_protocol tests/test_protocol.c -Iinclude
 * Run:    ./test_protocol
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

static double sensor_42(void *ud) { (void)ud; return 42.0; }

int main(void) {
    printf("=== Protocol Tests ===\n\n");

    plato_engine_t eng;
    plato_init(&eng);
    plato_add_sensor(&eng, "temp", sensor_42, NULL);

    char resp[PLATO_RESP_BUF];

    /* ---- tick command ---- */
    TEST("tick command returns sensor readings");
    {
        int n = plato_handle_command(&eng, "tick", resp, sizeof(resp));
        assert(n > 0);
        assert(strncmp(resp, "tick ", 5) == 0);
        assert(strstr(resp, "temp=42.00") != NULL);
    }
    PASS();

    TEST("tick command advances tick counter");
    {
        uint64_t before = eng.tick_num;
        plato_handle_command(&eng, "tick", resp, sizeof(resp));
        assert(eng.tick_num == before + 1);
    }
    PASS();

    /* ---- history command ---- */
    TEST("history returns readings");
    {
        plato_handle_command(&eng, "tick", resp, sizeof(resp));
        plato_handle_command(&eng, "tick", resp, sizeof(resp));
        int n = plato_handle_command(&eng, "history 5", resp, sizeof(resp));
        assert(n > 0);
        assert(strncmp(resp, "history", 7) == 0);
        assert(strstr(resp, "temp:") != NULL);
    }
    PASS();

    TEST("history default shows 10 entries");
    {
        int n = plato_handle_command(&eng, "history", resp, sizeof(resp));
        assert(n > 0);
        assert(strncmp(resp, "history (10)", 12) == 0);
    }
    PASS();

    /* ---- actuator command ---- */
    TEST("unknown actuator returns error");
    {
        int n = plato_handle_command(&eng, "nonexistent 5.0", resp, sizeof(resp));
        assert(strncmp(resp, "err", 3) == 0);
    }
    PASS();

    TEST("actuator set returns ok");
    {
        plato_add_actuator(&eng, "fan", NULL, NULL);
        int n = plato_handle_command(&eng, "fan 75.0", resp, sizeof(resp));
        assert(strncmp(resp, "ok fan=75.00", 12) == 0);
    }
    PASS();

    /* ---- alarm list ---- */
    TEST("alarm list shows alarms");
    {
        int n = plato_handle_command(&eng, "alarm list", resp, sizeof(resp));
        assert(n > 0);
        assert(strncmp(resp, "alarms", 6) == 0);
    }
    PASS();

    /* ---- subscribe/unsubscribe ---- */
    TEST("subscribe command returns ok");
    {
        int n = plato_handle_command(&eng, "subscribe", resp, sizeof(resp));
        assert(strcmp(resp, "ok subscribed") == 0);
    }
    PASS();

    TEST("unsubscribe command returns ok");
    {
        int n = plato_handle_command(&eng, "unsubscribe", resp, sizeof(resp));
        assert(strcmp(resp, "ok unsubscribed") == 0);
    }
    PASS();

    /* ---- help ---- */
    TEST("help command lists all commands");
    {
        int n = plato_handle_command(&eng, "help", resp, sizeof(resp));
        assert(n > 0);
        assert(strstr(resp, "tick") != NULL);
        assert(strstr(resp, "history") != NULL);
        assert(strstr(resp, "help") != NULL);
        assert(strstr(resp, "quit") != NULL);
        assert(strstr(resp, "subscribe") != NULL);
        assert(strstr(resp, "alarm list") != NULL);
    }
    PASS();

    /* ---- quit ---- */
    TEST("quit command returns bye");
    {
        int n = plato_handle_command(&eng, "quit", resp, sizeof(resp));
        assert(strcmp(resp, "bye") == 0);
    }
    PASS();

    /* ---- unknown command ---- */
    TEST("unknown command returns error");
    {
        int n = plato_handle_command(&eng, "foobar", resp, sizeof(resp));
        assert(strncmp(resp, "err", 3) == 0);
    }
    PASS();

    /* ---- whitespace handling ---- */
    TEST("leading whitespace is trimmed");
    {
        int n = plato_handle_command(&eng, "   tick", resp, sizeof(resp));
        assert(strncmp(resp, "tick ", 5) == 0);
    }
    PASS();

    TEST("trailing whitespace handled");
    {
        int n = plato_handle_command(&eng, "tick  ", resp, sizeof(resp));
        assert(strncmp(resp, "tick ", 5) == 0);
    }
    PASS();

    printf("\n%d/%d tests passed.\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
