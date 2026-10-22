# Self-Organizing Data Structure — Adaptive Data Lab (ADL)

> An adaptive data layer that learns from access patterns, written in pure C (C11).
> **Observe → Adapt → Measure → Prove**

**Author:** Asutosh Sahoo
**University:** Veer Surendra Sai University of Technology (VSSUT)

**Project promise:** demonstrate *when* adaptation helps — and make its trade-offs visible.
This project does **not** claim universal speedups. Adaptive can win on one workload and lose on another.

---

## Quick start

```bash
git clone <your-repo-url>
cd self-organizing-ds

make            # build  -> build/adl
make test       # unit tests + reference-model check
make demo       # watch the list reorder itself, and the policy layer switch
make bench      # run all workloads x all structures -> results.csv, timeline.csv
```

Requirements: a C11 compiler (`gcc` or `clang`) and `make`. No external libraries (only libm).

Custom benchmark:

```bash
./build/adl bench --n 2000 --ops 500000 --seed 7 --workload zipf \
                  --csv my_results.csv --series my_timeline.csv
```

| Option | Meaning | Default |
|---|---|---|
| `--n` | number of keys stored | 1000 |
| `--ops` | operations in the trace | 100000 |
| `--seed` | RNG seed (same seed ⇒ identical trace) | 42 |
| `--workload` | `all`, `uniform`, `zipf`, `phase`, `scan` | all |
| `--csv` / `--series` | summary CSV / per-window timeline CSV | `results.csv` / `timeline.csv` |

---

## How the code follows the deck

| Deck slide | What it asks for | Where it lives |
|---|---|---|
| 3–4 Architecture | API layer, trace generator, policy, backends, metrics, report | `include/adl.h`, `src/workload.c`, `src/list.c`, `src/metrics.c`, `src/main.c` |
| 5 Core algorithm | Move-to-Front on successful lookup | `pol_mtf()` in `src/list.c` |
| 6 Design patterns | Strategy, Observer, Factory, Adapter, Command | see table below |
| 7 Policy layer | Monitor → Classify → Select → Evaluate | `mon_record()` / `mon_evaluate()` in `src/list.c` |
| 8 Test plan | Uniform, Zipf-like, phase change, scan; fixed seeds, identical traces | `src/workload.c` |
| 9 Measurement | Correctness, lookup work, speed, adaptation, cost | `tests/`, `src/metrics.c` |
| 10 Demo screen | Live comparison view | `adl demo` and the console bars in `adl bench` |
| 11 MVP | Fixed baseline, MTF, workload generator, Strategy, CSV + tests, phase change, auto policy selection | all included |

### Design patterns

| Pattern | Implementation |
|---|---|
| **Strategy** | `PolicyFn` function pointer: `pol_fixed`, `pol_mtf`, `pol_transpose`, `pol_freq` — swapped at runtime without touching the engine |
| **Observer** | `ds_set_observer()` publishes `Accessed`, `PolicyChanged`, `MetricsUpdated` events |
| **Factory** | `ds_create("fixed" \| "mtf" \| "transpose" \| "freq" \| "adaptive")` |
| **Adapter** | `DSOps` vtable exposes one `get / put / erase` API over every backend |
| **Command** | `Op {kind, key, val}` — workload operations are replayable objects |

### The adaptive policy layer (slide 7)

Every `ADL_WIN` (1024) successful lookups the engine:

1. **Monitor** — counts hits per key in the window (open-addressing hash table).
2. **Classify** — computes the share of hits captured by the hottest keys and the overlap of the hot set with the previous window → `uniform/scan`, `skewed`, or `shift`.
3. **Select** — `skewed`/`shift` ⇒ Move-to-Front on; otherwise fixed order (no reordering cost).
4. **Evaluate** — switch count, reorder steps, and wall-clock time are all measured by the benchmark, so the cost of the policy layer is visible, not hidden.

Thresholds are macros in `include/adl.h` (`ADL_SKEW_THRESHOLD`, `ADL_SHIFT_OVERLAP`, `ADL_WIN`).

### Workloads

| Name | Description |
|---|---|
| `uniform` | every key equally likely |
| `zipf` | skewed, Zipf-like (s = 1.1); a few hot keys dominate |
| `phase` | Zipf-like, but the hot set changes at the midpoint |
| `scan` | sequential traversal of all keys, repeatedly |

### Metrics (slide 9)

| Metric | Column |
|---|---|
| Correctness | `make test` (reference model) + every benchmark lookup is value-verified |
| Lookup work | `cmp_per_lookup` |
| Speed | `ops_per_sec`, `median_ns_per_op` (median of 250-op batches; median of 3 runs) |
| Adaptation | `recovery_ops` — accesses until the windowed cost returns within 1.25× of the pre-shift level (`-1` never, `-2` n/a) |
| Cost | `adapt_steps`, `policy_switches`, `mem_bytes_per_item` |

`timeline.csv` holds average comparisons per 250-op window for every run — plot it to see
the phase-change dip and recovery.

---

## Sample results

`N = 1000` keys, 100,000 operations, seed 42. **Comparisons per successful lookup are deterministic**
(identical on any machine); timings vary with hardware and load.

| Workload | fixed | mtf | transpose | freq | adaptive |
|---|---:|---:|---:|---:|---:|
| uniform | 501.34 | 500.79 | 501.39 | 501.72 | 501.34 |
| zipf | 522.51 | 139.29 | 245.39 | 103.57 | 143.26 |
| phase | 534.49 | 139.95 | 274.63 | 128.98 | 143.91 |
| scan | 500.50 | **995.00** | 500.55 | 500.50 | 500.50 |

What this shows (and what it does not):

- On skewed workloads, self-organizing lists cut lookup work by roughly 4–5× versus the fixed list.
- On a **scan**, plain Move-to-Front does about **2× the work** of the fixed list — it keeps moving the
  node that is needed furthest in the future. The adaptive layer detects low skew and avoids this.
- On `uniform`, nothing helps; differences are noise.
- Adaptive pays a small price versus always-MTF on skewed traces (warm-up window, monitor overhead) —
  the trade-off for not collapsing on scans.
- Frequency Count wins on comparisons for stable skew, but recovers slowly after the hot set changes
  (large `recovery_ops` in the `phase` workload) because old counts must be overtaken.

These are results for one list size and seed; rerun with other `--n` / `--seed` values before drawing conclusions.

---

## Project layout

```
include/   adl.h  workload.h  metrics.h
src/       list.c  workload.c  metrics.c  main.c
tests/     test_correctness.c
Makefile   .github/workflows/ci.yml
```

## Roadmap (slide 11, "if time allows")

- Splay-tree backend behind the same `DSOps` interface
- Interactive dashboard / charts from `timeline.csv`
- Memory and latency profiling beyond the current per-item estimate

## License

MIT — see [LICENSE](LICENSE).

---
Built by **Asutosh Sahoo** · Veer Surendra Sai University of Technology (VSSUT)

## Interactive web app

**[Open Adaptive Data Lab](https://asutosh-source.github.io/self-organizing-ds/)** · [Web app source](docs/index.html) · [Run and deployment guide](docs/WEB_APP.md)

The browser playground is a JavaScript demonstration. Treat its measurements as browser-simulated until validated against the native C implementation; browser timing must not be presented as native C performance.
