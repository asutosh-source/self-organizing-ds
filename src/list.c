/*
 * Author : Asutosh Sahoo | Veer Surendra Sai University of Technology
 * list.c : list backends  (FixedList / MTF / Transpose / FrequencyCount / Adaptive)
 *
 * Strategy pattern : the reordering policy is a function pointer (PolicyFn)
 *                    that can be swapped without touching the engine.
 * Adaptive layer   : MONITOR (windowed hit counts) -> CLASSIFY (uniform / skewed /
 *                    shift) -> SELECT (policy) -> EVALUATE (counters + benchmark).
 */
#include "adl.h"
#include <stdlib.h>
#include <string.h>

typedef struct Node {
    long key, val;
    uint32_t freq;
    struct Node *next;
} Node;

typedef enum { POL_FIXED = 0, POL_MTF, POL_TRANSPOSE, POL_FREQ } Policy;
typedef enum { CLS_WARMUP = 0, CLS_UNIFORM, CLS_SKEWED, CLS_SHIFT } Class;

typedef struct { long key; uint32_t cnt; uint32_t stamp; } Slot;

typedef struct {
    Slot     slots[ADL_HT];
    uint32_t epoch;
    uint32_t seen;
    long     prev_top[ADL_TOPK];
    int      prev_k;
    int      prev_skewed;
    Class    cls;
} Monitor;

typedef struct ListDS ListDS;
typedef void (*PolicyFn)(ListDS *, Node *cur, Node *pred, Node *predpred);

struct ListDS {
    DS       base;
    Node    *head;
    size_t   n;
    Policy   policy;
    PolicyFn policy_fn;
    Monitor *mon;        /* non-NULL only for the adaptive backend */
};

static void emit(DS *d, const char *ev, long key, long aux)
{
    if (d->observer) d->observer(d->observer_ctx, ev, key, aux);
}

/* ---------- reordering policies (Strategy) ---------- */

static void pol_fixed(ListDS *l, Node *c, Node *p, Node *pp)
{ (void)l; (void)c; (void)p; (void)pp; }

static void pol_mtf(ListDS *l, Node *c, Node *p, Node *pp)
{
    (void)pp;
    if (!p) return;                  /* already at the front */
    p->next = c->next;
    c->next = l->head;
    l->head = c;
    l->base.adapt_steps++;
}

static void pol_transpose(ListDS *l, Node *c, Node *p, Node *pp)
{
    if (!p) return;                  /* already at the front */
    p->next = c->next;
    c->next = p;
    if (pp) pp->next = c; else l->head = c;
    l->base.adapt_steps++;
}

static void pol_freq(ListDS *l, Node *c, Node *p, Node *pp)
{
    (void)pp;
    c->freq++;
    if (!p || p->freq >= c->freq) return;
    p->next = c->next;               /* unlink */
    Node *q = NULL, *cur = l->head;  /* re-insert before first node with lower freq */
    while (cur && cur->freq >= c->freq) { l->base.adapt_steps++; q = cur; cur = cur->next; }
    c->next = cur;
    if (q) q->next = c; else l->head = c;
}

static const PolicyFn POLICY_FN[] = { pol_fixed, pol_mtf, pol_transpose, pol_freq };

static void set_policy(ListDS *l, Policy p)
{
    l->policy = p;
    l->policy_fn = POLICY_FN[p];
}

/* ---------- adaptive monitor ---------- */

static const char *ADAPTIVE_LABEL[2][4] = {
    { "adaptive[fixed]/warmup", "adaptive[fixed]/uniform-or-scan",
      "adaptive[fixed]/skewed", "adaptive[fixed]/shift" },
    { "adaptive[mtf]/warmup",   "adaptive[mtf]/uniform-or-scan",
      "adaptive[mtf]/skewed",   "adaptive[mtf]/shift" }
};

static void mon_evaluate(ListDS *l)
{
    Monitor *m = l->mon;
    long     top_key[ADL_TOPK];
    uint32_t top_cnt[ADL_TOPK];
    unsigned distinct = 0;

    for (unsigned i = 0; i < ADL_HT; i++)
        if (m->slots[i].stamp == m->epoch) distinct++;

    int K = (int)(distinct / 10);
    if (K < 1) K = 1;
    if (K > ADL_TOPK) K = ADL_TOPK;

    int filled = 0;
    for (unsigned i = 0; i < ADL_HT; i++) {
        const Slot *s = &m->slots[i];
        if (s->stamp != m->epoch) continue;
        uint32_t c = s->cnt;
        if (filled == K && c <= top_cnt[K - 1]) continue;
        int j = (filled < K) ? filled++ : K - 1;
        while (j > 0 && top_cnt[j - 1] < c) {
            top_cnt[j] = top_cnt[j - 1];
            top_key[j] = top_key[j - 1];
            j--;
        }
        top_cnt[j] = c;
        top_key[j] = s->key;
    }

    uint32_t covered = 0;
    for (int i = 0; i < filled; i++) covered += top_cnt[i];
    double share = m->seen ? (double)covered / (double)m->seen : 0.0;

    double overlap = 1.0;
    if (m->prev_k > 0 && filled > 0) {
        int match = 0;
        for (int i = 0; i < filled; i++)
            for (int j = 0; j < m->prev_k; j++)
                if (top_key[i] == m->prev_top[j]) { match++; break; }
        overlap = (double)match / (double)filled;
    }

    int skewed = share >= ADL_SKEW_THRESHOLD;
    Class cls = !skewed ? CLS_UNIFORM
              : (m->prev_skewed && overlap < ADL_SHIFT_OVERLAP) ? CLS_SHIFT : CLS_SKEWED;
    Policy want = skewed ? POL_MTF : POL_FIXED;

    emit(&l->base, "MetricsUpdated", (long)(share * 1000.0), (long)(overlap * 1000.0));
    if (want != l->policy) {
        set_policy(l, want);
        l->base.policy_switches++;
        emit(&l->base, "PolicyChanged", 0, (long)want);
    }
    m->cls = cls;

    for (int i = 0; i < filled; i++) m->prev_top[i] = top_key[i];
    m->prev_k = filled;
    m->prev_skewed = skewed;
    m->epoch++;
    m->seen = 0;
}

static void mon_record(ListDS *l, long key)
{
    Monitor *m = l->mon;
    size_t h = (size_t)(((uint64_t)key * 0x9E3779B97F4A7C15ULL) >> (64 - ADL_HT_BITS));
    for (;;) {
        Slot *s = &m->slots[h];
        if (s->stamp != m->epoch) { s->stamp = m->epoch; s->key = key; s->cnt = 1; break; }
        if (s->key == key)        { s->cnt++; break; }
        h = (h + 1) & (ADL_HT - 1);
    }
    if (++m->seen >= ADL_WIN) mon_evaluate(l);
}

/* ---------- core list operations ---------- */

static Node *find(ListDS *l, long key, Node **pred, Node **predpred)
{
    Node *p = NULL, *pp = NULL, *c = l->head;
    while (c) {
        l->base.comparisons++;
        if (c->key == key) { *pred = p; *predpred = pp; return c; }
        pp = p; p = c; c = c->next;
    }
    *pred = p; *predpred = pp;      /* on a miss, pred is the tail */
    return NULL;
}

static int list_get(DS *d, long key, long *out)
{
    ListDS *l = (ListDS *)d;
    Node *p, *pp;
    uint64_t before = d->comparisons;
    Node *c = find(l, key, &p, &pp);
    if (!c) return 0;
    if (out) *out = c->val;
    emit(d, "Accessed", key, (long)(d->comparisons - before));
    l->policy_fn(l, c, p, pp);
    if (l->mon) mon_record(l, key);
    return 1;
}

static int list_put(DS *d, long key, long value)
{
    ListDS *l = (ListDS *)d;
    Node *p, *pp;
    Node *c = find(l, key, &p, &pp);
    if (c) { c->val = value; return 0; }
    Node *nn = (Node *)calloc(1, sizeof *nn);
    if (!nn) return 0;
    nn->key = key; nn->val = value;
    if (p) p->next = nn; else l->head = nn;     /* append at the tail */
    l->n++;
    return 1;
}

static int list_erase(DS *d, long key)
{
    ListDS *l = (ListDS *)d;
    Node *p, *pp;
    Node *c = find(l, key, &p, &pp);
    if (!c) return 0;
    if (p) p->next = c->next; else l->head = c->next;
    free(c);
    l->n--;
    return 1;
}

static void list_destroy(DS *d)
{
    ListDS *l = (ListDS *)d;
    Node *c = l->head;
    while (c) { Node *nx = c->next; free(c); c = nx; }
    free(l->mon);
    free(l);
}

static size_t list_size(const DS *d) { return ((const ListDS *)d)->n; }

static size_t list_mem(const DS *d)
{
    const ListDS *l = (const ListDS *)d;
    return sizeof *l + l->n * sizeof(Node) + (l->mon ? sizeof(Monitor) : 0);
}

static size_t list_snapshot(const DS *d, long *out, size_t max)
{
    size_t i = 0;
    for (const Node *c = ((const ListDS *)d)->head; c && i < max; c = c->next) out[i++] = c->key;
    return i;
}

static const char *list_policy(const DS *d)
{
    const ListDS *l = (const ListDS *)d;
    static const char *names[] = { "fixed", "mtf", "transpose", "freq" };
    if (l->mon) return ADAPTIVE_LABEL[l->policy == POL_MTF][l->mon->cls];
    return names[l->policy];
}

/* One ops table per backend kind (Adapter). */
#define MAKE_OPS(var, nm) \
    static const DSOps var = { nm, list_get, list_put, list_erase, list_destroy, \
                               list_mem, list_size, list_snapshot, list_policy }
MAKE_OPS(OPS_FIXED, "fixed");
MAKE_OPS(OPS_MTF, "mtf");
MAKE_OPS(OPS_TRANSPOSE, "transpose");
MAKE_OPS(OPS_FREQ, "freq");
MAKE_OPS(OPS_ADAPTIVE, "adaptive");

const char *const ds_kinds[] = { "fixed", "mtf", "transpose", "freq", "adaptive" };
const size_t ds_kinds_count = sizeof ds_kinds / sizeof ds_kinds[0];

/* Factory pattern */
DS *ds_create(const char *kind)
{
    const DSOps *ops = NULL;
    Policy pol = POL_FIXED;
    int adaptive = 0;

    if      (!strcmp(kind, "fixed"))     { ops = &OPS_FIXED; }
    else if (!strcmp(kind, "mtf"))       { ops = &OPS_MTF;       pol = POL_MTF; }
    else if (!strcmp(kind, "transpose")) { ops = &OPS_TRANSPOSE; pol = POL_TRANSPOSE; }
    else if (!strcmp(kind, "freq"))      { ops = &OPS_FREQ;      pol = POL_FREQ; }
    else if (!strcmp(kind, "adaptive"))  { ops = &OPS_ADAPTIVE;  adaptive = 1; }
    else return NULL;

    ListDS *l = (ListDS *)calloc(1, sizeof *l);
    if (!l) return NULL;
    l->base.ops = ops;
    set_policy(l, pol);
    if (adaptive) {
        l->mon = (Monitor *)calloc(1, sizeof *l->mon);
        if (!l->mon) { free(l); return NULL; }
        l->mon->epoch = 1;
        l->mon->cls = CLS_WARMUP;
    }
    return &l->base;
}
