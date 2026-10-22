/*
 * Author : Asutosh Sahoo | Veer Surendra Sai University of Technology
 * metrics.c : metrics collector -- comparisons, time, overhead, recovery, CSV.
 */
#define _POSIX_C_SOURCE 200809L
#include "metrics.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static long value_for(long key) { return key * 7 + 1; }

/* Slide 9: "Accesses until performance recovers after a shift". */
static long compute_recovery(const Trace *t, const double *series, size_t nw)
{
    if (t->shift_at == 0) return -2;
    size_t sw = t->shift_at / ADL_SERIES_WIN;
    if (sw < 4 || sw >= nw) return -2;
    double base = 0.0;
    for (size_t i = sw - 4; i < sw; i++) base += series[i];
    base /= 4.0;
    double limit = base * 1.25;
    for (size_t w = sw; w < nw; w++)
        if (series[w] <= limit) return (long)((w - sw) * ADL_SERIES_WIN);
    return -1;
}

static int run_once(const char *kind, const Trace *t, RunResult *r)
{
    DS *d = ds_create(kind);
    if (!d) return 0;
    for (size_t k = 0; k < t->n_keys; k++) ds_put(d, (long)k, value_for((long)k));
    ds_reset_counters(d);

    size_t nw = t->len / ADL_SERIES_WIN;
    if (nw == 0) nw = 1;
    double *series = (double *)calloc(nw, sizeof(double));
    double *batch_ns = (double *)calloc(nw, sizeof(double));
    if (!series || !batch_ns) { free(series); free(batch_ns); ds_destroy(d); return 0; }

    memset(r, 0, sizeof *r);
    uint64_t sink = 0;
    double total = 0.0;
    size_t done = 0;

    for (size_t w = 0; w < nw; w++) {
        size_t begin = w * ADL_SERIES_WIN;
        size_t end = (w + 1 == nw) ? t->len : begin + ADL_SERIES_WIN;
        uint64_t c0 = d->comparisons;
        double t0 = now_ns();
        for (size_t i = begin; i < end; i++) {
            const Op *op = &t->ops[i];
            long v = 0;
            if (op->kind == OP_GET) {
                if (!ds_get(d, op->key, &v) || v != value_for(op->key)) {
                    free(series); free(batch_ns); ds_destroy(d);
                    return 0;                       /* correctness failure */
                }
                sink += (uint64_t)v;
            } else if (op->kind == OP_PUT) {
                ds_put(d, op->key, op->val);
            } else {
                ds_erase(d, op->key);
            }
        }
        double dt = now_ns() - t0;
        size_t cnt = end - begin;
        batch_ns[w] = dt / (double)cnt;
        series[w] = (double)(d->comparisons - c0) / (double)cnt;
        total += dt;
        done += cnt;
    }
    (void)sink;

    qsort(batch_ns, nw, sizeof(double), cmp_double);
    strncpy(r->structure, kind, sizeof r->structure - 1);
    strncpy(r->workload, workload_name(t->kind), sizeof r->workload - 1);
    r->n = t->n_keys; r->ops = t->len; r->seed = t->seed;
    r->lookups = done;
    r->comparisons = d->comparisons;
    r->adapt_steps = d->adapt_steps;
    r->policy_switches = d->policy_switches;
    r->cmp_per_lookup = (double)d->comparisons / (double)done;
    r->total_ns = total;
    r->ops_per_sec = total > 0 ? (double)done / (total / 1e9) : 0.0;
    r->median_ns_per_op = batch_ns[nw / 2];
    r->mem_per_item = (double)ds_mem_bytes(d) / (double)ds_size(d);
    r->recovery_ops = compute_recovery(t, series, nw);
    r->series = series;
    r->n_windows = nw;
    free(batch_ns);
    ds_destroy(d);
    return 1;
}

static int by_total_ns(const void *a, const void *b)
{
    return cmp_double(&((const RunResult *)a)->total_ns, &((const RunResult *)b)->total_ns);
}

/* Three repetitions on the identical trace; keep the median-time run. */
int run_benchmark(const char *kind, const Trace *t, RunResult *out)
{
    enum { REPS = 3 };
    RunResult rr[REPS];
    for (int i = 0; i < REPS; i++)
        if (!run_once(kind, t, &rr[i])) {
            for (int j = 0; j < i; j++) result_free(&rr[j]);
            return 0;
        }
    qsort(rr, REPS, sizeof rr[0], by_total_ns);
    *out = rr[REPS / 2];
    for (int i = 0; i < REPS; i++) if (i != REPS / 2) result_free(&rr[i]);
    return 1;
}

void result_free(RunResult *r) { free(r->series); r->series = NULL; }

void csv_write_header(FILE *f)
{
    fprintf(f, "workload,structure,n_keys,ops,seed,comparisons,cmp_per_lookup,"
               "adapt_steps,policy_switches,total_ms,ops_per_sec,median_ns_per_op,"
               "mem_bytes_per_item,recovery_ops\n");
}

void csv_write_row(FILE *f, const RunResult *r)
{
    fprintf(f, "%s,%s,%zu,%zu,%llu,%llu,%.4f,%llu,%llu,%.3f,%.0f,%.2f,%.1f,%ld\n",
            r->workload, r->structure, r->n, r->ops, (unsigned long long)r->seed,
            (unsigned long long)r->comparisons, r->cmp_per_lookup,
            (unsigned long long)r->adapt_steps, (unsigned long long)r->policy_switches,
            r->total_ns / 1e6, r->ops_per_sec, r->median_ns_per_op,
            r->mem_per_item, r->recovery_ops);
}

void series_write_header(FILE *f) { fprintf(f, "workload,structure,window,ops_end,avg_comparisons\n"); }

void series_write(FILE *f, const RunResult *r)
{
    for (size_t w = 0; w < r->n_windows; w++)
        fprintf(f, "%s,%s,%zu,%zu,%.4f\n", r->workload, r->structure, w,
                (w + 1) * ADL_SERIES_WIN, r->series[w]);
}
