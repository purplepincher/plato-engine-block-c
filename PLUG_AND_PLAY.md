# Plug & Play — Plato Engine Block (C)

Copy-paste templates for the three most common patterns.

## Pattern 1: Minimal Engine (Sensors + Alarms)

```c
#define PLATO_ENGINE_IMPL
#include "plato_engine.h"
#include <stdio.h>

// -- Your sensor callbacks --
static double read_temp(void *ud) { (void)ud; return 72.5; /* your code */ }

int main(void) {
    plato_engine_t eng;
    plato_init(&eng);

    int s = plato_add_sensor(&eng, "temp", read_temp, NULL);
    plato_add_alarm(&eng, "overheat", s, PLATO_GT, 80.0, PLATO_CRIT);

    char resp[1024];
    plato_handle_command(&eng, "tick", resp, sizeof(resp));
    printf("%s\n", resp);

    plato_handle_command(&eng, "alarm list", resp, sizeof(resp));
    printf("%s", resp);

    return 0;
}
```

Build: `gcc -o demo main.c -lm`

## Pattern 2: Interactive Command Daemon

```c
#define PLATO_ENGINE_IMPL
#include "plato_engine.h"
#include <stdio.h>

static double read_temp(void *ud) { (void)ud; return 65.0; }
static int set_fan(const char *name, double val, void *ud) {
    (void)ud; printf("[HW] %s = %.0f\n", name, val); return 0;
}

int main(void) {
    plato_engine_t eng;
    plato_init(&eng);

    int temp = plato_add_sensor(&eng, "temp", read_temp, NULL);
    plato_add_actuator(&eng, "fan", set_fan, NULL);
    plato_add_alarm(&eng, "hot", temp, PLATO_GT, 80.0, PLATO_CRIT);

    char cmd[512], resp[1024];
    printf("Plato Engine — type 'help' for commands\n");
    while (fgets(cmd, sizeof(cmd), stdin)) {
        if (strncmp(cmd, "quit", 4) == 0) break;
        plato_handle_command(&eng, cmd, resp, sizeof(resp));
        printf("%s\n", resp);
    }
    return 0;
}
```

Commands: `tick`, `history [N]`, `fan 75`, `alarm list`, `help`, `quit`

## Pattern 3: TCP Server (Multi-Client)

```bash
# Build and run
make
./plato_server          # port 7070
./plato_server 9090     # custom port

# From another terminal
echo "tick" | nc localhost 7070
echo "history 10" | nc localhost 7070
echo "subscribe" | nc localhost 7070  # gets live broadcasts
```

Wire custom sensors by editing `src/sensors_dummy.c` to call your real hardware. Register them in `src/main.c` before the main loop.

## Quick Reference

| What | Code |
|------|------|
| Init engine | `plato_engine_t eng; plato_init(&eng);` |
| Add sensor | `int idx = plato_add_sensor(&eng, "name", callback, user_data);` |
| Add actuator | `plato_add_actuator(&eng, "name", callback, user_data);` |
| Add alarm | `plato_add_alarm(&eng, "name", sensor_idx, PLATO_GT, threshold, PLATO_CRIT);` |
| Run one tick | `plato_tick(&eng);` |
| Handle command | `plato_handle_command(&eng, "tick", resp, sizeof(resp));` |
| Check alarm | `eng.alarms[i].firing` after tick |
| Read history | `plato_handle_command(&eng, "history 10", resp, sizeof(resp));` |
| Shrink for MCU | Override `PLATO_MAX_*` constants before `#include` |
