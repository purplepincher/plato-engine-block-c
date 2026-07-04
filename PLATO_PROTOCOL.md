# Plato Wire Protocol Specification

> Version 1.1 — July 2026

The Plato Engine Block communicates over text-based, line-delimited protocol.
Designed for human readability, easy parsing, and telnet-friendliness.

## Transport

- **Default port:** 7070 (TCP)
- **Encoding:** UTF-8, newline-delimited (`\n` or `\r\n`)
- **Direction:** Client → Server (commands), Server → Client (responses + broadcasts)

## Connection Lifecycle

```
Client                          Server
  │                               │
  │──── TCP Connect ────────────▶│
  │◀─── Welcome banner ─────────│  "Plato Engine Block — type 'help' for commands"
  │                               │
  │──── Command ────────────────▶│
  │◀─── Response ───────────────│
  │       ...                     │
  │──── "quit" ─────────────────▶│
  │◀─── "bye" ──────────────────│
  │──── TCP Close ──────────────▶│
```

On connect the server sends exactly one banner line. If the server is at its
client limit it instead sends `err server full` and immediately closes.

## Commands

### `tick`

Read all sensors, store in history, evaluate alarms.

**Request:**
```
tick
```

**Response:**
```
tick <number>: <sensor1>=<value1> <sensor2>=<value2> ...
```

For each alarm that fires this tick, an additional line follows:
```
! ALARM <name> [<severity>] <operator> <threshold>
```
For a symmetry-mode alarm the severity carries a ` SYMM` suffix, e.g.
`[WARN SYMM]`.

If a `VETO`-severity alarm fired this tick, a veto line follows:
```
⛔ VETO ACTIVE — overridden by '<alarm name>'
```

For each registered symmetry pair, a passive-monitoring line follows:
```
  sym '<name>': r=<correlation> <✓|✗>
```
(`✓` when correlation meets the pair's threshold, `✗` otherwise.)

**Example:**
```
> tick
tick 1: cpu_temp=82.34 random=47.81 constant=42.00
! ALARM overheat [CRIT] > 80.00
```

### `history [N]`

Show last N sensor readings. Default N is 10.

**Request:**
```
history
history 20
```

**Response:**
```
history (<count>):
  <sensor1>: <val1> <val2> ... <valN>
  <sensor2>: <val1> <val2> ... <valN>
```

Most recent value first. Shows up to N or available count, whichever is less.

**Example:**
```
> history 5
history (5):
  cpu_temp: 65.12 62.34 58.91 61.45 64.22
  random: 23.45 47.81 91.33 12.67 85.40
  constant: 42.00 42.00 42.00 42.00 42.00
```

### `<actuator> <value>`

Set an actuator to a value.

**Request:**
```
<actuator_name> <float_value>
```

**Response (success):**
```
ok <actuator_name>=<value>
```

**Response (unknown actuator):**
```
err unknown actuator '<name>'
```

**Response (veto active):**
```
⛔ VETO BLOCKED — '<actuator_name>' overridden by '<veto source alarm>'
```
Any actuator write is refused while a `VETO`-severity alarm is active (see
`veto`). The actuator's stored value is left unchanged.

**Example:**
```
> fan_speed 75.5
ok fan_speed=75.50

> nonexistent 5.0
err unknown actuator 'nonexistent'
```

### `alarm list`

Show all registered alarms and their current state.

**Request:**
```
alarm list
```

**Response (standard alarm):**
```
alarms (<count>):
  [0] <name>  sensor=<sensor>  <operator> <threshold>  sev=<severity>  armed=<yes|no>
```

**Response (symmetry alarm):** symmetry alarms print a `SYMM` form with two
sensors and the correlation threshold:
```
  [1] <name>  SYMM  sensors=<a>/<b>  thresh=<correlation>  sev=<severity>  armed=<yes|no>
```

**Example:**
```
> alarm list
alarms (3):
  [0] overheat  sensor=cpu_temp  > 80.00  sev=CRIT  armed=yes
  [1] chilly    sensor=cpu_temp  < 50.00  sev=WARN  armed=no
  [2] lucky     sensor=random    >= 90.00  sev=INFO  armed=yes
```

### `symmetry list`

Show all registered symmetry pairs and their last computed correlation.

**Request:**
```
symmetry list
```

**Response:**
```
symmetry pairs (<count>):
  [0] <name>  <sensor_a> ↔ <sensor_b>  r=<correlation>  thresh=<threshold>  <✓|✗>
```

Symmetry pairs monitor correlation between two sensor histories passively; they
do not raise alarms on their own (use a symmetry *alarm* for that).

### `veto`

Show whether a veto is currently in effect (set by a `VETO`-severity alarm
during the last `tick`).

**Request:**
```
veto
```

**Response (veto active):**
```
⛔ VETO active — source: '<alarm name>' (alarm <index>)
```

**Response (no veto):**
```
✅ No veto active
```

### `subscribe`

Subscribe to automatic tick broadcasts.

**Request:**
```
subscribe
```

**Response:**
```
ok subscribed
```

After subscribing, the server sends automatic tick responses every tick interval
(default 2000ms).

### `unsubscribe`

Stop receiving tick broadcasts.

**Request:**
```
unsubscribe
```

**Response:**
```
ok unsubscribed
```

### `help`

List available commands.

**Request:**
```
help
```

**Response:**
```
Plato Engine Block — commands:
  tick            — read sensors, run one tick
  history [N]     — show last N readings (default 10)
  <actuator> <v>  — set actuator to value
  alarm list      — show all alarms
  symmetry list   — show all symmetry pairs
  veto            — show current veto state
  subscribe       — subscribe to tick broadcasts
  unsubscribe     — stop tick broadcasts
  help            — this message
  quit            — disconnect
```

### `quit`

Disconnect from the server.

**Request:**
```
quit
```

**Response:**
```
bye
```

The server sends this response and then closes the connection.

### Unknown Command / Empty Lines

**Request:**
```
<any unrecognized input>
```

**Response:**
```
err unknown command (try 'help')
```

Blank/whitespace-only lines are ignored by the server (no response, connection
stays open).

## Alarm Lifecycle

```
           ┌──────────────────────────────────┐
           │                                  │
           ▼                                  │
  ┌─────────────┐   threshold   ┌──────────┐  │
  │   ARMED     │─── crossed ──▶│  FIRING  │  │
  │             │               │          │  │
  └─────────────┘               └────┬─────┘  │
       ▲                             │        │
       │        cooldown expired     │        │
       │◀────────────────────────────┘        │
       │        cooldown ticks                │
       │                                      │
       └──────────────────────────────────────┘
```

1. Alarm starts **armed**
2. When sensor value meets condition → **fires** (one tick only)
3. Enters **cooldown** for `PLATO_ALARM_COOLDOWN` ticks
4. After cooldown → **re-arms**
5. If condition still met on next tick → fires again

This prevents alarm spam while ensuring sustained conditions are reported.

## Comparison Operators

| Operator | Meaning |
|---|---|
| `>` | Greater than |
| `<` | Less than |
| `>=` | Greater than or equal |
| `<=` | Less than or equal |
| `==` | Equal (epsilon ±1e-9) |

## Severity Levels

| Level | Meaning |
|---|---|
| `INFO` | Informational, no action needed |
| `WARN` | Warning, investigate soon |
| `CRIT` | Critical, immediate action required |
| `VETO` | Veto-level; when such an alarm fires, all actuator writes are blocked until the alarm re-arms |

## Binary Representation

The protocol is text-only. No binary framing. This is intentional:
- Telnet-friendly for debugging
- Easy to parse with `sscanf()` / `strtok()`
- Logs cleanly to files
- Works over serial ports, WebSockets, and Unix pipes

## Version History

| Version | Date | Changes |
|---|---|---|
| 1.0 | 2026-06 | Initial specification |
| 1.1 | 2026-07 | Document `symmetry list` and `veto` commands, `VETO` severity, veto-blocked actuator response, symmetry-mode `alarm list` form, and the extra `tick` output lines (veto / per-pair symmetry). Server now sends `bye` before closing on `quit`. |
