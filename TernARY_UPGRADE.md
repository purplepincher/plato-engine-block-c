# ⚡ TernARY Upgrade — Plato Engine Block v1.1

> **Cross-pollination:** Embedding the Ternary-Continuous Hybrid Manifold,
> SAEP Veto governance, and Topological Symmetry detection into a C99
> embedded sensor engine.

---

## Overview

The Plato Engine Block started as a simple sensor → history → alarm pipeline.
This upgrade transforms it into a **Ternary-Symmetry Manifold** that can reason
about sensor relationships in ternary logic, enforce safety constraints via
veto overrides, and detect when coupled sensors decouple (symmetry breaks).

### What's New

| Feature | Concept | Immplementation |
|---|---|---|
| **Ternary-Continuous Shim** | Conviction [0,1] ↔ Trit {-1,0,+1} | `plato_conviction_t`, `plato_trit_t`, Leminal Zone deadband (0.3–0.7) |
| **Veto System** | SAEP 4-tier hierarchy; VETO overrides actuator writes | `PLATO_VETO` severity, `veto_active` flag, write blocking |
| **Symmetry Check** | Topological identity via cross-correlation | `plato_symmetry_pair_t`, `plato_check_symmetry()`, symmetry alarms |

---

## 1. Ternary-Continuous Shim

### The Concept

Every sensor reading can be expressed as a **conviction** — a value in [0.0, 1.0]
reflecting how "strongly" a sensor believes its reading is significant. This
continuous value maps to a **trit** (ternary digit) through the Leminal Zone:

```
Conviction:    0.0 ─────── 0.3 ─────── 0.7 ─────── 1.0
                          │           │
             PLATO_TRIT_NEG │ PLATO_TRIT_ZERO │ PLATO_TRIT_POS
                    (-1)   │      (0)        │     (+1)
                           │                 │
                      Leminal Zone     Leminal Zone
                      (deadband)       (deadband)
```

### API

```c
// Convert conviction [0,1] to a trit
plato_trit_t trit = plato_conviction_to_trit(conviction);

// Convert trit back to centre-of-zone conviction
plato_conviction_t c = plato_trit_to_conviction(trit);

// Normalise a raw sensor value into [0,1] given its expected range
plato_conviction_t c = plato_normalise_conviction(raw, min, max);

// Is the value in the deadband?
bool in_leminal = plato_is_lemminal(c);
```

### Constants

| Constant | Default | Description |
|---|---|---|
| `PLATO_LEMINAL_LOW` | 0.3 | Below this → trit NEG |
| `PLATO_LEMINAL_HIGH` | 0.7 | Above this → trit POS |

Override before `#include "plato_engine.h"` to tune the deadband.

### Example

```c
double temp = 72.5;  // sensor reading
plato_conviction_t c = plato_normalise_conviction(temp, 40.0, 100.0);
plato_trit_t t = plato_conviction_to_trit(c);
// c ≈ 0.542  →  t = ZERO(0)  (in Leminal Zone)
```

---

## 2. Veto System

### The Concept

Alarms aren't just notifications. With `PLATO_VETO` severity, an alarm becomes
a **safety constraint** that overrides all actuator writes when it fires.

The severity hierarchy is:

```
INFO  (0)  →  WARN (1)  →  CRIT (2)  →  VETO (3)
                                           ⛔ BLOCKS ACTUATORS
```

When a VETO alarm fires during a tick:
1. `eng->veto_active` is set to `true`
2. `eng->veto_source_name` records the alarm name
3. Any subsequent actuator write command returns `"⛔ VETO BLOCKED — ..."`
4. Veto resets at the start of the next tick

### API

```c
// Create a veto alarm (severity = PLATO_VETO)
int a = plato_add_alarm(&eng, "safety_override", sensor_idx,
                        PLATO_GT, 80.0, PLATO_VETO);

// Query after tick
if (plato_veto_active(&eng)) {
    printf("Veto by: %s\n", plato_veto_source(&eng));
}
```

### Veto + Actuator Flow

```
  Tick begins → reset veto
  Read sensors → evaluate alarms
    ↓
  VETO alarm fires?
    ├── No  → actuators writable
    └── Yes → veto_active = true
              actuator writes return "⛔ VETO BLOCKED"
```

### Commands

```
> veto
✅ No veto active

> veto
⛔ VETO active — source: 'drift_veto'
```

---

## 3. Symmetry Check

### The Concept

When two sensors are expected to behave similarly (e.g., two temperature sensors
in the same rack), the engine computes a **normalised cross-correlation** over a
sliding window of their histories.

If the correlation drops below a threshold, symmetry is "broken."

### API

```c
// Register a symmetry pair for passive monitoring
int p = plato_add_symmetry_pair(&eng, "rack-temp-pair",
                                 sensor_a, sensor_b, 0.75);

// Register a symmetry alarm (fires on break)
int a = plato_add_symmetry_alarm(&eng, "temp_mismatch",
                                  sensor_a, sensor_b,
                                  0.75, PLATO_WARN);

// Manually check a pair
double corr = plato_check_symmetry(&eng, pair_idx);
```

### Constants

| Constant | Default | Description |
|---|---|---|
| `PLATO_SYMMETRY_WINDOW` | 16 | History window for correlation |
| `PLATO_SYMMETRY_THRESHOLD` | 0.75 | Default min correlation |
| `PLATO_MAX_SYMMETRY_PAIRS` | 8 | Max monitored pairs |

### Commands

```
> symmetry list
symmetry pairs (1):
  [0] sine-drift  sine ↔ drift  r=0.32  thresh=0.75  ✗
```

### Cross-Correlation Formula

Given two history windows `A` and `B` of length `n`:

```
r = Σ(Ai - μA)(Bi - μB) / √(Σ(Ai - μA)²) · √(Σ(Bi - μB)²)
```

Values are clamped to [0, 1] for interpretability. A score of 1.0 means
perfectly symmetric; 0.0 means completely decorrelated.

---

## Command Reference — New Commands

| Command | Description |
|---|---|
| `symmetry list` | Show all symmetry pairs and their current correlation |
| `veto` | Show current veto status |

`tick` responses now also include symmetry pair state and veto status.

---

## Compatibility

✅ **Backward compatible** — all existing code compiles and runs unchanged.
Existing alarms continue to work as before.

| Old | New |
|---|---|
| `plato_init()` | Same |
| `plato_add_sensor()` | Same |
| `plato_add_actuator()` | Same |
| `plato_add_alarm()` | Same (new `plato_alarm_t` has extra fields, zeroed) |
| `plato_tick()` | Same (now also checks symmetry + veto) |
| `plato_handle_command()` | Same (new `symmetry list`, `veto` commands added) |

---

## Migration Guide

### If you only use standard alarms: **zero changes.**

The engine works exactly as before. Symmetry pairs and veto are opt-in.

### To add symmetry monitoring:

```c
int s1 = plato_add_sensor(&eng, "rack_left",  read_temp, &left);
int s2 = plato_add_sensor(&eng, "rack_right", read_temp, &right);

// Passive monitoring (no alarm)
plato_add_symmetry_pair(&eng, "rack-pair", s1, s2, 0.80);

// Active alarm on break
plato_add_symmetry_alarm(&eng, "rack_mismatch", s1, s2, 0.80, PLATO_WARN);
```

### To add veto constraints:

```c
// Use PLATO_VETO as severity
plato_add_alarm(&eng, "critical_overtemp", temp_sensor,
                PLATO_GT, 85.0, PLATO_VETO);

// In your actuator write callback, optionally check veto:
if (!plato_veto_active(&eng)) {
    write_hardware(name, value);
}
```

---

## Resource Impact

| Feature | RAM (additional) | Flash (additional) |
|---|---|---|
| Ternary-Shim (inline) | 0 bytes (static inline) | ~200 bytes |
| Veto state | 36 bytes (1 bool + 32 char name + 1 int) | ~100 bytes |
| Symmetry (8 pairs, 16-win) | ~500 bytes (pairs + scratch) | ~600 bytes |

Total impact: **~1 KB RAM, ~1 KB flash** over baseline.

---

## Theory

### The Ternary-Continuous Manifold

The Leminal Zone (0.3–0.7) creates a **deadband** where small fluctuations
don't flip the trit. This is the continuous-to-discrete interface:

```
Trit -1  →  "Too low"     (conviction < 0.3)
Trit  0  →  "Nominal"     (conviction 0.3–0.7)  ← Leminal buffer
Trit +1  →  "Too high"    (conviction > 0.7)
```

### Symmetry as Topological Invariant

Cross-correlation between sensor histories is a 1-dimensional proxy for
topological identity. Two sensors that track each other with high correlation
are "symmetric" — their Betti-0 signature (connected components) is the same.
When symmetry breaks, it signals a structural change in the system.

### Veto as SAEP Constraint Layer

The `PLATO_VETO` severity implements the lowest tier of the SAEP hierarchy
(Room → Sector → Portfolio → Market). At the embedded level, veto blocks
actuator writes — enforcing a physical safety constraint before the signal
reaches the higher governance layers.

---

## File Manifest

| File | Description |
|---|---|
| `include/plato_engine.h` | Core engine with TernARY upgrade |
| `examples/symmetry_demo.c` | Complete demo of all three features |
| `TernARY_UPGRADE.md` | This document |

---

## References

- [FLEET-POLLINATION-MAP.md](../market-manifold/FLEET-POLLINATION-MAP.md) — Cross-pollination strategy
- [PLATO_PROTOCOL.md](PLATO_PROTOCOL.md) — Wire protocol specification
- [README.md](README.md) — Original documentation
