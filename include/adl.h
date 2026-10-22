/*
 * Self-Organizing Data Structure  --  Adaptive Data Lab (ADL)
 * Author : Asutosh Sahoo
 * Univ.  : Veer Surendra Sai University of Technology (VSSUT)
 *
 * adl.h : common API (Adapter), backend factory (Factory),
 *         observer hook (Observer).
 */
#ifndef ADL_H
#define ADL_H

#include <stddef.h>
#include <stdint.h>

#define ADL_AUTHOR     "Asutosh Sahoo"
#define ADL_UNIVERSITY "Veer Surendra Sai University of Technology"

typedef struct DS DS;

/* Observer pattern: events are "Accessed", "PolicyChanged", "MetricsUpdated". */
typedef void (*ds_observer_fn)(void *ctx, const char *event, long key, long aux);

/* Adapter pattern: one common API (get / put / erase) over every backend. */
typedef struct DSOps {
    const char *name;
    int    (*get)(DS *, long key, long *out);   /* 1 if found, else 0            */
    int    (*put)(DS *, long key, long value);  /* 1 if inserted, 0 if updated   */
    int    (*erase)(DS *, long key);            /* 1 if removed, else 0          */
    void   (*destroy)(DS *);
    size_t (*mem_bytes)(const DS *);
    size_t (*size)(const DS *);
    size_t (*snapshot)(const DS *, long *out, size_t max); /* keys in list order */
    const char *(*policy)(const DS *);          /* human readable policy / state */
} DSOps;

struct DS {
    const DSOps *ops;
    uint64_t comparisons;      /* key comparisons performed            */
    uint64_t adapt_steps;      /* pointer moves / walks for reordering */
    uint64_t policy_switches;  /* times the adaptive layer changed policy */
    ds_observer_fn observer;
    void *observer_ctx;
};

/* Factory pattern: kinds = "fixed", "mtf", "transpose", "freq", "adaptive". */
extern const char *const ds_kinds[];
extern const size_t ds_kinds_count;
DS *ds_create(const char *kind);

static inline int    ds_get(DS *d, long k, long *o) { return d->ops->get(d, k, o); }
static inline int    ds_put(DS *d, long k, long v)  { return d->ops->put(d, k, v); }
static inline int    ds_erase(DS *d, long k)        { return d->ops->erase(d, k); }
static inline void   ds_destroy(DS *d)              { d->ops->destroy(d); }
static inline size_t ds_size(const DS *d)           { return d->ops->size(d); }
static inline size_t ds_mem_bytes(const DS *d)      { return d->ops->mem_bytes(d); }
static inline const char *ds_policy(const DS *d)    { return d->ops->policy(d); }
static inline size_t ds_snapshot(const DS *d, long *o, size_t m) { return d->ops->snapshot(d, o, m); }
static inline void ds_set_observer(DS *d, ds_observer_fn f, void *ctx) { d->observer = f; d->observer_ctx = ctx; }
static inline void ds_reset_counters(DS *d) { d->comparisons = 0; d->adapt_steps = 0; d->policy_switches = 0; }

/* Adaptive policy tunables (slide 7: monitor -> classify -> select -> evaluate) */
#define ADL_WIN            1024     /* sliding-window length (hits)            */
#define ADL_TOPK           16       /* max hot keys tracked per window         */
#define ADL_HT_BITS        12
#define ADL_HT             (1u << ADL_HT_BITS)
#define ADL_SKEW_THRESHOLD 0.30     /* top-K share of window to call "skewed"  */
#define ADL_SHIFT_OVERLAP  0.50     /* hot-set overlap below this => "shift"   */

#endif /* ADL_H */
