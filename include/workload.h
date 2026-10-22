/* Author: Asutosh Sahoo | Veer Surendra Sai University of Technology
 * workload.h : repeatable operation streams (Trace Generator + Command pattern) */
#ifndef WORKLOAD_H
#define WORKLOAD_H

#include <stddef.h>
#include <stdint.h>

typedef enum { OP_GET, OP_PUT, OP_ERASE } OpKind;

/* Command pattern: an operation is a replayable object. */
typedef struct { OpKind kind; long key; long val; } Op;

typedef enum { WL_UNIFORM, WL_ZIPF, WL_PHASE, WL_SCAN, WL_COUNT } WorkloadKind;

typedef struct {
    Op          *ops;
    size_t       len;
    size_t       n_keys;
    uint64_t     seed;
    WorkloadKind kind;
    size_t       shift_at;   /* op index where the hot set changes (0 = none) */
} Trace;

/* Deterministic RNG (splitmix64) so every structure sees the identical trace. */
uint64_t rng_next(uint64_t *state);
double   rng_double(uint64_t *state);

const char *workload_name(WorkloadKind k);
int         workload_parse(const char *s, WorkloadKind *out);
Trace      *trace_generate(WorkloadKind k, size_t n_keys, size_t n_ops, uint64_t seed);
void        trace_free(Trace *t);

#endif
