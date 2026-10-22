/*
 * Self-Organizing Data Structure -- Adaptive Data Lab (ADL)
 * Author : Asutosh Sahoo
 * Univ.  : Veer Surendra Sai University of Technology (VSSUT)
 *
 * Usage:  adl demo
 *         adl bench [--n N] [--ops M] [--seed S] [--workload all|uniform|zipf|phase|scan]
 *                   [--csv FILE] [--series FILE]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adl.h"
#include "metrics.h"
#include "workload.h"

static void banner(void)
{
    puts("==============================================================");
    puts("  SELF-ORGANIZING DATA STRUCTURE  --  Adaptive Data Lab");
    puts("  Observe -> Adapt -> Measure -> Prove");
    printf("  Author     : %s\n", ADL_AUTHOR);
    printf("  University : %s\n", ADL_UNIVERSITY);
    puts("==============================================================");
}

static void usage(void)
{
    puts("usage:");
    puts("  adl demo");
    puts("  adl bench [--n N] [--ops M] [--seed S] [--workload all|uniform|zipf|phase|scan]");
    puts("            [--csv FILE] [--series FILE]");
}

/* ---------------- demo: make adaptation visible ---------------- */

static void print_list(const DS *d)
{
    long keys[16];
    size_t n = ds_snapshot(d, keys, 16);
    printf("[");
    for (size_t i = 0; i < n; i++) printf("%s%c", i ? " -> " : "", (char)('A' + keys[i] - 1));
    printf("]");
}

static void demo_policy(const char *kind)
{
    DS *d = ds_create(kind);
    for (long k = 1; k <= 4; k++) ds_put(d, k, k);
    printf("\n%-9s start        ", kind); print_list(d); putchar('\n');
    const long seq[] = { 3, 3, 4, 4, 2 };
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        uint64_t before = d->comparisons;
        ds_get(d, seq[i], NULL);
        printf("%-9s access %c (%llu cmp) ", kind, (char)('A' + seq[i] - 1),
               (unsigned long long)(d->comparisons - before));
        print_list(d); putchar('\n');
    }
    ds_destroy(d);
}

static void demo_observer(void *ctx, const char *ev, long key, long aux)
{
    (void)ctx; (void)key;
    if (!strcmp(ev, "PolicyChanged"))
        printf("    >> PolicyChanged -> %s\n", aux == 1 ? "move-to-front" : "fixed (no reordering)");
    else if (!strcmp(ev, "MetricsUpdated"))
        printf("    window: top-hot-keys share = %.1f%%, hot-set overlap with previous = %.0f%%%s\n",
               (double)key / 10.0, (double)aux / 10.0,
               (key >= 300 && aux < 500) ? "  <- hot-set change" : "");
}

static void run_demo(void)
{
    puts("\n-- Move-to-Front on [A,B,C,D]: access C (slide 5) --");
    demo_policy("mtf");
    puts("\n-- Transpose: accessed node swaps one step forward --");
    demo_policy("transpose");
    puts("\n-- Frequency Count: list kept sorted by hit count --");
    demo_policy("freq");

    puts("\n-- Adaptive policy layer: monitor -> classify -> select (slide 7) --");
    DS *d = ds_create("adaptive");
    for (long k = 0; k < 1000; k++) ds_put(d, k, k);
    ds_set_observer(d, demo_observer, NULL);
    uint64_t rng = 7;

    puts("  phase 1: uniform accesses");
    for (int i = 0; i < 2 * ADL_WIN; i++) ds_get(d, (long)(rng_next(&rng) % 1000), NULL);
    printf("  state: %s\n", ds_policy(d));

    puts("  phase 2: hot set {10..17} takes 90% of accesses");
    for (int i = 0; i < 3 * ADL_WIN; i++) {
        long k = (rng_double(&rng) < 0.9) ? 10 + (long)(rng_next(&rng) % 8)
                                          : (long)(rng_next(&rng) % 1000);
        ds_get(d, k, NULL);
    }
    printf("  state: %s\n", ds_policy(d));

    puts("  phase 3: hot set shifts to {500..507}");
    for (int i = 0; i < 3 * ADL_WIN; i++) {
        long k = (rng_double(&rng) < 0.9) ? 500 + (long)(rng_next(&rng) % 8)
                                          : (long)(rng_next(&rng) % 1000);
        ds_get(d, k, NULL);
    }
    printf("  state: %s\n", ds_policy(d));

    puts("  phase 4: back to uniform accesses");
    for (int i = 0; i < 3 * ADL_WIN; i++) ds_get(d, (long)(rng_next(&rng) % 1000), NULL);
    printf("  state: %s   (policy switches: %llu)\n", ds_policy(d),
           (unsigned long long)d->policy_switches);
    ds_destroy(d);
    puts("\nMake the adaptation visible; do not claim universal speedups.");
}

/* ---------------- benchmark ---------------- */

static void print_bar(double v, double max)
{
    int w = max > 0 ? (int)(30.0 * v / max + 0.5) : 0;
    putchar('|');
    for (int i = 0; i < w; i++) putchar('#');
    putchar('\n');
}

static int run_bench(size_t n, size_t ops, uint64_t seed, int only, const char *csv, const char *series)
{
    FILE *fc = csv ? fopen(csv, "w") : NULL;
    FILE *fs = series ? fopen(series, "w") : NULL;
    if ((csv && !fc) || (series && !fs)) {
        fprintf(stderr, "error: cannot open output file\n");
        if (fc) fclose(fc);
        if (fs) fclose(fs);
        return 1;
    }
    if (fc) csv_write_header(fc);
    if (fs) series_write_header(fs);

    for (int w = 0; w < WL_COUNT; w++) {
        if (only >= 0 && w != only) continue;
        Trace *t = trace_generate((WorkloadKind)w, n, ops, seed);
        if (!t) { fprintf(stderr, "error: trace generation failed\n"); return 1; }

        RunResult res[8];
        size_t nr = 0;
        double max_cmp = 0.0;
        for (size_t s = 0; s < ds_kinds_count; s++) {
            if (!run_benchmark(ds_kinds[s], t, &res[nr])) {
                fprintf(stderr, "error: benchmark failed for %s (correctness check)\n", ds_kinds[s]);
                return 1;
            }
            if (res[nr].cmp_per_lookup > max_cmp) max_cmp = res[nr].cmp_per_lookup;
            if (fc) csv_write_row(fc, &res[nr]);
            if (fs) series_write(fs, &res[nr]);
            nr++;
        }

        printf("\n== workload: %-8s  N=%zu keys  trace=%zu ops  seed=%llu ==\n",
               workload_name((WorkloadKind)w), n, ops, (unsigned long long)seed);
        printf("%-10s %10s %9s %9s %9s %8s %7s  %s\n", "structure", "cmp/lookup",
               "Mops/s", "med ns/op", "recovery", "switches", "B/item", "lookup work");
        size_t best_cmp = 0, best_time = 0;
        for (size_t i = 0; i < nr; i++) {
            const RunResult *r = &res[i];
            char rec[16];
            if (r->recovery_ops == -2) snprintf(rec, sizeof rec, "n/a");
            else if (r->recovery_ops == -1) snprintf(rec, sizeof rec, "never");
            else snprintf(rec, sizeof rec, "%ld", r->recovery_ops);
            printf("%-10s %10.2f %9.2f %9.1f %9s %8llu %7.1f  ", r->structure, r->cmp_per_lookup,
                   r->ops_per_sec / 1e6, r->median_ns_per_op, rec,
                   (unsigned long long)r->policy_switches, r->mem_per_item);
            print_bar(r->cmp_per_lookup, max_cmp);
            if (r->cmp_per_lookup < res[best_cmp].cmp_per_lookup) best_cmp = i;
            if (r->ops_per_sec > res[best_time].ops_per_sec) best_time = i;
        }
        printf("  fewest comparisons: %s | highest throughput: %s\n",
               res[best_cmp].structure, res[best_time].structure);
        for (size_t i = 0; i < nr; i++) result_free(&res[i]);
        trace_free(t);
    }
    if (fc) { fclose(fc); printf("\nwrote %s\n", csv); }
    if (fs) { fclose(fs); printf("wrote %s\n", series); }
    puts("\nReport results, not promises: adaptive can win on one workload and lose on another.");
    puts("(comparisons are deterministic; timings vary by machine and load)");
    return 0;
}

int main(int argc, char **argv)
{
    banner();
    if (argc < 2) { usage(); return 1; }
    if (!strcmp(argv[1], "demo")) { run_demo(); return 0; }
    if (strcmp(argv[1], "bench")) { usage(); return 1; }

    size_t n = 1000, ops = 100000;
    uint64_t seed = 42;
    int only = -1;
    const char *csv = "results.csv", *series = "timeline.csv";

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (i + 1 >= argc) { usage(); return 1; }
        const char *v = argv[++i];
        if      (!strcmp(a, "--n"))      n = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--ops"))    ops = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--seed"))   seed = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--csv"))    csv = v;
        else if (!strcmp(a, "--series")) series = v;
        else if (!strcmp(a, "--workload")) {
            if (!strcmp(v, "all")) only = -1;
            else { WorkloadKind k; if (!workload_parse(v, &k)) { usage(); return 1; } only = (int)k; }
        } else { usage(); return 1; }
    }
    if (n < 2 || ops < 4 * ADL_SERIES_WIN) {
        fprintf(stderr, "error: need --n >= 2 and --ops >= %d\n", 4 * ADL_SERIES_WIN);
        return 1;
    }
    return run_bench(n, ops, seed, only, csv, series);
}
