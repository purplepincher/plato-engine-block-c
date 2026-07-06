# plato-engine-block-c

Single-header C99 sensor→history→alarm engine: read sensors, store time-series history, evaluate threshold and symmetry alarms, and expose a line-delimited command protocol over stdin or TCP.

## Quickstart

```bash
git clone https://github.com/purplepincher/plato-engine-block-c.git
cd plato-engine-block-c
make
make test
```

Run the interactive daemon:

```bash
./plato_engine
```

Or the TCP server:

```bash
./plato_server          # default port 7070
./plato_server 9090     # custom port
```

Auto-tick mode (one tick every `N` milliseconds):

```bash
./plato_engine -a 1000
```

## Usage

Interactive session via stdin:

```text
$ ./plato_engine
Plato Engine Block — standalone daemon
Sensors: 3  Actuators: 2  Alarms: 3
Auto-tick: off
Type 'help' for commands.

> tick
tick 1: cpu_temp=65.64 random=65.99 constant=42.00
> tick
tick 2: cpu_temp=65.11 random=91.48 constant=42.00
! ALARM lucky [INFO] >= 90.00
> history 5
history (5):
  cpu_temp: 65.11 65.64
  random: 91.48 65.99
  constant: 42.00 42.00
> fan_speed 75.5
ok fan_speed=75.50
> alarm list
alarms (3):
  [0] overheat  sensor=cpu_temp  > 80.00  sev=CRIT  armed=yes
  [1] chilly  sensor=cpu_temp  < 50.00  sev=WARN  armed=yes
  [2] lucky  sensor=random  >= 90.00  sev=INFO  armed=no
> quit
bye

Shutting down. 2 ticks served.
```

TCP session on port 7070:

```text
$ nc localhost 7070
Plato Engine Block — type 'help' for commands
> tick
tick 1: cpu_temp=63.47 random=11.58 constant=42.00
> alarm list
alarms (3):
  [0] overheat  sensor=cpu_temp  > 80.00  sev=CRIT  armed=yes
  [1] chilly  sensor=cpu_temp  < 50.00  sev=WARN  armed=yes
  [2] lucky  sensor=random  >= 90.00  sev=INFO  armed=yes
> quit
bye
```

## How it works

1. **Sensors** — Callbacks (`plato_sensor_fn`) produce a `double` each tick. The demo binary registers `cpu_temp`, `random`, and `constant` sensors.
2. **History** — Every tick, each sensor value is pushed into a per-sensor ring buffer (`PLATO_MAX_HISTORY` samples).
3. **Alarms** — Each alarm compares a sensor value against a threshold using `>`, `<`, `>=`, `<=`, or `==`. When the condition is met, the alarm fires for one tick and then enters a cooldown (`PLATO_ALARM_COOLDOWN`) before re-arming.
4. **Symmetry pairs** — Pairs of sensors can be monitored for cross-correlation over a sliding window. A symmetry alarm fires when correlation drops below its threshold.
5. **Veto** — An alarm with severity `PLATO_VETO` blocks all actuator writes while it is firing.
6. **Protocol** — `plato_handle_command()` parses text commands and writes text responses. `plato_server` wraps this in a TCP server with tick broadcasts to subscribed clients.

No dynamic allocation occurs after `plato_init()`; all state lives in the caller-allocated `plato_engine_t` struct.

## Configuration and options

Include the header in one translation unit with the implementation macro:

```c
#define PLATO_ENGINE_IMPL
#include "plato_engine.h"
```

Override constants before the include:

| Constant | Default | Description |
|---|---|---|
| `PLATO_MAX_SENSORS` | 16 | Maximum sensor count |
| `PLATO_MAX_ACTUATORS` | 8 | Maximum actuator count |
| `PLATO_MAX_ALARMS` | 16 | Maximum alarm count |
| `PLATO_MAX_HISTORY` | 256 | Samples per sensor history ring buffer |
| `PLATO_MAX_SUBSCRIBERS` | 32 | TCP subscriber slots |
| `PLATO_ALARM_COOLDOWN` | 10 | Ticks before a fired alarm re-arms |
| `PLATO_NAME_LEN` | 32 | Maximum name length |
| `PLATO_CMD_BUF` | 512 | Command buffer size |
| `PLATO_RESP_BUF` | 1024 | Response buffer size |

Ternary/symmetry constants:

| Constant | Default | Description |
|---|---|---|
| `PLATO_LEMINAL_LOW` | 0.3 | Conviction below this maps to trit `-1` |
| `PLATO_LEMINAL_HIGH` | 0.7 | Conviction above this maps to trit `+1` |
| `PLATO_SYMMETRY_WINDOW` | 16 | Samples used for cross-correlation |
| `PLATO_SYMMETRY_THRESHOLD` | 0.75 | Minimum correlation to consider a pair symmetric |
| `PLATO_MAX_SYMMETRY_PAIRS` | 8 | Maximum symmetry pairs |

### Core API

```c
void plato_init(plato_engine_t *eng);
int  plato_add_sensor(plato_engine_t *eng, const char *name,
                      plato_sensor_fn fn, void *user_data);
int  plato_add_actuator(plato_engine_t *eng, const char *name,
                        plato_actuator_fn fn, void *user_data);
int  plato_add_alarm(plato_engine_t *eng, const char *name,
                     int sensor_idx, plato_cmp_t cmp,
                     double threshold, plato_severity_t severity);
int  plato_add_symmetry_alarm(plato_engine_t *eng, const char *name,
                              int sensor_a, int sensor_b,
                              double min_correlation, plato_severity_t severity);
int  plato_add_symmetry_pair(plato_engine_t *eng, const char *name,
                             int sensor_a, int sensor_b, double threshold);
void plato_tick(plato_engine_t *eng);
int  plato_handle_command(plato_engine_t *eng, const char *cmd,
                          char *resp, size_t resp_len);
```

Comparison operators: `PLATO_GT`, `PLATO_LT`, `PLATO_GTE`, `PLATO_LTE`, `PLATO_EQ`.  
Severity levels: `PLATO_INFO`, `PLATO_WARN`, `PLATO_CRIT`, `PLATO_VETO`.

### Build targets

```bash
make                  # Build plato_engine and plato_server
make test             # Build and run all tests
make examples         # Build all examples
make DEBUG=1          # Debug build
make clean            # Remove binaries
```

## Limitations

- **Single-threaded.** The engine has no internal locking; concurrent access must be serialized by the host.
- **Text protocol only.** No binary framing or encryption; see [`PLATO_PROTOCOL.md`](./PLATO_PROTOCOL.md) for the wire format.
- **TCP server is POSIX-only.** `plato_server` uses `poll()`, `socket()`, and `inet_ntop()`. The header library itself is plain C99.
- **Simplified sensor model.** The demo uses simulated sensors; real sensors require wiring your own `plato_sensor_fn` callbacks.
- **No persistence.** History and state live in memory only; use the host to save/restore if needed.

## Documentation

- [`PLATO_PROTOCOL.md`](./PLATO_PROTOCOL.md) — full command protocol
- [`DEVELOPER_GUIDE.md`](./DEVELOPER_GUIDE.md) — integration guide
- [`TUTORIAL.md`](./TUTORIAL.md) — worked examples
- [`TernARY_UPGRADE.md`](./TernARY_UPGRADE.md) — ternary logic and veto notes

## License

MIT. See [`LICENSE`](./LICENSE).
