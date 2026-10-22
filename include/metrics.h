/* Author: Asutosh Sahoo | Veer Surendra Sai University of Technology
 * metrics.h : metrics collector + benchmark runner */
#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <stdio.h>
#include "adl.h"
#include "workload.h"

#define ADL_SERIES_WIN 250   /* ops per timing / comparison window */

typedef struct {
    char     workload[16];
    char     structure[16];
    size_t   n, ops;
    uint64_t seed;
    uint64_t lookups, comparisons, adapt_steps, policy_switches;
    double   cmp_per_lookup;     /* "lookup work"                     */
    double   total_ns;
    double   ops_per_sec;        /* "speed"                           */
    double   median_ns_per_op;   /* median over ADL_SERIES_WIN batches*/
    double   mem_per_item;       /* "cost": bytes per stored item     */
    long     recovery_ops;       /* accesses until recovery, -1 never, -2 n/a */
    double  *series;             /* avg comparisons per op per window */
    size_t   n_windows;
} RunResult;

/* Prefill keys 0..n-1, replay the trace, verify every lookup, collect metrics. */
int  run_benchmark(const char *kind, const Trace *t, RunResult *out);
void result_free(RunResult *r);
void csv_write_header(FILE *f);
void csv_write_row(FILE *f, const RunResult *r);
void series_write_header(FILE *f);
void series_write(FILE *f, const RunResult *r);

#endif
