/*
 * Author : Asutosh Sahoo | Veer Surendra Sai University of Technology
 * test_correctness.c : unit tests + reference model (slide 9: "Correctness").
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "adl.h"
#include "workload.h"

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static int order_is(DS *d, const long *want, size_t n)
{
    long got[32];
    size_t m = ds_snapshot(d, got, 32);
    if (m != n) return 0;
    return memcmp(got, want, n * sizeof(long)) == 0;
}

static void test_reference_model(const char *kind)
{
    enum { K = 300 };
    static int present[K];
    static long val[K];
    memset(present, 0, sizeof present);
    DS *d = ds_create(kind);
    uint64_t rng = 12345;
    size_t count = 0;
    int ok = 1;
    for (int i = 0; i < 200000; i++) {
        long key = (long)(rng_next(&rng) % K);
        int op = (int)(rng_next(&rng) % 10);
        if (op < 6) {                                   /* get */
            long v = -1;
            int f = ds_get(d, key, &v);
            if (f != present[key] || (f && v != val[key])) ok = 0;
        } else if (op < 8) {                            /* put */
            long nv = (long)(rng_next(&rng) % 1000);
            int ins = ds_put(d, key, nv);
            if (ins != !present[key]) ok = 0;
            if (!present[key]) count++;
            present[key] = 1; val[key] = nv;
        } else {                                        /* erase */
            int er = ds_erase(d, key);
            if (er != present[key]) ok = 0;
            if (present[key]) count--;
            present[key] = 0;
        }
        if (ds_size(d) != count) ok = 0;
        if (!ok) break;
    }
    printf("  reference model: %-9s %s\n", kind, ok ? "ok" : "MISMATCH");
    CHECK(ok);
    ds_destroy(d);
}

static DS *abcd(const char *kind)
{
    DS *d = ds_create(kind);
    for (long k = 1; k <= 4; k++) ds_put(d, k, k * 10);
    return d;
}

static void test_orderings(void)
{
    DS *d;
    long v;

    d = abcd("fixed");
    ds_get(d, 3, &v);
    { long w[] = {1,2,3,4}; CHECK(order_is(d, w, 4)); CHECK(v == 30); }
    ds_destroy(d);

    d = abcd("mtf");                                    /* slide 5 example */
    ds_reset_counters(d);                               /* ignore prefill cost */
    ds_get(d, 3, &v);
    { long w[] = {3,1,2,4}; CHECK(order_is(d, w, 4)); }
    ds_get(d, 3, &v);
    { long w[] = {3,1,2,4}; CHECK(order_is(d, w, 4)); }
    ds_get(d, 99, &v);                                  /* miss must not reorder */
    { long w[] = {3,1,2,4}; CHECK(order_is(d, w, 4)); }
    CHECK(d->comparisons == 3 + 1 + 4);
    ds_destroy(d);

    d = abcd("transpose");
    ds_get(d, 3, &v);
    { long w[] = {1,3,2,4}; CHECK(order_is(d, w, 4)); }
    ds_get(d, 3, &v);
    { long w[] = {3,1,2,4}; CHECK(order_is(d, w, 4)); }
    ds_destroy(d);

    d = abcd("freq");
    ds_get(d, 3, &v);
    { long w[] = {3,1,2,4}; CHECK(order_is(d, w, 4)); }
    ds_get(d, 4, &v);
    { long w[] = {3,4,1,2}; CHECK(order_is(d, w, 4)); }
    ds_get(d, 4, &v);
    { long w[] = {4,3,1,2}; CHECK(order_is(d, w, 4)); }
    ds_destroy(d);
    puts("  ordering tests done");
}

static void test_adaptive_policy(void)
{
    DS *d = ds_create("adaptive");
    for (long k = 0; k < 1000; k++) ds_put(d, k, k);
    uint64_t rng = 99;

    for (int i = 0; i < 4 * ADL_WIN; i++) {             /* skewed: 8 hot keys, 90% */
        long k = rng_double(&rng) < 0.9 ? (long)(rng_next(&rng) % 8) : (long)(rng_next(&rng) % 1000);
        ds_get(d, k, NULL);
    }
    CHECK(strstr(ds_policy(d), "[mtf]") != NULL);
    uint64_t sw = d->policy_switches;
    CHECK(sw >= 1);

    for (int i = 0; i < 4 * ADL_WIN; i++)               /* sequential scan => low skew */
        ds_get(d, (long)(i % 1000), NULL);
    CHECK(strstr(ds_policy(d), "[fixed]") != NULL);
    CHECK(d->policy_switches > sw);
    printf("  adaptive policy: switches=%llu final=%s\n",
           (unsigned long long)d->policy_switches, ds_policy(d));
    ds_destroy(d);
}

static void test_traces(void)
{
    Trace *a = trace_generate(WL_ZIPF, 500, 5000, 7);
    Trace *b = trace_generate(WL_ZIPF, 500, 5000, 7);
    Trace *c = trace_generate(WL_ZIPF, 500, 5000, 8);
    CHECK(a && b && c);
    CHECK(memcmp(a->ops, b->ops, a->len * sizeof(Op)) == 0);   /* repeatable */
    CHECK(memcmp(a->ops, c->ops, a->len * sizeof(Op)) != 0);   /* seed matters */
    int in_range = 1;
    for (size_t i = 0; i < a->len; i++) if (a->ops[i].key < 0 || a->ops[i].key >= 500) in_range = 0;
    CHECK(in_range);
    trace_free(a); trace_free(b); trace_free(c);
    puts("  trace generator: repeatable + in range");
}

int main(void)
{
    printf("ADL tests  (%s | %s)\n", ADL_AUTHOR, ADL_UNIVERSITY);
    for (size_t i = 0; i < ds_kinds_count; i++) test_reference_model(ds_kinds[i]);
    test_orderings();
    test_adaptive_policy();
    test_traces();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
