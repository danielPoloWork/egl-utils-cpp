# 2026-07-05 — Benchmark suite & baselines (roadmap 10.2)

## What got done

- Turned the Milestone-1 benchmark stub into a real, reproducible **micro-benchmark suite** and
  published the first baselines under `docs/benchmarks/`. Recorded in ADR-0027.
- **Dependency-free harness (`benchmark_harness.hpp`).** Times each scenario with the library's
  own `Stopwatch` (dogfooding ADR-0018): a warm-up rep, then 64 timed reps of 2^16 ops, reporting
  the per-operation **median + p99** and throughput. A portable `do_not_optimize` (address stored
  through a `volatile` sink — no inline asm, works under MSVC) prevents dead-code elimination. No
  Google Benchmark dependency — rationale in ADR-0027.
- **Eight scenarios**, all header-only, single-threaded, deterministic: `fnv1a_64`,
  `FlatMap::contains`, `ObjectPool` acquire+release, `LockFreeQueue` push+pop, `CircularBuffer`
  push+pop, `StackAllocator` reset+allocate, `StringBuilder::append`, `BinarySerializer`
  round-trip. I/O / socket / logger components are excluded (kernel/network-bound, not
  micro-benchmarkable).
- **Baseline report** `docs/benchmarks/2026-07-05-suite-baseline.md` with the full environment,
  results table, interpretation, and reproduce steps; indexed in `docs/benchmarks/README.md`.

## Headline baseline (i5-6600K, Win10, MSVC 19.51, Release)

| Scenario | Median |
|----------|--------|
| StackAllocator reset+allocate(64 B) | 1.67 ns |
| BinarySerializer u64+f64 round-trip | 3.20 ns |
| CircularBuffer push+pop | 3.06 ns |
| StringBuilder::append(4 B) | 4.12 ns |
| LockFreeQueue push+pop (1t) | 12.88 ns |
| FlatMap::contains (1024) | 27.41 ns |
| ObjectPool acquire+release | 33.85 ns |
| fnv1a_64 (64 B) | 57.83 ns |

## Gotchas folded in

- **Stack overflow from a stack-backed allocator.** `StackAllocator<1U<<20U>` as a local blew the
  1 MB default stack (`STATUS_STACK_OVERFLOW`, exit 253). Since the bench `reset()`s before each
  64 B allocation, dropped it to `StackAllocator<1U<<12U>`.
- **CI tidy on `bench_main.cpp`** (it's a diff `.cpp`, so the tidy gate applies): pointer
  arithmetic on `blob.data() + …` → `std::string_view{blob}.substr(...)`; `std::sort` →
  `std::ranges::sort`; a forwarding-reference `Body&&` (called repeatedly) → by-value `Body`; and
  `bugprone-exception-escape` on `main` — wrapped the suite in try/catch(...) and NOLINTed the
  residual (a throw from the stderr write in the handler; best-effort in a dev-only binary). The
  `.clang-tidy` disables magic-numbers and cognitive-complexity, so the bench's literals are fine.

## Verification

- Built the `bench` preset (Release) and ran the suite: clean, sensible numbers, p99 within ~1.5×
  of median. `clang-format` clean, `clang-tidy` clean on `bench_main.cpp` + `benchmark_harness.hpp`
  with the repo config, `consistency_lint.py` passing. Library/tests untouched (bench-only + docs).
- CI note: Actions minutes still exhausted (billing); the `benchmark` job only builds the suite
  anyway. Baselines were measured locally, as the methodology intends.

## Project state

- Milestones 1–9 complete; **Milestone 10 in progress** (10.1 + 10.2 done; 10.3 ≥80% coverage,
  10.4 tag v1.0.0 remain). Version still `0.0.0`.

## How the next session resumes

- Next roadmap item: **10.3 — achieve ≥80% line coverage across all modules.** Likely needs a
  coverage build (gcov/llvm-cov on the Linux CI cell, or OpenCppCoverage locally on Windows),
  measuring `util_tests`, then filling gaps with targeted tests. Decide the coverage tool/gate in
  its ADR; wire a CI coverage job if one isn't already present. Then 10.4 tags v1.0.0 (and the
  pre-1.0 milestone→version bumps for M5–M9 come due around the release PR).
- One PR at a time: wait for the 10.2 PR to merge before starting 10.3.
