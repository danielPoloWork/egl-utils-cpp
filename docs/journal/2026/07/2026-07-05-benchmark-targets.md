# 2026-07-05 — Performance targets & regression thresholds (roadmap 11.6)

## What got done

- Added `docs/benchmarks/performance-targets.md`: the numeric performance contract for the eight
  benchmarked hot paths, on the existing `Stopwatch` harness (decision locked: keep the harness,
  not Google Benchmark). Linked from `docs/benchmarks/README.md` and spec §6.

## The framing (why two kinds of target)

Absolute nanosecond numbers are machine-specific and CI runners are too noisy to gate (ADR-0027),
so the contract separates:

1. **Algorithmic-class invariants** — machine-independent, always in force: the O(...) class and
   the zero/one-time-allocation property of each hot path (e.g. `FlatMap::contains` is O(log n),
   `StackAllocator` is O(1) with zero heap up to `Size`). A lookup that starts scaling linearly is
   a defect regardless of the clock.
2. **Regression thresholds** — relative, same-machine: per-op median ≤ **1.25×** the recorded
   baseline, with a **p99 ≤ 2× median** stability guard. Checked by hand against the baseline on
   comparable hardware; CI builds the suite but does not gate timings.

Eight scenarios across eight components (well over the "≥5 modules with numeric targets" bar);
methodology (warm-up + 64 reps × 2¹⁶ ops, median + p99, `do_not_optimize`, Release) and the
reference machine are stated so a run is reproducible. The OS-boundary components (`FileStream`,
`TcpSocket`, `Logger`) are explicitly *not* targeted — kernel/network-bound, not micro-benchmarkable.

## Verification

- `python tools/consistency_lint.py` → OK.
- Doc-only; no code, no build impact. Version stays `1.0.0`. (The baseline numbers reused here are
  the v0.0.0 reference-machine run from roadmap 10.2; no re-measurement in this PR.)

## Project state

- Milestones 1–10 complete. **Milestone 11 in progress:** 11.1–11.4, 11.6 done; **11.5 and 11.7
  remain.**

## How the next session resumes

- One PR at a time (AGENTS §6.1): after 11.6 merges, start **11.5 — security & threat-model
  section** (the only M11 item with a code component): a threat model for the untrusted-input
  components (`BinarySerializer`, `CliParser`, `JsonParser`), **libFuzzer harness targets** under
  `src/` (bench/test-scope, gated behind a CMake option; note the parsers are the fuzz targets),
  and a **SHA-256 non-cryptographic scoping statement** (decision locked) plus extended NIST test
  vectors. Consider whether the fuzz targets warrant an ADR (likely yes: a new build option + a
  security-testing decision).
- Then 11.7 (§3 example fix + reconcile the spec §5 `find` "optional-like" wording) closes M11.
