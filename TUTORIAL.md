# Tutorial — Building a Temperature Monitor with Plato Engine Block

This tutorial walks you through building a complete temperature monitoring system from scratch. You'll create a program that reads temperature sensors, tracks history, fires alarms, and responds to commands.

**Prerequisites:** A C compiler (gcc or clang), make, and a terminal.

## Step 1: Create the Project

```bash
mkdir temp_monitor && cd temp_monitor
# Copy the single header into your project
cp /path/to/plato-engine-block-c/include/plato_engine.h .
```

## Step 2: Minimal Engine Setup

Create `main.c`:

```c
#define PLATO_ENGINE_IMPL
#include "plato_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// A fake temperature sensor — in real code, read from hardware
static double read_temperature(void *user_data) {
    (void)user_data;
    // Simulate: 60°C base + random fluctuation
    return 60.0 + (rand() % 200) / 10.0;
}

// An actuator that controls a cooling fan
static int set_fan(const char *name, double value, void *user_data) {
    (void)user_data;
    printf("[ACTUATOR] %s set to %.0f%%\n", name, value);
    return 0;
}

int main(void) {
    srand((unsigned)time(NULL));

    // 1. Initialize the engine
    plato_engine_t eng;
    plato_init(&eng);

    // 2. Register sensors
    int temp_sensor = plato_add_sensor(&eng, "cpu_temp", read_temperature, NULL);
    if (temp_sensor < 0) {
        fprintf(stderr, "Failed to add sensor\n");
        return 1;
    }

    // 3. Register actuators
    int fan = plato_add_actuator(&eng, "fan_speed", set_fan, NULL);
    if (fan < 0) {
        fprintf(stderr, "Failed to add actuator\n");
        return 1;
    }

    // 4. Set up alarms
    //    Overheat: temperature > 75°C, critical severity
    plato_add_alarm(&eng, "overheat", temp_sensor,
                    PLATO_GT, 75.0, PLATO_CRIT);
    //    Chilly: temperature < 62°C, warning severity
    plato_add_alarm(&eng, "chilly", temp_sensor,
                    PLATO_LT, 62.0, PLATO_WARN);

    // 5. Run ticks
    printf("Running 20 ticks...\n\n");
    char resp[1024];
    for (int i = 0; i < 20; i++) {
        plato_handle_command(&eng, "tick", resp, sizeof(resp));
        printf("%s\n", resp);
    }

    // 6. Show history
    printf("\n--- History (last 5 readings) ---\n");
    plato_handle_command(&eng, "history 5", resp, sizeof(resp));
    printf("%s", resp);

    // 7. Show alarm states
    printf("\n--- Alarm States ---\n");
    plato_handle_command(&eng, "alarm list", resp, sizeof(resp));
    printf("%s", resp);

    return 0;
}
```

## Step 3: Build and Run

```bash
gcc -o temp_monitor main.c -lm
./temp_monitor
```

You'll see output like:

```
Running 20 ticks...

tick 1: cpu_temp=68.40
tick 2: cpu_temp=72.30
...
tick 8: cpu_temp=76.50
! ALARM overheat [CRIT] > 75.00
...

--- History (last 5 readings) ---
history (5):
  cpu_temp: 74.20 69.50 76.50 65.30 72.10

--- Alarm States ---
  [0] overheat  sensor=cpu_temp  > 75.00  sev=CRIT  armed=no
  [1] chilly    sensor=cpu_temp  < 62.00  sev=WARN  armed=yes
```

## Step 4: Add Interactive Commands

Let's make it accept commands from stdin:

```c
// Replace the for loop in main() with:
char cmd[512];
char resp[1024];

printf("Plato Temp Monitor — type 'help' for commands\n");

while (fgets(cmd, sizeof(cmd), stdin)) {
    // Check for quit
    if (strncmp(cmd, "quit", 4) == 0) break;

    // Handle command
    int len = plato_handle_command(&eng, cmd, resp, sizeof(resp));
    if (len > 0) {
        printf("%s\n", resp);
    }
}
```

Now you can interact live:

```
> tick
tick 1: cpu_temp=64.20
> tick
tick 2: cpu_temp=78.90
! ALARM overheat [CRIT] > 75.00
> fan_speed 80
ok fan_speed=80.00
[ACTUATOR] fan_speed set to 80%
> history 5
history (5):
  cpu_temp: 78.90 64.20
> alarm list
  [0] overheat  sensor=cpu_temp  > 75.00  sev=CRIT  armed=no
  [1] chilly    sensor=cpu_temp  < 62.00  sev=WARN  armed=yes
```

## Step 5: Add a Second Sensor

```c
// Add humidity sensor
static double read_humidity(void *user_data) {
    (void)user_data;
    return 40.0 + (rand() % 300) / 10.0;
}

// In main(), after the temperature sensor:
int humid_sensor = plato_add_sensor(&eng, "humidity", read_humidity, NULL);
plato_add_alarm(&eng, "high_humidity", humid_sensor,
                PLATO_GTE, 60.0, PLATO_WARN);
```

Now each tick reports both sensors:

```
tick 5: cpu_temp=71.30 humidity=55.20
```

## Step 6: Auto-Tick Mode

For unattended operation, tick on a timer:

```c
#include <unistd.h>

// In main():
printf("Auto-ticking every 1 second. Press Ctrl+C to stop.\n");
char resp[1024];
while (1) {
    plato_handle_command(&eng, "tick", resp, sizeof(resp));
    printf("%s\n", resp);
    sleep(1);
}
```

## What You Built

- ✅ Multi-sensor monitoring engine
- ✅ Ring buffer history with O(1) lookups
- ✅ Alarm system with cooldown deduplication
- ✅ Text command protocol
- ✅ Actuator control
- ✅ Zero heap allocation after init

## Next Steps

- **Connect real sensors** — Replace the dummy callbacks with ADC/GPIO reads
- **Run the TCP server** — Use `server.c` as a starting point for network access
- **Go embedded** — Shrink the constants and run on an ESP32 or STM32
- **Read the protocol spec** — See `PLATO_PROTOCOL.md` for the full wire format
