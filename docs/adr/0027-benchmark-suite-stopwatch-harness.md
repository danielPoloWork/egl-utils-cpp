# ADR-0027: Benchmark suite on a dependency-free Stopwatch harness

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §3 (non-functional: performance), §6 (verification & test strategy),
  roadmap 10.2, [ADR-0018](0018-stopwatch-steady-clock-accumulation.md) (the `Stopwatch` it
  reuses), the `docs/benchmarks/` methodology note

## Context

Roadmap 10.2 completes the benchmark suite and publishes baselines under `docs/benchmarks/`.
Until now `src/bench/.../bench_main.cpp` was a Milestone-1 stub that only kept the `bench` preset
buildable. The spec makes several qualitative performance claims ("ultra-fast lookups", "O(1)
object pool", "ultra-fast serializer") that §10 requires to be backed by a reproducible
benchmark, and the CI `benchmark` job builds the suite but — deliberately — does not run timings
as a gate (shared runners are too noisy).

Decisions: **which harness** (a third-party framework such as Google Benchmark, or a small
in-house one) and **what to measure and how to record it**.

## Decision

**A small, dependency-free micro-benchmark harness that dogfoods the library's own `Stopwatch`,
covering the header-only performance-critical hot paths, with baselines published as dated
reports under `docs/benchmarks/`.**

- **In-house harness, no new dependency.** `benchmark_harness.hpp` times each scenario with
  `it::d4np::util::Stopwatch` (ADR-0018): a warm-up rep, then N timed reps of a body performing
  `ops_per_rep` operations, reporting the **per-operation median and p99** across reps and
  deriving throughput from the median. A portable `do_not_optimize` (store the value's address
  through a `volatile` sink — no inline asm, so it works under MSVC) prevents dead-code
  elimination. This keeps the consumer-facing zero-dependency contract intact (the benchmark is
  dev-only, but adding Google Benchmark's build cost buys little for these simple loops) and
  dogfoods `Stopwatch`.
- **Scope: header-only, single-threaded hot paths.** Eight scenarios — `fnv1a_64`,
  `FlatMap::contains`, `ObjectPool` acquire+release, single-thread `LockFreeQueue` push+pop,
  `CircularBuffer` push+pop, `StackAllocator` allocate, `StringBuilder::append`, and a
  `BinarySerializer` round-trip. These are the components whose performance the spec asserts and
  whose measurement is deterministic. The I/O and OS-boundary components (`FileStream`,
  `TcpSocket`, `Logger`) are excluded — they are dominated by the kernel/network, not
  micro-benchmarkable into a stable per-op number.
- **CI builds, a reference machine measures.** The CI `benchmark` job keeps building the suite so
  it never rots, but it does not gate on timings. Baselines are captured from a Release build on
  a recorded machine (CPU, RAM, OS, toolchain, commit) and committed as a dated report under
  `docs/benchmarks/`, per the existing template; a regression is only meaningful against a re-run
  on comparable hardware.

## Alternatives Considered

- **Google Benchmark (via FetchContent, like doctest)** — rejected for now: it adds a
  configure/build dependency and API surface for loops that a 40-line `Stopwatch` harness times
  adequately, and the project already owns a high-resolution timer worth dogfooding. Revisit if
  the suite grows to need registration, fixtures, or automatic iteration scaling.
- **Gate CI on measured timings** — rejected: shared CI runners are too noisy (turbo,
  co-tenancy) for a stable regression threshold; the job builds the suite, and gating is done by
  hand against a recorded baseline on comparable hardware (documented in
  `docs/benchmarks/README.md`).
- **Benchmark the I/O / socket components too** — rejected: their latency is kernel/network
  bound, not a property of this code, so a per-op micro-number would be misleading.
- **One report file per scenario** — rejected as churn for a single coordinated run: one suite
  baseline report holds all eight scenarios (same environment, same command); per-scenario
  reports can split out later if a scenario needs its own deep-dive.

## Consequences

- The spec's performance claims are now backed by concrete, reproducible numbers, and there is a
  baseline (v0.0.0) to compare future runs against.
- The harness has no third-party dependency and exercises `Stopwatch` as a real consumer would.
- The `bench` target stays green in CI by construction; timing regressions are caught by
  re-running on a comparable machine, not by a noisy CI threshold.
- Adding a scenario is a function in `bench_main.cpp` plus a row in the baseline report; adding a
  dependency is not required.
- Patterns catalogue: unchanged.

## References

- Spec §3 (performance NFR), §6 (verification), §10 (performance claims need a reproducible
  benchmark).
- ADR-0018 (`Stopwatch`); `docs/benchmarks/README.md` (methodology) and `template.md`.
