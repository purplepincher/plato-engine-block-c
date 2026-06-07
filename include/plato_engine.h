/*
 * plato_engine.h — Plato Engine Block: a tiny, embeddable sensor→history→alarm
 *                  engine in a single header.
 *
 * C99, <500 lines of implementation, no dynamic allocation after init.
 * Suitable for POSIX daemons, bare-metal MCUs, and game loops alike.
 *
 * ⚡ TernARY UPGRADE (v1.1):
 *   - Ternary-Continuous Shim: conviction [0,1] map to Trit gates {-1,0,+1}
 *   - Veto System: PLATO_VETO severity overrides actuator writes
 *   - Symmetry Check: cross-correlation between sensor pairs
 *
 * Usage (in ONE .c file):
 *     #define PLATO_ENGINE_IMPL
 *     #include "plato_engine.h"
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLATO_ENGINE_H
#define PLATO_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Tuneable constants (override before #include if you like)         */
/* ------------------------------------------------------------------ */
#ifndef PLATO_MAX_SENSORS
#define PLATO_MAX_SENSORS      16
#endif
#ifndef PLATO_MAX_ACTUATORS
#define PLATO_MAX_ACTUATORS    8
#endif
#ifndef PLATO_MAX_ALARMS
#define PLATO_MAX_ALARMS       16
#endif
#ifndef PLATO_MAX_HISTORY
#define PLATO_MAX_HISTORY      256
#endif
#ifndef PLATO_MAX_SUBSCRIBERS
#define PLATO_MAX_SUBSCRIBERS  32
#endif
#ifndef PLATO_ALARM_COOLDOWN
#define PLATO_ALARM_COOLDOWN   10   /* ticks before re-arm */
#endif
#ifndef PLATO_NAME_LEN
#define PLATO_NAME_LEN         32
#endif
#ifndef PLATO_CMD_BUF
#define PLATO_CMD_BUF          512
#endif
#ifndef PLATO_RESP_BUF
#define PLATO_RESP_BUF         1024
#endif

/* ---- TernARY constants ---- */
#ifndef PLATO_LEMINAL_LOW
#define PLATO_LEMINAL_LOW      0.3   /* conviction < 0.3 → trit NEG(-1) */
#endif
#ifndef PLATO_LEMINAL_HIGH
#define PLATO_LEMINAL_HIGH     0.7   /* conviction > 0.7 → trit POS(+1) */
#endif
#ifndef PLATO_SYMMETRY_WINDOW
#define PLATO_SYMMETRY_WINDOW  16    /* sample window for symmetry check */
#endif
#ifndef PLATO_SYMMETRY_THRESHOLD
#define PLATO_SYMMETRY_THRESHOLD 0.75 /* min cross-correlation to pass */
#endif
#ifndef PLATO_MAX_SYMMETRY_PAIRS
#define PLATO_MAX_SYMMETRY_PAIRS 8
#endif

/* ------------------------------------------------------------------ */
/*  Compiler portability                                              */
/* ------------------------------------------------------------------ */
#ifndef PLATO_UNUSED
#if defined(__GNUC__) || defined(__clang__)
#define PLATO_UNUSED __attribute__((unused))
#else
#define PLATO_UNUSED
#endif
#endif

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

/** Trit: ternary logic gate value — {-1, 0, +1}. */
typedef enum {
    PLATO_TRIT_NEG  = -1,
    PLATO_TRIT_ZERO =  0,
    PLATO_TRIT_POS  =  1
} plato_trit_t;

/** Conviction: continuous belief in [0.0, 1.0]. */
typedef double plato_conviction_t;

/** Sensor read callback. Returns the current value. */
typedef double (*plato_sensor_fn)(void *user_data);

/** Actuator write callback. Returns 0 on success. */
typedef int (*plato_actuator_fn)(const char *name, double value, void *user_data);

/** Alarm comparison operator. */
typedef enum {
    PLATO_GT,          /* value >  threshold */
    PLATO_LT,          /* value <  threshold */
    PLATO_GTE,         /* value >= threshold */
    PLATO_LTE,         /* value <= threshold */
    PLATO_EQ           /* value == threshold (epsilon) */
} plato_cmp_t;

/** Severity levels for alarms (+ VETO override). */
typedef enum {
    PLATO_INFO = 0,
    PLATO_WARN = 1,
    PLATO_CRIT = 2,
    PLATO_VETO = 3   /* ⛔ veto-level: overrides actuator writes */
} plato_severity_t;

/** Alarm mode: standard comparison or symmetry-based. */
typedef enum {
    PLATO_ALARM_STANDARD,       /* normal threshold alarm */
    PLATO_ALARM_SYMMETRY        /* symmetry-break alarm (two sensors) */
} plato_alarm_mode_t;

/* ---- descriptors ---- */

typedef struct {
    char           name[PLATO_NAME_LEN];
    plato_sensor_fn read;
    void          *user_data;
} plato_sensor_t;

typedef struct {
    char             name[PLATO_NAME_LEN];
    plato_actuator_fn write;
    void            *user_data;
    double           value;        /* last written value */
} plato_actuator_t;

typedef struct {
    char             name[PLATO_NAME_LEN];
    int              sensor_idx;       /* primary sensor (or first for symmetry) */
    int              sensor_idx2;      /* second sensor (symmetry only, else -1) */
    plato_alarm_mode_t mode;           /* STANDARD or SYMMETRY */
    plato_cmp_t      cmp;
    double           threshold;
    plato_severity_t severity;
    int              cooldown;         /* remaining ticks */
    bool             armed;            /* true = can fire */
    bool             firing;           /* true this tick */
} plato_alarm_t;

/* ---- symmetry pair descriptor ---- */

typedef struct {
    char  name[PLATO_NAME_LEN];
    int   sensor_a;                   /* index of first sensor */
    int   sensor_b;                   /* index of second sensor */
    double threshold;                  /* min correlation to pass */
    double last_correlation;           /* result of last check */
    bool   symmetric;                  /* true if last check passed */
} plato_symmetry_pair_t;

/* ---- history ring buffer ---- */

typedef struct {
    double  values[PLATO_MAX_HISTORY];
    int     count;      /* how many slots filled (≤ PLATO_MAX_HISTORY) */
    int     head;       /* next write position */
} plato_history_t;

/* ---- subscriber (opaque int handle, e.g. fd) ---- */

typedef struct {
    int  handles[PLATO_MAX_SUBSCRIBERS];
    bool subscribed[PLATO_MAX_SUBSCRIBERS];
    int  count;
} plato_subs_t;

/* ---- the engine ---- */

typedef struct {
    plato_sensor_t    sensors[PLATO_MAX_SENSORS];
    int               sensor_count;

    plato_actuator_t  actuators[PLATO_MAX_ACTUATORS];
    int               actuator_count;

    plato_alarm_t     alarms[PLATO_MAX_ALARMS];
    int               alarm_count;

    plato_history_t   history[PLATO_MAX_SENSORS];  /* one per sensor */

    plato_symmetry_pair_t symmetry_pairs[PLATO_MAX_SYMMETRY_PAIRS];
    int                   symmetry_pair_count;

    plato_subs_t      subs;

    double            last_tick[PLATO_MAX_SENSORS];
    uint64_t          tick_num;

    /* ---- Veto state ---- */
    bool              veto_active;       /* true = veto in effect this tick */
    int               veto_source_alarm; /* index of the alarm that fired veto */
    char              veto_source_name[PLATO_NAME_LEN]; /* sensor name that triggered */
} plato_engine_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/** Initialise an engine (zero it out, ready for use). */
void plato_init(plato_engine_t *eng);

/** Register a sensor. Returns sensor index or -1 on full. */
int  plato_add_sensor(plato_engine_t *eng, const char *name,
                      plato_sensor_fn fn, void *user_data);

/** Register an actuator. Returns actuator index or -1 on full. */
int  plato_add_actuator(plato_engine_t *eng, const char *name,
                        plato_actuator_fn fn, void *user_data);

/** Register a standard alarm. Returns alarm index or -1 on full. */
int  plato_add_alarm(plato_engine_t *eng, const char *name,
                     int sensor_idx, plato_cmp_t cmp,
                     double threshold, plato_severity_t severity);

/** Register a symmetry alarm (fires when two sensors decouple). */
int  plato_add_symmetry_alarm(plato_engine_t *eng, const char *name,
                              int sensor_a, int sensor_b,
                              double min_correlation, plato_severity_t severity);

/** Register a symmetry pair for passive monitoring. Returns index or -1. */
int  plato_add_symmetry_pair(plato_engine_t *eng, const char *name,
                             int sensor_a, int sensor_b, double threshold);

/**
 * Run one tick: read all sensors, store history, evaluate alarms,
 * check symmetry pairs, evaluate veto state.
 * Fills `eng->last_tick[]` and advances `eng->tick_num`.
 */
void plato_tick(plato_engine_t *eng);

/**
 * Check a specific symmetry pair. Returns the cross-correlation [0,1].
 * Convenience wrapper — symmetry pairs are checked automatically in tick().
 */
double plato_check_symmetry(plato_engine_t *eng, int pair_idx);

/* ---- Ternary-Continuous Shim ---- */

/** Convert conviction [0,1] to trit {-1, 0, +1} with Leminal Zone deadband. */
static inline plato_trit_t plato_conviction_to_trit(plato_conviction_t c) {
    if (c < PLATO_LEMINAL_LOW)  return PLATO_TRIT_NEG;
    if (c > PLATO_LEMINAL_HIGH) return PLATO_TRIT_POS;
    return PLATO_TRIT_ZERO;
}

/** Convert trit to centre-of-zone conviction [0,1]. */
static inline plato_conviction_t plato_trit_to_conviction(plato_trit_t t) {
    switch (t) {
        case PLATO_TRIT_NEG:  return 0.0;
        case PLATO_TRIT_ZERO: return 0.5;
        case PLATO_TRIT_POS:  return 1.0;
    }
    return 0.5;
}

/** Normalise a raw value into a conviction [0,1] given min/max range. */
static inline plato_conviction_t plato_normalise_conviction(double raw,
                                                             double min_val,
                                                             double max_val) {
    if (max_val <= min_val) return 0.5;
    double clamped = raw < min_val ? min_val : (raw > max_val ? max_val : raw);
    return (clamped - min_val) / (max_val - min_val);
}

/** Is the conviction in the deadband (Leminal Zone)? */
static inline bool plato_is_lemminal(plato_conviction_t c) {
    return c >= PLATO_LEMINAL_LOW && c <= PLATO_LEMINAL_HIGH;
}

/* ---- Veto query ---- */

/** Check if a veto is active after last tick. */
static inline bool plato_veto_active(const plato_engine_t *eng) {
    return eng->veto_active;
}

/** Get the name of the sensor that triggered the current veto. */
static inline const char *plato_veto_source(const plato_engine_t *eng) {
    return eng->veto_source_name;
}

/* ---- Command handler ---- */

/**
 * Handle a text command, write response into `resp` (up to resp_len-1 bytes).
 * Returns response length.
 *
 * Commands: tick, history [N], <actuator> <value>, alarm list,
 *           symmetry list, veto, subscribe, unsubscribe, help, quit
 */
int  plato_handle_command(plato_engine_t *eng, const char *cmd,
                          char *resp, size_t resp_len);

/** Subscribe a handle (e.g. fd). Returns 0 on success, -1 on full. */
int  plato_subscribe(plato_engine_t *eng, int handle);

/** Unsubscribe a handle. Returns 0 on success, -1 if not found. */
int  plato_unsubscribe(plato_engine_t *eng, int handle);

/* ------------------------------------------------------------------ */
/*  Implementation (only when PLATO_ENGINE_IMPL is defined)            */
/* ------------------------------------------------------------------ */
#ifdef PLATO_ENGINE_IMPL

/* ---- helpers ---- */

static void history_push(plato_history_t *h, double val) {
    h->values[h->head] = val;
    h->head = (h->head + 1) % PLATO_MAX_HISTORY;
    if (h->count < PLATO_MAX_HISTORY) h->count++;
}

static double history_get(const plato_history_t *h, int n) {
    /* n=0 is most recent */
    if (n < 0 || n >= h->count) return 0.0;
    int idx = (h->head - 1 - n + PLATO_MAX_HISTORY) % PLATO_MAX_HISTORY;
    return h->values[idx];
}

static bool cmp_eval(double val, plato_cmp_t cmp, double threshold) {
    switch (cmp) {
        case PLATO_GT:  return val >  threshold;
        case PLATO_LT:  return val <  threshold;
        case PLATO_GTE: return val >= threshold;
        case PLATO_LTE: return val <= threshold;
        case PLATO_EQ:  return fabs(val - threshold) < 1e-9;
    }
    return false;
}

static const char *cmp_str(plato_cmp_t cmp) {
    switch (cmp) {
        case PLATO_GT:  return ">";
        case PLATO_LT:  return "<";
        case PLATO_GTE: return ">=";
        case PLATO_LTE: return "<=";
        case PLATO_EQ:  return "==";
    }
    return "?";
}

static const char *severity_str(plato_severity_t s) {
    switch (s) {
        case PLATO_INFO: return "INFO";
        case PLATO_WARN: return "WARN";
        case PLATO_CRIT: return "CRIT";
        case PLATO_VETO: return "VETO";
    }
    return "?";
}

static const char *trit_str(plato_trit_t t) PLATO_UNUSED;
static const char *trit_str(plato_trit_t t) {
    switch (t) {
        case PLATO_TRIT_NEG:  return "NEG(-1)";
        case PLATO_TRIT_ZERO: return "ZERO(0)";
        case PLATO_TRIT_POS:  return "POS(+1)";
    }
    return "?";
}

/* ---- normalised cross-correlation between two history buffers ---- */

static double history_cross_corr(const plato_history_t *ha,
                                  const plato_history_t *hb,
                                  int window) {
    int n = window < ha->count ? window : ha->count;
    if (hb->count < n) n = hb->count;
    if (n < 2) return 1.0; /* not enough data — assume symmetric */

    double sum_a = 0.0, sum_b = 0.0;
    for (int i = 0; i < n; i++) {
        sum_a += history_get(ha, i);
        sum_b += history_get(hb, i);
    }
    double mean_a = sum_a / n;
    double mean_b = sum_b / n;

    double num = 0.0, den_a = 0.0, den_b = 0.0;
    for (int i = 0; i < n; i++) {
        double da = history_get(ha, i) - mean_a;
        double db = history_get(hb, i) - mean_b;
        num  += da * db;
        den_a += da * da;
        den_b += db * db;
    }
    double den = sqrt(den_a) * sqrt(den_b);
    if (den < 1e-12) return 1.0; /* flat signals → symmetric */
    double r = num / den;
    return (r < 0.0) ? 0.0 : r;  /* clamp negative to 0 */
}

/* ---- public API ---- */

void plato_init(plato_engine_t *eng) {
    memset(eng, 0, sizeof(*eng));
}

int plato_add_sensor(plato_engine_t *eng, const char *name,
                     plato_sensor_fn fn, void *user_data) {
    if (eng->sensor_count >= PLATO_MAX_SENSORS) return -1;
    int idx = eng->sensor_count++;
    strncpy(eng->sensors[idx].name, name, PLATO_NAME_LEN - 1);
    eng->sensors[idx].read       = fn;
    eng->sensors[idx].user_data  = user_data;
    return idx;
}

int plato_add_actuator(plato_engine_t *eng, const char *name,
                       plato_actuator_fn fn, void *user_data) {
    if (eng->actuator_count >= PLATO_MAX_ACTUATORS) return -1;
    int idx = eng->actuator_count++;
    strncpy(eng->actuators[idx].name, name, PLATO_NAME_LEN - 1);
    eng->actuators[idx].write     = fn;
    eng->actuators[idx].user_data = user_data;
    eng->actuators[idx].value     = 0.0;
    return idx;
}

int plato_add_alarm(plato_engine_t *eng, const char *name,
                    int sensor_idx, plato_cmp_t cmp,
                    double threshold, plato_severity_t severity) {
    if (eng->alarm_count >= PLATO_MAX_ALARMS) return -1;
    if (sensor_idx < 0 || sensor_idx >= eng->sensor_count) return -1;
    int idx = eng->alarm_count++;
    strncpy(eng->alarms[idx].name, name, PLATO_NAME_LEN - 1);
    eng->alarms[idx].sensor_idx  = sensor_idx;
    eng->alarms[idx].sensor_idx2 = -1;
    eng->alarms[idx].mode        = PLATO_ALARM_STANDARD;
    eng->alarms[idx].cmp         = cmp;
    eng->alarms[idx].threshold   = threshold;
    eng->alarms[idx].severity    = severity;
    eng->alarms[idx].cooldown    = 0;
    eng->alarms[idx].armed       = true;
    eng->alarms[idx].firing      = false;
    return idx;
}

int plato_add_symmetry_alarm(plato_engine_t *eng, const char *name,
                              int sensor_a, int sensor_b,
                              double min_correlation, plato_severity_t severity) {
    if (eng->alarm_count >= PLATO_MAX_ALARMS) return -1;
    if (sensor_a < 0 || sensor_a >= eng->sensor_count) return -1;
    if (sensor_b < 0 || sensor_b >= eng->sensor_count) return -1;
    if (sensor_a == sensor_b) return -1;
    int idx = eng->alarm_count++;
    strncpy(eng->alarms[idx].name, name, PLATO_NAME_LEN - 1);
    eng->alarms[idx].sensor_idx  = sensor_a;
    eng->alarms[idx].sensor_idx2 = sensor_b;
    eng->alarms[idx].mode        = PLATO_ALARM_SYMMETRY;
    eng->alarms[idx].cmp         = PLATO_LT; /* fires when corr < threshold */
    eng->alarms[idx].threshold   = min_correlation;
    eng->alarms[idx].severity    = severity;
    eng->alarms[idx].cooldown    = 0;
    eng->alarms[idx].armed       = true;
    eng->alarms[idx].firing      = false;
    return idx;
}

int plato_add_symmetry_pair(plato_engine_t *eng, const char *name,
                            int sensor_a, int sensor_b, double threshold) {
    if (eng->symmetry_pair_count >= PLATO_MAX_SYMMETRY_PAIRS) return -1;
    if (sensor_a < 0 || sensor_a >= eng->sensor_count) return -1;
    if (sensor_b < 0 || sensor_b >= eng->sensor_count) return -1;
    if (sensor_a == sensor_b) return -1;
    int idx = eng->symmetry_pair_count++;
    strncpy(eng->symmetry_pairs[idx].name, name, PLATO_NAME_LEN - 1);
    eng->symmetry_pairs[idx].sensor_a = sensor_a;
    eng->symmetry_pairs[idx].sensor_b = sensor_b;
    eng->symmetry_pairs[idx].threshold = threshold;
    eng->symmetry_pairs[idx].last_correlation = 1.0;
    eng->symmetry_pairs[idx].symmetric = true;
    return idx;
}

double plato_check_symmetry(plato_engine_t *eng, int pair_idx) {
    if (pair_idx < 0 || pair_idx >= eng->symmetry_pair_count) return 0.0;
    plato_symmetry_pair_t *p = &eng->symmetry_pairs[pair_idx];
    double corr = history_cross_corr(&eng->history[p->sensor_a],
                                      &eng->history[p->sensor_b],
                                      PLATO_SYMMETRY_WINDOW);
    p->last_correlation = corr;
    p->symmetric = (corr >= p->threshold);
    return corr;
}

void plato_tick(plato_engine_t *eng) {
    eng->tick_num++;

    /* read sensors */
    for (int i = 0; i < eng->sensor_count; i++) {
        double val = eng->sensors[i].read(eng->sensors[i].user_data);
        eng->last_tick[i] = val;
        history_push(&eng->history[i], val);
    }

    /* evaluate symmetry pairs (passive monitoring) */
    for (int i = 0; i < eng->symmetry_pair_count; i++) {
        plato_check_symmetry(eng, i);
    }

    /* reset veto at start of tick */
    eng->veto_active = false;
    eng->veto_source_name[0] = '\0';
    eng->veto_source_alarm = -1;

    /* evaluate alarms */
    for (int i = 0; i < eng->alarm_count; i++) {
        plato_alarm_t *a = &eng->alarms[i];
        a->firing = false;

        /* cooldown tick */
        if (a->cooldown > 0) {
            a->cooldown--;
            if (a->cooldown == 0) a->armed = true;
        }

        if (!a->armed) continue;

        bool condition_met = false;

        if (a->mode == PLATO_ALARM_SYMMETRY) {
            /* symmetry alarm: fires when correlation drops below threshold */
            double corr = history_cross_corr(&eng->history[a->sensor_idx],
                                              &eng->history[a->sensor_idx2],
                                              PLATO_SYMMETRY_WINDOW);
            condition_met = (corr < a->threshold);
        } else {
            /* standard alarm */
            double val = eng->last_tick[a->sensor_idx];
            condition_met = cmp_eval(val, a->cmp, a->threshold);
        }

        if (condition_met) {
            a->firing = true;
            a->armed  = false;
            a->cooldown = PLATO_ALARM_COOLDOWN;

            /* VETO override: highest severity, disables actuator writes */
            if (a->severity >= PLATO_VETO) {
                eng->veto_active = true;
                eng->veto_source_alarm = i;
                strncpy(eng->veto_source_name, a->name, PLATO_NAME_LEN - 1);
            }
        }
    }
}

int plato_subscribe(plato_engine_t *eng, int handle) {
    for (int i = 0; i < PLATO_MAX_SUBSCRIBERS; i++) {
        if (!eng->subs.subscribed[i]) {
            eng->subs.handles[i]     = handle;
            eng->subs.subscribed[i]  = true;
            eng->subs.count++;
            return 0;
        }
    }
    return -1;
}

int plato_unsubscribe(plato_engine_t *eng, int handle) {
    for (int i = 0; i < PLATO_MAX_SUBSCRIBERS; i++) {
        if (eng->subs.subscribed[i] && eng->subs.handles[i] == handle) {
            eng->subs.subscribed[i] = false;
            eng->subs.count--;
            return 0;
        }
    }
    return -1;
}

/* ---- command handling ---- */

/* trim leading whitespace */
static const char *trim(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

/* trim trailing whitespace in-place */
static void rtrim(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

int plato_handle_command(plato_engine_t *eng, const char *cmd,
                         char *resp, size_t resp_len) {
    /* work on a trimmed copy */
    char cmdbuf[PLATO_CMD_BUF];
    strncpy(cmdbuf, cmd, sizeof(cmdbuf) - 1);
    cmdbuf[sizeof(cmdbuf) - 1] = '\0';
    rtrim(cmdbuf);
    const char *c = trim(cmdbuf);

    /* ---- tick ---- */
    if (strcmp(c, "tick") == 0) {
        plato_tick(eng);
        int n = 0;
        n += snprintf(resp + n, resp_len - n, "tick %llu:",
                      (unsigned long long)eng->tick_num);
        for (int i = 0; i < eng->sensor_count; i++) {
            n += snprintf(resp + n, resp_len - n, " %s=%.2f",
                          eng->sensors[i].name, eng->last_tick[i]);
        }
        /* fire alarms */
        for (int i = 0; i < eng->alarm_count; i++) {
            if (eng->alarms[i].firing) {
                const char *mode_str = (eng->alarms[i].mode == PLATO_ALARM_SYMMETRY)
                                        ? " SYMM" : "";
                n += snprintf(resp + n, resp_len - n,
                              "\n! ALARM %s [%s%s] %s %.2f",
                              eng->alarms[i].name,
                              severity_str(eng->alarms[i].severity),
                              mode_str,
                              cmp_str(eng->alarms[i].cmp),
                              eng->alarms[i].threshold);
            }
        }
        /* report veto */
        if (eng->veto_active) {
            n += snprintf(resp + n, resp_len - n,
                          "\n⛔ VETO ACTIVE — overridden by '%s'",
                          eng->veto_source_name);
        }
        /* report symmetry state if any pairs are defined */
        for (int i = 0; i < eng->symmetry_pair_count; i++) {
            plato_symmetry_pair_t *p = &eng->symmetry_pairs[i];
            n += snprintf(resp + n, resp_len - n,
                          "\n  sym '%s': r=%.2f %s",
                          p->name, p->last_correlation,
                          p->symmetric ? "✓" : "✗");
        }
        return n;
    }

    /* ---- history [N] ---- */
    if (strncmp(c, "history", 7) == 0 &&
        (cmd[7] == '\0' || cmd[7] == ' ' || cmd[7] == '\n')) {
        int n = 0;
        const char *arg = trim(c + 7);
        int count = 10; /* default */
        if (*arg) count = atoi(arg);
        if (count <= 0) count = 10;

        n += snprintf(resp + n, resp_len - n, "history (%d):\n", count);
        for (int s = 0; s < eng->sensor_count; s++) {
            int avail = eng->history[s].count;
            int show  = count < avail ? count : avail;
            n += snprintf(resp + n, resp_len - n, "  %s:", eng->sensors[s].name);
            for (int j = 0; j < show; j++) {
                n += snprintf(resp + n, resp_len - n, " %.2f",
                              history_get(&eng->history[s], j));
            }
            n += snprintf(resp + n, resp_len - n, "\n");
        }
        return n;
    }

    /* ---- alarm list ---- */
    if (strcmp(c, "alarm list") == 0) {
        int n = 0;
        n += snprintf(resp + n, resp_len - n, "alarms (%d):\n", eng->alarm_count);
        for (int i = 0; i < eng->alarm_count; i++) {
            plato_alarm_t *a = &eng->alarms[i];
            if (a->mode == PLATO_ALARM_SYMMETRY) {
                n += snprintf(resp + n, resp_len - n,
                              "  [%d] %s  SYMM  sensors=%s/%s  thresh=%.2f  sev=%s  armed=%s\n",
                              i, a->name,
                              eng->sensors[a->sensor_idx].name,
                              eng->sensors[a->sensor_idx2].name,
                              a->threshold,
                              severity_str(a->severity),
                              a->armed ? "yes" : "no");
            } else {
                n += snprintf(resp + n, resp_len - n,
                              "  [%d] %s  sensor=%s  %s %.2f  sev=%s  armed=%s\n",
                              i, a->name, eng->sensors[a->sensor_idx].name,
                              cmp_str(a->cmp), a->threshold,
                              severity_str(a->severity),
                              a->armed ? "yes" : "no");
            }
        }
        return n;
    }

    /* ---- symmetry list ---- */
    if (strcmp(c, "symmetry list") == 0) {
        int n = 0;
        n += snprintf(resp + n, resp_len - n, "symmetry pairs (%d):\n",
                      eng->symmetry_pair_count);
        for (int i = 0; i < eng->symmetry_pair_count; i++) {
            plato_symmetry_pair_t *p = &eng->symmetry_pairs[i];
            n += snprintf(resp + n, resp_len - n,
                          "  [%d] %s  %s ↔ %s  r=%.2f  thresh=%.2f  %s\n",
                          i, p->name,
                          eng->sensors[p->sensor_a].name,
                          eng->sensors[p->sensor_b].name,
                          p->last_correlation,
                          p->threshold,
                          p->symmetric ? "✓" : "✗");
        }
        return n;
    }

    /* ---- veto status ---- */
    if (strcmp(c, "veto") == 0) {
        if (eng->veto_active) {
            return snprintf(resp, resp_len,
                            "⛔ VETO active — source: '%s' (alarm %d)",
                            eng->veto_source_name, eng->veto_source_alarm);
        }
        return snprintf(resp, resp_len, "✅ No veto active");
    }

    /* ---- submit / unsubscribe (stub — real fd handled by server) ---- */
    if (strcmp(c, "subscribe") == 0) {
        snprintf(resp, resp_len, "ok subscribed");
        return (int)strlen(resp);
    }
    if (strcmp(c, "unsubscribe") == 0) {
        snprintf(resp, resp_len, "ok unsubscribed");
        return (int)strlen(resp);
    }

    /* ---- help ---- */
    if (strcmp(c, "help") == 0) {
        return snprintf(resp, resp_len,
            "Plato Engine Block — commands:\n"
            "  tick            — read sensors, run one tick\n"
            "  history [N]     — show last N readings (default 10)\n"
            "  <actuator> <v>  — set actuator to value\n"
            "  alarm list      — show all alarms\n"
            "  symmetry list   — show all symmetry pairs\n"
            "  veto            — show current veto state\n"
            "  subscribe       — subscribe to tick broadcasts\n"
            "  unsubscribe     — stop tick broadcasts\n"
            "  help            — this message\n"
            "  quit            — disconnect\n");
    }

    /* ---- quit ---- */
    if (strcmp(c, "quit") == 0) {
        snprintf(resp, resp_len, "bye");
        return (int)strlen(resp);
    }

    /* ---- actuator <name> <value> ---- */
    {
        char aname[PLATO_NAME_LEN] = {0};
        double aval = 0.0;
        if (sscanf(c, "%31s %lf", aname, &aval) == 2) {
            for (int i = 0; i < eng->actuator_count; i++) {
                if (strcmp(eng->actuators[i].name, aname) == 0) {
                    /* check veto: blocked if veto is active */
                    if (eng->veto_active) {
                        return snprintf(resp, resp_len,
                                        "⛔ VETO BLOCKED — '%s' overridden by '%s'",
                                        aname, eng->veto_source_name);
                    }
                    if (eng->actuators[i].write) {
                        eng->actuators[i].write(aname, aval,
                                                 eng->actuators[i].user_data);
                    }
                    eng->actuators[i].value = aval;
                    return snprintf(resp, resp_len, "ok %s=%.2f", aname, aval);
                }
            }
            return snprintf(resp, resp_len, "err unknown actuator '%s'", aname);
        }
    }

    return snprintf(resp, resp_len, "err unknown command (try 'help')");
}

#endif /* PLATO_ENGINE_IMPL */
#endif /* PLATO_ENGINE_H */
