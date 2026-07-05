# Performance Targets & Regression Thresholds

The numeric performance contract for the benchmarked hot paths. It turns the qualitative spec
language ("ultra-fast lookups", "O(1) object pool") into two kinds of checkable target, and
states the methodology every run must follow. It builds on
[ADR-0027](../adr/0027-benchmark-suite-stopwatch-harness.md) (the dependency-free `Stopwatch`
harness) and the [baseline report](2026-07-05-suite-baseline.md); the general rules are in
[`README.md`](README.md).

## Why two kinds of target

Absolute nanosecond numbers are machine-specific and shared CI runners are too noisy to gate on
(ADR-0027). So the contract separates the *durable, machine-independent* guarantee from the
*same-machine* regression check:

1. **Algorithmic-class invariants (machine-independent, always in force).** The complexity class
   and the zero/one-time-allocation property of each hot path. These do not depend on the CPU and
   are the guarantees a consumer actually relies on. A violation (e.g. a lookup that starts scaling
   linearly, or an alloc that starts touching the heap) is a defect regardless of the clock.
2. **Regression thresholds (relative, on comparable hardware).** A per-operation median that must
   not exceed **1.25×** the recorded baseline for that scenario **on the same machine class**, with
   a stability guard of **p99 ≤ 2× median**. These catch same-machine regressions; they are checked
   by hand against the baseline on comparable hardware, not gated on CI runners.

## Methodology (every run must follow)

- **Harness:** the library's own `Stopwatch` via `benchmark_harness.hpp`; build and run with
  `cmake --preset bench && cmake --build --preset bench` then execute `util_bench`.
- **Build:** Release (`-O2`/`/O2`), single-threaded, `do_not_optimize` around the measured work to
  defeat dead-code elimination.
- **Sampling:** one **un-timed warm-up** rep, then **64 timed reps** of **2¹⁶ operations** (2¹⁴ for
  `StringBuilder`); report the **per-operation median and p99** across reps and derive throughput
  from the median.
- **Record with every result:** CPU / RAM / OS, toolchain version, and the commit SHA — a number
  without its environment is not comparable.
- **Reference machine (baseline):** Intel Core i5-6600K @ 3.50 GHz (4C/4T), 32 GB, Windows 10
  19045, MSVC 19.51 (x64), Release. Baseline commit: `perf/benchmark-suite` (v0.0.0 pre-1.0).

## Targets

Eight scenarios across eight performance-critical components (satisfies the "≥5 modules with
numeric targets" bar). Baseline medians are from the [reference machine](2026-07-05-suite-baseline.md);
thresholds are relative to those.

| Scenario (component) | Baseline median | Regression threshold (ref machine) | Algorithmic-class invariant (machine-independent) |
|---|---|---|---|
| `hash::fnv1a_64` (64 B) — **HashAlgorithms** | 57.83 ns | ≤ 72 ns | O(n) in bytes, ~≤ 1 ns/byte; no allocation |
| `FlatMap<int,int>::contains` (1024) — **FlatMap** | 27.41 ns | ≤ 34 ns | **O(log n)** — doubling entries adds ~one comparison, not linear time |
| `ObjectPool<int>` acquire+release — **ObjectPool** | 33.85 ns | ≤ 42 ns | **O(1)**, no runtime allocation (storage reused) |
| `LockFreeQueue<int>` push+pop, 1 thread — **LockFreeQueue** | 12.88 ns | ≤ 16 ns | **O(1)** per op, no allocation on the hot path |
| `CircularBuffer<int>` push+pop — **CircularBuffer** | 3.06 ns | ≤ 4 ns | **O(1)**, no allocation (fixed ring) |
| `StackAllocator` reset+allocate(64 B) — **StackAllocator** | 1.67 ns | ≤ 2.5 ns | **O(1)** bump, **zero heap** up to `Size` |
| `StringBuilder::append` (4 B) — **StringBuilder** | 4.12 ns | ≤ 5 ns | **amortized O(1)** per char; no per-append allocation after `reserve` |
| `BinarySerializer` u64+f64 round-trip — **BinarySerializer** | 3.20 ns | ≤ 4 ns | **O(1)** per scalar; writes into a caller-owned span (no allocation) |

**Not targeted (deliberately).** The OS-boundary components (`FileStream`, `TcpSocket`/`TcpServer`,
`Logger`) are kernel/network-bound, not micro-benchmarkable into a stable per-op number (ADR-0027);
their contract is correctness + the async/non-blocking behavior verified by tests, not a nanosecond
target.

## Enforcement

- **Algorithmic-class invariants** are enforced by design + the unit tests that assert behavior
  (e.g. `FlatMap` uses `std::lower_bound`); a change that would break the class is caught in review
  against the [contract table](../architecture/component-contracts.md).
- **Regression thresholds** are checked manually: re-run the suite on comparable hardware, compare
  each median to `1.25×` its baseline and each p99 to `2×` its median, and record the run as a new
  dated report. The CI `benchmark` job **builds** the suite (so it never rots) but does not gate on
  timings.
- A confirmed regression past threshold is a `docs/bugs/` entry (performance defect) unless an ADR
  justifies the new cost.
