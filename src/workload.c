/*
 * Author : Asutosh Sahoo | Veer Surendra Sai University of Technology
 * workload.c : trace generator -- uniform, skewed (Zipf-like), phase change, scan.
 * Fixed seeds + identical traces for every implementation (slide 8).
 */
#include "workload.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

uint64_t rng_next(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double rng_double(uint64_t *s) { return (double)(rng_next(s) >> 11) * (1.0 / 9007199254740992.0); }

static const char *const NAMES[WL_COUNT] = { "uniform", "zipf", "phase", "scan" };

const char *workload_name(WorkloadKind k) { return NAMES[k]; }

int workload_parse(const char *s, WorkloadKind *out)
{
    for (int i = 0; i < WL_COUNT; i++)
        if (!strcmp(s, NAMES[i])) { *out = (WorkloadKind)i; return 1; }
    return 0;
}

static void shuffle(long *a, size_t n, uint64_t *rng)
{
    for (size_t i = 0; i < n; i++) a[i] = (long)i;
    for (size_t i = n; i > 1; i--) {
        size_t j = (size_t)(rng_next(rng) % i);
        long t = a[i - 1]; a[i - 1] = a[j]; a[j] = t;
    }
}

/* Zipf(s=1.1) inverse-CDF sampler over ranks 0..n-1. */
static size_t zipf_sample(const double *cdf, size_t n, uint64_t *rng)
{
    double u = rng_double(rng);
    size_t lo = 0, hi = n - 1;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cdf[mid] < u) lo = mid + 1; else hi = mid;
    }
    return lo;
}

Trace *trace_generate(WorkloadKind kind, size_t n_keys, size_t n_ops, uint64_t seed)
{
    if (n_keys == 0 || n_ops == 0) return NULL;
    Trace *t = (Trace *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->ops = (Op *)calloc(n_ops, sizeof(Op));   /* zeroed: padding stays deterministic */
    double *cdf = (double *)malloc(n_keys * sizeof(double));
    long *perm1 = (long *)malloc(n_keys * sizeof(long));
    long *perm2 = (long *)malloc(n_keys * sizeof(long));
    if (!t->ops || !cdf || !perm1 || !perm2) {
        free(t->ops); free(cdf); free(perm1); free(perm2); free(t);
        return NULL;
    }
    t->len = n_ops; t->n_keys = n_keys; t->seed = seed; t->kind = kind;

    uint64_t rng = seed ? seed : 1;
    shuffle(perm1, n_keys, &rng);   /* rank -> key (hot set A) */
    shuffle(perm2, n_keys, &rng);   /* rank -> key (hot set B) */

    double sum = 0.0;
    for (size_t r = 0; r < n_keys; r++) {
        sum += pow((double)(r + 1), -1.1);   /* Zipf exponent s = 1.1 */
        cdf[r] = sum;
    }
    for (size_t r = 0; r < n_keys; r++) cdf[r] /= sum;

    if (kind == WL_PHASE) t->shift_at = (n_ops / 2 / 250) * 250;

    for (size_t i = 0; i < n_ops; i++) {
        long key = 0;
        switch (kind) {
        case WL_UNIFORM: key = (long)(rng_next(&rng) % n_keys); break;
        case WL_ZIPF:    key = perm1[zipf_sample(cdf, n_keys, &rng)]; break;
        case WL_PHASE:   key = (i < t->shift_at ? perm1 : perm2)[zipf_sample(cdf, n_keys, &rng)]; break;
        case WL_SCAN:    key = (long)(i % n_keys); break;
        default: break;
        }
        t->ops[i].kind = OP_GET;
        t->ops[i].key = key;
        t->ops[i].val = 0;
    }
    free(cdf); free(perm1); free(perm2);
    return t;
}

void trace_free(Trace *t) { if (t) { free(t->ops); free(t); } }
