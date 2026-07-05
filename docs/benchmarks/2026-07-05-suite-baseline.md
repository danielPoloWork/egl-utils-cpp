# Benchmark Report: suite baseline (8 header-only hot paths)

- **Date:** 2026-07-05
- **Version / commit:** v0.0.0 @ `perf/benchmark-suite` (roadmap 10.2)
- **Environment:** Intel Core i5-6600K @ 3.50 GHz (4C/4T), 32 GB RAM, Windows 10 Pro 19045,
  MSVC cl 19.51.36247 (x64), CMake `bench` preset (Release `-O2`), single-threaded.
- **Command:** `cmake --preset bench && cmake --build --preset bench` then `./build/bench/util_bench`

## Scenario

First reproducible baseline for the eight performance-critical, header-only hot paths, measured
through the library's own `Stopwatch` harness (`benchmark_harness.hpp`). Each scenario runs an
un-timed warm-up rep, then 64 timed reps of 2^16 operations (2^14 for `StringBuilder`); the
harness reports the **per-operation median and p99** across reps and derives throughput from the
median. `do_not_optimize` keeps the compiler from eliding the measured work.

The suite substantiates the spec's qualitative performance language (e.g. "ultra-fast lookups"
for `FlatMap`, "O(1) object pool") with concrete numbers, and is the baseline future runs are
compared against for regressions.

## Results

| Scenario | Median | p99 | Throughput |
|----------|--------|-----|------------|
| `hash::fnv1a_64` (64 B window) | 57.83 ns | 78.18 ns | 17.3 M ops/s |
| `FlatMap<int,int>::contains` (1024 entries) | 27.41 ns | 32.71 ns | 36.5 M ops/s |
| `ObjectPool<int>` acquire+release | 33.85 ns | 51.79 ns | 29.5 M ops/s |
| `LockFreeQueue<int>` push+pop (single thread) | 12.88 ns | 15.32 ns | 77.6 M ops/s |
| `CircularBuffer<int>` push+pop | 3.06 ns | 4.38 ns | 327.2 M ops/s |
| `StackAllocator` reset+allocate(64 B) | 1.67 ns | 2.44 ns | 599.6 M ops/s |
| `StringBuilder::append` (4 B) | 4.12 ns | 4.93 ns | 242.7 M ops/s |
| `BinarySerializer` u64+f64 round-trip | 3.20 ns | 3.79 ns | 313.0 M ops/s |

## Interpretation

- **Sub-nanosecond-class primitives.** `StackAllocator` bump allocation (~1.7 ns),
  `CircularBuffer` push+pop (~3 ns), `BinarySerializer` round-trip (~3.2 ns), and `StringBuilder`
  append (~4 ns) are all a handful of machine instructions — no allocation on the hot path — as
  intended for the zero-allocation, contiguous-storage designs.
- **`FlatMap::contains` at ~27 ns** over 1024 entries is a binary search (~10 comparisons) over
  cache-friendly contiguous storage — the "fast lookup" claim, quantified. `ObjectPool`
  acquire+release (~34 ns) and single-thread `LockFreeQueue` push+pop (~13 ns) reflect their
  atomic bookkeeping.
- **`fnv1a_64` at ~58 ns for 64 B** is ~0.9 ns/byte, in line with a byte-at-a-time FNV-1a.
- p99 stays within ~1.5× of the median for every scenario, so the medians are stable, not lucky
  minima.

**Caveats.** Desktop Windows is not a pinned, isolated benchmark host: absolute numbers carry
run-to-run noise (turbo, scheduling, background load) — treat them as a same-machine baseline,
not a cross-platform truth. The CI `benchmark` job only *builds* the suite; timings are not
gated on shared runners (see `docs/benchmarks/README.md`). Comparisons are valid only against a
re-run on comparable hardware.

## Reproduce

```bash
git clone https://github.com/danielPoloWork/egl-util-cpp && cd egl-util-cpp
cmake --preset bench
cmake --build --preset bench
./build/bench/util_bench      # Windows: .\build\bench\util_bench.exe
```
