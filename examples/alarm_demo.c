/*
 * examples/alarm_demo.c — Alarm triggering demo.
 *
 * Shows an alarm that fires when a value exceeds a threshold,
 * then respects cooldown before re-arming.
 *
 * Build:  cc -o alarm_demo examples/alarm_demo.c -Iinclude
 * Run:    ./alarm_demo
 */

#define PLATO_ENGINE_IMPL
#include "plato_engine.h"

#include <stdio.h>
#include <stdlib.h>

/* Stepped sensor: returns 50 for first 3 ticks, then 90, then 50... */
static double sensor_stepped(void *ud) {
    int *phase = (int *)ud;
    return (*phase < 3) ? 50.0 : 90.0;
}

int main(void) {
    plato_engine_t eng;
    plato_init(&eng);

    int phase = 0;
    int sidx = plato_add_sensor(&eng, "temp", sensor_stepped, &phase);
    plato_add_alarm(&eng, "overheat", sidx, PLATO_GT, 80.0, PLATO_CRIT);

    printf("Alarm Demo — stepping temperature past threshold\n\n");

    for (int i = 0; i < 15; i++) {
        eng.tick_num++;
        /* read sensor manually */
        eng.last_tick[0] = eng.sensors[0].read(eng.sensors[0].user_data);

        /* evaluate alarms */
        for (int a = 0; a < eng.alarm_count; a++) {
            plato_alarm_t *al = &eng.alarms[a];
            al->firing = false;
            if (al->cooldown > 0) {
                al->cooldown--;
                if (al->cooldown == 0) al->armed = true;
            }
            if (al->armed && cmp_eval(eng.last_tick[al->sensor_idx],
                                      al->cmp, al->threshold)) {
                al->firing = true;
                al->armed  = false;
                al->cooldown = PLATO_ALARM_COOLDOWN;
            }
        }

        printf("tick %2llu  temp=%.0f  alarm=%s  armed=%s  cooldown=%d\n",
               (unsigned long long)eng.tick_num,
               eng.last_tick[0],
               eng.alarms[0].firing ? "FIRE!" : "ok",
               eng.alarms[0].armed ? "yes" : "no",
               eng.alarms[0].cooldown);

        phase++;
        if (phase >= 6) phase = 0;
    }

    printf("\nDone.\n");
    return 0;
}
