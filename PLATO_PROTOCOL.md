# Plato Wire Protocol Specification

> Version 1.0 — June 2026

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

If any alarms fire, additional lines follow:
```
! ALARM <name> [<severity>] <operator> <threshold>
```

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

**Response:**
```
alarms (<count>):
  [0] <name>  sensor=<sensor>  <operator> <threshold>  sev=<severity>  armed=<yes|no>
```

**Example:**
```
> alarm list
alarms (3):
  [0] overheat  sensor=cpu_temp  > 80.00  sev=CRIT  armed=yes
  [1] chilly    sensor=cpu_temp  < 50.00  sev=WARN  armed=no
  [2] lucky     sensor=random    >= 90.00  sev=INFO  armed=yes
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

The server closes the connection after sending this response.

### Unknown Command

**Request:**
```
<any unrecognized input>
```

**Response:**
```
err unknown command (try 'help')
```

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
