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

No dynamic allocation occurs after `plato_init()`; all state lives in the caller-allocated `plato_engine_t` struct.

## Status & capabilities

Everything below was verified by building the library (`make`), running the
test suite (`make test` — **35/35 tests pass**: 14 engine, 14 protocol, 7
history), and driving both `plato_engine` and `plato_server` over stdin/TCP.

- ✅ **Single-header C99 library** (`include/plato_engine.h`) — compile into
  one `.c` file via `#define PLATO_ENGINE_IMPL`. No heap allocation after init.
- ✅ **Sensor → history → alarm pipeline:** `plato_tick()` reads all sensors,
  pushes into per-sensor ring buffers, evaluates threshold alarms, and checks
  symmetry pairs.
- ✅ **Threshold alarms** with `>`, `<`, `>=`, `<=`, `==`, a fire-then-cooldown
  lifecycle (`PLATO_ALARM_COOLDOWN`), and `INFO`/`WARN`/`CRIT` severities.
- ✅ **Symmetry monitoring:** cross-correlation between sensor pairs
  (`plato_add_symmetry_pair`) and symmetry *alarms* that fire when two sensors
  decouple (`plato_add_symmetry_alarm`).
- ✅ **Veto system:** a `PLATO_VETO`-severity alarm blocks **all** actuator
  writes while it is firing (verified in `plato_engine.h` and the symmetry demo).
- ✅ **Ternary-Continuous shim:** conviction `[0,1]` ↔ trit `{-1,0,+1}` via a
  Leminal-zone deadband (`plato_conviction_to_trit`).
- ✅ **Text protocol:** `plato_handle_command()` parses line-delimited commands
  (`tick`, `history`, `<actuator> <value>`, `alarm list`, `symmetry list`,
  `veto`, `subscribe`, `unsubscribe`, `help`, `quit`).
- ✅ **Two demo binaries:** `plato_engine` (stdin daemon, `-a N` auto-tick) and
  `plato_server` (POSIX `poll()`-based TCP server, default port 7070).
- ✅ **Examples** in `examples/`: `minimal`, `alarm_demo`, `multi_sensor`,
  `symmetry_demo`, `client`.

### What it does NOT do (yet)

- 🔮 **No persistence.** History, alarms, and engine state are in-RAM only;
  there is no save/restore or journaling. `memory/JOURNAL.md` is an agent
  notebook, not a runtime data store.
- 🔮 **No thread safety.** Single-threaded by design; the host must serialize
  concurrent access (e.g. lock around `plato_tick`/`plato_handle_command`).
- 🔮 **No binary protocol, encryption, or auth.** The wire format is plaintext
  UTF-8 lines — fine for a trusted/serial link, not for an untrusted network.
- 🔮 **No real sensor drivers.** `src/sensors_dummy.c` ships simulated sensors
  (`cpu_temp`, `random`, `constant`); you must wire your own `plato_sensor_fn`.
- 🔮 **No actuator range enforcement.** The engine calls your write callback with
  whatever value it is given — clamping/limits are the host's responsibility.
- 🔮 **TCP server is POSIX-only** (`poll`/`socket`/`inet_ntop`); the header
  library itself is portable C99, but `plato_server` won't build on bare metal
  or non-POSIX hosts.

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
