# Developer Guide — Plato Engine Block (C)

## Architecture Overview

The Plato Engine Block is a single-header C99 library implementing a sensor→history→alarm pipeline. It allocates zero bytes after initialization — all state lives in fixed-size arrays inside the `plato_engine_t` struct. The entire implementation lives behind `#define PLATO_ENGINE_IMPL` in `plato_engine.h`.

### Data Flow

```
Sensor callbacks → plato_tick() → history ring buffers → alarm evaluation → fire/cooldown cycle
                                        ↓
                              plato_handle_command() → text protocol responses
```

### Core Struct

`plato_engine_t` contains:
- **sensors[16]** — Callbacks + names. Each returns a `double`.
- **actuators[8]** — Write callbacks + last value.
- **alarms[16]** — Comparison rules bound to a sensor index.
- **history[16]** — One ring buffer per sensor (256 slots each).
- **subs** — Up to 32 subscriber handles (file descriptors).
- **tick_num** — Monotonically increasing tick counter.

All arrays are compile-time sized via `#define` constants that you override before inclusion.

### Module Walkthrough

#### Ring Buffer (`plato_history_t`)

Fixed-size circular buffer. `history_push()` overwrites oldest values. `history_get(n)` returns the n-th most recent value (O(1)). No dynamic allocation — the buffer lives inline in the engine struct.

#### Alarm Engine

Alarms have three states: **armed** → **firing** → **cooldown** → **armed**.

1. An armed alarm evaluates its comparison (`PLATO_GT`, `PLATO_LT`, etc.) against the sensor's current value.
2. If true, `firing = true`, `armed = false`, and a cooldown counter starts (default 10 ticks).
3. Each tick decrements cooldown. When it hits zero, the alarm re-arms.

This prevents alarm floods — a critical overtemp alarm fires once, then suppresses for 10 ticks before it can fire again.

#### Command Parser (`plato_handle_command`)

A text-based protocol handler that dispatches on the incoming command string:
- `tick` — Calls `plato_tick()` internally, formats sensor values.
- `history [N]` — Reads from ring buffers.
- `<actuator> <value>` — Finds actuator by name, calls write callback.
- `alarm list` — Iterates alarm descriptors.
- `subscribe`/`unsubscribe` — Manages the subscriber set.
- `help`, `quit` — Utility commands.

The parser is simple: trim whitespace, `strcmp` for exact matches, `sscanf` for actuator commands. Unknown commands return `err unknown command`.

#### TCP Server (`server.c`)

A standalone `poll()`-based TCP server that:
1. Accepts connections on a configurable port (default 7070).
2. Reads newline-delimited commands from each client.
3. Dispatches through `plato_handle_command()`.
4. Broadcasts tick results to all subscribers.

Not part of the header library — it's an integration example showing how to wrap the engine in a network service.

### Extension Points

#### Custom Sensor Backends

Implement `plato_sensor_fn`:

```c
double my_sensor(void *user_data) {
    MyHardware *hw = (MyHardware *)user_data;
    return read_adc(hw->channel);
}
```

Register with `plato_add_sensor(&eng, "my_sensor", my_sensor, &hw)`. The engine calls this function every tick. Keep it fast — it blocks the tick loop.

#### Custom Actuator Backends

Implement `plato_actuator_fn`:

```c
int my_actuator(const char *name, double value, void *user_data) {
    MyDriver *drv = (MyDriver *)user_data;
    return write_pwm(drv, (int)value);
}
```

Return 0 on success, non-zero on error. The engine doesn't enforce actuator ranges — that's your responsibility.

#### Scaling for Embedded

Override constants before including the header:

```c
#define PLATO_MAX_SENSORS     4
#define PLATO_MAX_HISTORY     32
#define PLATO_MAX_ALARMS      4
#define PLATO_MAX_ACTUATORS   2
#define PLATO_MAX_SUBSCRIBERS 0   // no TCP needed
#define PLATO_ENGINE_IMPL
#include "plato_engine.h"
```

With these settings, `sizeof(plato_engine_t)` drops to ~300 bytes.

### Testing Strategy

Tests live in `tests/`:
- **test_engine.c** — Core lifecycle: init, add sensors/actuators/alarms, tick, verify values.
- **test_history.c** — Ring buffer overflow, wraparound, read-by-index.
- **test_protocol.c** — Command parsing: all commands, edge cases, error handling.

Run with `make test`. Tests are standalone C programs using `assert()` — no framework needed.

### Contributing

1. **Keep it under 400 lines.** The size constraint is architectural. If you need more, split it into a separate file.
2. **No heap allocation.** Everything must work with `malloc` unavailable.
3. **C99 only.** No GCC extensions, no C11 features.
4. **Test your changes.** Add a test in `tests/` for any new behavior.
5. **Run `make test` before submitting.** All tests must pass on gcc and clang.
