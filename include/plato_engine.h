/*
 * plato_engine.h — Plato Engine Block: a tiny, embeddable sensor→history→alarm
 *                  engine in a single header.
 *
 * C99, <400 lines of implementation, no dynamic allocation after init.
 * Suitable for POSIX daemons, bare-metal MCUs, and game loops alike.
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

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

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

/** Severity levels for alarms. */
typedef enum {
    PLATO_INFO = 0,
    PLATO_WARN = 1,
    PLATO_CRIT = 2
} plato_severity_t;

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
    int              sensor_idx;
    plato_cmp_t      cmp;
    double           threshold;
    plato_severity_t severity;
    int              cooldown;     /* remaining ticks */
    bool             armed;        /* true = can fire */
    bool             firing;       /* true this tick */
} plato_alarm_t;

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

    plato_subs_t      subs;

    double            last_tick[PLATO_MAX_SENSORS];
    uint64_t          tick_num;
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

/** Register an alarm. Returns alarm index or -1 on full. */
int  plato_add_alarm(plato_engine_t *eng, const char *name,
                     int sensor_idx, plato_cmp_t cmp,
                     double threshold, plato_severity_t severity);

/**
 * Run one tick: read all sensors, store history, evaluate alarms.
 * Fills `eng->last_tick[]` and advances `eng->tick_num`.
 */
void plato_tick(plato_engine_t *eng);

/**
 * Handle a text command, write response into `resp` (up to resp_len-1 bytes).
 * Returns response length.
 *
 * Commands: tick, history [N], <actuator> <value>, alarm list,
 *           subscribe, unsubscribe, help, quit
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
    }
    return "?";
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
    eng->alarms[idx].sensor_idx = sensor_idx;
    eng->alarms[idx].cmp        = cmp;
    eng->alarms[idx].threshold  = threshold;
    eng->alarms[idx].severity   = severity;
    eng->alarms[idx].cooldown   = 0;
    eng->alarms[idx].armed      = true;
    eng->alarms[idx].firing     = false;
    return idx;
}

void plato_tick(plato_engine_t *eng) {
    eng->tick_num++;

    /* read sensors */
    for (int i = 0; i < eng->sensor_count; i++) {
        double val = eng->sensors[i].read(eng->sensors[i].user_data);
        eng->last_tick[i] = val;
        history_push(&eng->history[i], val);
    }

    /* evaluate alarms */
    for (int i = 0; i < eng->alarm_count; i++) {
        plato_alarm_t *a = &eng->alarms[i];
        a->firing = false;

        /* cooldown tick */
        if (a->cooldown > 0) {
            a->cooldown--;
            if (a->cooldown == 0) a->armed = true;
        }

        if (a->armed) {
            double val = eng->last_tick[a->sensor_idx];
            if (cmp_eval(val, a->cmp, a->threshold)) {
                a->firing = true;
                a->armed  = false;
                a->cooldown = PLATO_ALARM_COOLDOWN;
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

/* trim leading and trailing whitespace (in-place for trailing) */
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
                n += snprintf(resp + n, resp_len - n,
                              "\n! ALARM %s [%s] %s %.2f",
                              eng->alarms[i].name,
                              severity_str(eng->alarms[i].severity),
                              cmp_str(eng->alarms[i].cmp),
                              eng->alarms[i].threshold);
            }
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
            n += snprintf(resp + n, resp_len - n,
                          "  [%d] %s  sensor=%s  %s %.2f  sev=%s  armed=%s\n",
                          i, a->name, eng->sensors[a->sensor_idx].name,
                          cmp_str(a->cmp), a->threshold,
                          severity_str(a->severity),
                          a->armed ? "yes" : "no");
        }
        return n;
    }

    /* ---- subscribe / unsubscribe (stub — real fd handled by server) ---- */
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
