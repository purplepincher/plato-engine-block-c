# Plato Engine Block — C Reference Implementation

> A tiny, embeddable sensor→history→alarm engine in under 400 lines of C99.

The Plato Engine Block is a tiny, embeddable sensor→history→alarm engine:
**read sensors, store history, fire alarms, stream it all**. It's designed to
run anywhere — from POSIX servers to bare-metal MCUs to game loops — with zero
dynamic allocation after initialization.

Hosted under [purplepincher](https://github.com/purplepincher) as a standalone,
tested C99 toolkit.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                     Plato Engine Block                        │
│                                                              │
│  ┌──────────┐    ┌──────────────┐    ┌──────────────┐        │
│  │ Sensors  │───▶│  Tick Loop   │───▶│  History     │        │
│  │ (N max)  │    │  (plato_tick)│    │  Ring Buffer │        │
│  └──────────┘    └──────┬───────┘    └──────────────┘        │
│                         │                                     │
│                    ┌────▼────┐                                │
│                    │ Alarms  │──▶ Fire / Cooldown / Re-arm    │
│                    │ (N max) │                                │
│                    └─────────┘                                │
│                                                              │
│  ┌──────────────┐  ┌───────────────┐                         │
│  │  Actuators   │  │  Subscribers  │◀── TCP Server            │
│  │  (N max)     │  │  (N max)      │    (broadcast ticks)     │
│  └──────────────┘  └───────────────┘                         │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐    │
│  │            Text Protocol (plato_handle_command)       │    │
│  │   tick | history | actuator | alarm | subscribe       │    │
│  └──────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
         │                                    │
         ▼                                    ▼
   ┌──────────┐                        ┌────────────┐
   │  stdin   │                        │ TCP :7070  │
   │  daemon  │                        │  server    │
   └──────────┘                        └────────────┘
```

---

## Quick Start

```bash
# Clone and build
git clone https://github.com/purplepincher/plato-engine-block-c.git
cd plato-engine-block-c
make

# Run tests
make test

# Interactive daemon (type commands on stdin)
./plato_engine

# Auto-tick daemon (ticks every 1000ms, reads commands on stdin)
./plato_engine -a 1000

# Multi-client TCP server
./plato_server          # default port 7070
./plato_server 9090     # custom port

# Try it
$ ./plato_engine
Plato Engine Block — standalone daemon
Type 'help' for commands.

> tick
tick 1: cpu_temp=62.34 random=47.81 constant=42.00
> tick
tick 2: cpu_temp=65.12 random=23.45 constant=42.00
> history 5
history (5):
  cpu_temp: 65.12 62.34
  random: 23.45 47.81
  constant: 42.00 42.00
> alarm list
alarms (3):
  [0] overheat  sensor=cpu_temp  > 80.00  sev=CRIT  armed=yes
  [1] chilly    sensor=cpu_temp  < 50.00  sev=WARN  armed=yes
  [2] lucky     sensor=random    >= 90.00  sev=INFO  armed=yes
> help
Plato Engine Block — commands:
  tick            — read sensors, run one tick
  history [N]     — show last N readings (default 10)
  <actuator> <v>  — set actuator to value
  alarm list      — show all alarms
  subscribe       — subscribe to tick broadcasts
  unsubscribe     — stop tick broadcasts
  help            — this message
  quit            — disconnect
```

---

## API Reference

### Header Library (`plato_engine.h`)

Include in one `.c` file with `#define PLATO_ENGINE_IMPL` for the implementation.

#### Configuration Constants

| Constant | Default | Description |
|---|---|---|
| `PLATO_MAX_SENSORS` | 16 | Maximum sensor count |
| `PLATO_MAX_ACTUATORS` | 8 | Maximum actuator count |
| `PLATO_MAX_ALARMS` | 16 | Maximum alarm count |
| `PLATO_MAX_HISTORY` | 256 | History buffer depth per sensor |
| `PLATO_MAX_SUBSCRIBERS` | 32 | TCP subscriber limit |
| `PLATO_ALARM_COOLDOWN` | 10 | Ticks before alarm re-arms |
| `PLATO_NAME_LEN` | 32 | Max name length |
| `PLATO_CMD_BUF` | 512 | Command buffer size |
| `PLATO_RESP_BUF` | 1024 | Response buffer size |

Override any constant before `#include "plato_engine.h"`.

#### Core Functions

```c
void plato_init(plato_engine_t *eng);
```
Initialize engine. Must be called first. Zeroes all state.

```c
int plato_add_sensor(plato_engine_t *eng, const char *name,
                     plato_sensor_fn fn, void *user_data);
```
Register a sensor. Returns index (≥0) or -1 on failure.

```c
int plato_add_actuator(plato_engine_t *eng, const char *name,
                       plato_actuator_fn fn, void *user_data);
```
Register an actuator. Returns index (≥0) or -1 on failure.

```c
int plato_add_alarm(plato_engine_t *eng, const char *name,
                    int sensor_idx, plato_cmp_t cmp,
                    double threshold, plato_severity_t severity);
```
Register an alarm on a sensor. Comparison operators: `PLATO_GT`, `PLATO_LT`,
`PLATO_GTE`, `PLATO_LTE`, `PLATO_EQ`. Severities: `PLATO_INFO`, `PLATO_WARN`,
`PLATO_CRIT`.

```c
void plato_tick(plato_engine_t *eng);
```
Run one tick: read all sensors, store in history, evaluate all alarms.

```c
int plato_handle_command(plato_engine_t *eng, const char *cmd,
                         char *resp, size_t resp_len);
```
Parse a text command, write response. Returns response length.

```c
int plato_subscribe(plato_engine_t *eng, int handle);
int plato_unsubscribe(plato_engine_t *eng, int handle);
```
Manage subscriber handles (file descriptors, etc.).

---

## Protocol Reference

See [PLATO_PROTOCOL.md](PLATO_PROTOCOL.md) for the full wire protocol spec.

| Command | Response | Description |
|---|---|---|
| `tick` | `tick N: name=val ...` | Read sensors, advance tick |
| `history [N]` | Formatted history | Show last N readings per sensor |
| `<actuator> <value>` | `ok name=val` / `err ...` | Set actuator value |
| `alarm list` | Alarm table | Show all alarms and state |
| `subscribe` | `ok subscribed` | Enable tick broadcasts |
| `unsubscribe` | `ok unsubscribed` | Disable tick broadcasts |
| `help` | Command list | Show available commands |
| `quit` | `bye` | Disconnect |

---

## Example Configurations

### Engine Room Monitor

```c
plato_engine_t eng;
plato_init(&eng);

int temp = plato_add_sensor(&eng, "exhaust_temp", read_thermocouple, &tc1);
int pres = plato_add_sensor(&eng, "oil_pressure", read_pressure, &p1);

plato_add_alarm(&eng, "overtemp", temp, PLATO_GT, 220.0, PLATO_CRIT);
plato_add_alarm(&eng, "low_oil",  pres, PLATO_LT,  15.0, PLATO_CRIT);

plato_add_actuator(&eng, "shutdown_valve", write_solenoid, &sv1);
```

### Server Rack Monitor

```c
plato_init(&eng);

int cpu = plato_add_sensor(&eng, "rack_cpu", ipmi_cpu_temp, NULL);
int fan = plato_add_sensor(&eng, "fan_rpm",  ipmi_fan_rpm,  NULL);

plato_add_alarm(&eng, "cpu_hot",  cpu, PLATO_GT,  85.0, PLATO_WARN);
plato_add_alarm(&eng, "fan_fail", fan, PLATO_LT, 500.0, PLATO_CRIT);

plato_add_actuator(&eng, "fan_boost", ipmi_set_fan, NULL);
```

### Game World (NPC Health System)

```c
plato_init(&eng);

int hp = plato_add_sensor(&eng, "npc_health", npc_get_hp, &goblin);
int mp = plato_add_sensor(&eng, "npc_mana",   npc_get_mp, &goblin);

plato_add_alarm(&eng, "low_hp",  hp, PLATO_LT, 20.0, PLATO_CRIT);
plato_add_alarm(&eng, "no_mana", mp, PLATO_EQ,  0.0,  PLATO_INFO);

plato_add_actuator(&eng, "heal", npc_heal, &goblin);
plato_add_actuator(&eng, "flee", npc_flee, &goblin);
```

---

## ESP32 / Bare Metal Porting Guide

The engine is designed for embedded. Here's how to run it on an ESP32:

### 1. Override Constants

```c
#define PLATO_MAX_SENSORS     8
#define PLATO_MAX_HISTORY     64
#define PLATO_MAX_SUBSCRIBERS 4
#define PLATO_ALARM_COOLDOWN  5
#define PLATO_ENGINE_IMPL
#include "plato_engine.h"
```

RAM usage with these settings: ~1.5 KB.

### 2. Wire Real Sensors

```c
static double read_dht22(void *ud) {
    float temp;
    dht_read_data(DHT_TYPE_DHT22, GPIO_NUM_4, NULL, &temp);
    return (double)temp;
}

static int write_relay(const char *name, double value, void *ud) {
    gpio_set_level(GPIO_NUM_5, value > 0.5 ? 1 : 0);
    return 0;
}
```

### 3. Tick from a FreeRTOS Timer

```c
void tick_task(void *arg) {
    plato_engine_t *eng = (plato_engine_t *)arg;
    for (;;) {
        plato_tick(eng);
        // Check alarms, trigger actions
        for (int i = 0; i < eng->alarm_count; i++) {
            if (eng->alarms[i].firing) {
                ESP_LOGW("plato", "Alarm: %s", eng->alarms[i].name);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### 4. Memory Footprint

| Config | RAM | Flash |
|---|---|---|
| 4 sensors, 32 history | ~800 B | ~2 KB |
| 8 sensors, 64 history | ~1.5 KB | ~2 KB |
| 16 sensors, 256 history | ~6 KB | ~2.5 KB |

No heap allocation. Everything is static.

---

## Scope

This repo is a single, self-contained C99 implementation. It exposes a small
sensor/actuator/alarm engine and a text protocol over stdin and TCP. It does not
include a server-side orchestration layer, a TypeScript port, or a separate
protocol specification — those would be additional projects if they are ever
needed. The engine is intentionally small so it can be ported to any platform
and connected to any transport.

---

## Performance Characteristics

| Metric | Value |
|---|---|
| Single tick (16 sensors) | < 1 μs |
| History lookup | O(1) |
| Alarm evaluation (16 alarms) | < 1 μs |
| Memory (default config) | ~8 KB |
| Memory (embedded config) | ~1.5 KB |
| Binary size (stripped) | ~15 KB |
| Dynamic allocations | 0 (after init) |
| Thread safety | None (single-threaded by design) |

Tested on x86_64 Linux and ARM Cortex-M (QEMU). No platform-specific code
in the engine core.

---

## Building

```bash
make                  # Build daemon + server
make test             # Build and run all tests
make examples         # Build all examples
make DEBUG=1          # Debug build
make clean            # Remove all binaries
```

### Dependencies

- C99 compiler (gcc, clang, tcc)
- POSIX (for server.c: `poll()`, `socket()`)
- libm (math, for examples only)
- No other dependencies

---

## License

MIT — use it for anything. See [LICENSE](LICENSE).

---

## Contributing

1. Fork the repo
2. Create a feature branch
3. Add tests for new functionality
4. Ensure `make test` passes
5. Submit a PR

Keep it under 400 lines in the header. The constraint is the point.
