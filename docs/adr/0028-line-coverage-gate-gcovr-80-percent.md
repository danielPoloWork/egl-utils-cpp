# ADR-0028: Line-coverage gate — gcovr in CI at ≥80%, OpenCppCoverage locally

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §6 (verification & test strategy), §3 (non-functional: reliability),
  roadmap 10.3, AGENTS §10 (the "new code ≥ 80% line, finalized in an ADR" bar)

## Context

Roadmap 10.3 requires ≥80% line coverage across all modules, and AGENTS §10 says the coverage
bar is "finalized in an ADR". Until now there was no coverage measurement at all: no CI job, no
CMake preset, no recorded number. Two decisions are needed: **how coverage is measured and
enforced**, and **what the threshold is and against what**.

Constraint: the maintainer's local box is MSVC-only (no `gcc`/`clang`, so no `gcov`/`llvm-cov`),
while CI's reference cell is Linux + GCC. The two toolchains produce different coverage tools and
slightly different numbers (notably, a platform `#ifdef` branch counts on the OS that compiles
it).

## Decision

**Measure line coverage with `gcovr` (over GCC instrumentation) in a dedicated CI job that fails
below 80% overall for `src/main/cpp`, and measure locally with OpenCppCoverage against the MSVC
build.**

- **CI gate (`coverage` job, Linux/GCC).** A new `EGL_UTIL_COVERAGE` CMake option adds
  `--coverage -O0 -g` for GCC/Clang only (a no-op under MSVC, so it is safe to leave defined); a
  `coverage` CMake preset (Debug + static tier + the option) builds and runs `util_tests`, and
  `gcovr --root . --filter 'src/main/cpp/' --fail-under-line 80` fails the job below the
  threshold. The gate is on the **overall** library line rate, which the baseline puts at 93.0%
  — a wide margin that absorbs Windows/Linux measurement differences.
- **Local measurement (Windows/MSVC).** OpenCppCoverage runs against the MSVC `build/debug`
  binary and its PDBs, filtered to `src/main/cpp`, exporting Cobertura XML for a per-file
  breakdown. This is how the baseline below was captured and how gaps are found on the dev box
  (no `gcov` there).
- **Baseline (this PR).** 220 tests / 3856 assertions; **93.0% overall line coverage**, and
  **every module ≥80%** — the minimum is `tcp_socket.cpp` at 82.7%, raised from 65.6% by adding
  targeted tests for its edge and error paths (`wait_writable`, `set_non_blocking`, both
  move-assignments, an idle listener's timeout/`accept`, an unresolvable-host `connect`, a
  port-0 immediate-failure `connect`, a blocking-mode round trip, a send that fills the buffer to
  `would_block`, and closed-socket/closed-server graceful failure).

## Alternatives Considered

- **`llvm-cov` (source-based coverage)** — rejected: it needs a Clang toolchain in the coverage
  job, whereas GCC + `gcov` + `gcovr` reuse the cell CI already builds with; `gcovr` gives a
  simple `--fail-under-line` gate and a readable summary.
- **A per-file hard threshold in CI** — rejected as the enforced gate: `gcovr`'s per-file
  enforcement is awkward and brittle against small platform-`#ifdef` files, and Windows vs. Linux
  differences would make a per-file Linux gate flaky. The overall-80% gate is enforced; the
  **per-module ≥80% state is verified and recorded** (this ADR, and the journal's OpenCppCoverage
  table) rather than CI-gated per file.
- **A hosted service (Codecov/Coveralls)** — rejected: adds a third-party integration and a token
  for a self-contained library; a local `gcovr` threshold is enough and keeps the repo
  dependency-free.
- **Gate coverage on the Windows cell** — rejected: Windows has no native `gcov`; OpenCppCoverage
  is a great local tool but an awkward CI dependency, and GCC/`gcovr` on Linux is the standard.

## Consequences

- Coverage is now measured and enforced: a change that drops the library below 80% overall fails
  the CI `coverage` job.
- Two tools by design — `gcovr`/GCC in CI, OpenCppCoverage/MSVC locally; their absolute numbers
  differ slightly, so the CI gate is set with margin (80% vs. an actual ~93%) and comparisons are
  made within a toolchain, not across.
- `tcp_socket.cpp`'s error/edge paths are now exercised, which also hardens the socket component
  beyond the coverage number.
- Residual uncovered lines are genuinely hard-to-trigger OS-error branches (a failing
  `send`/`recv`/`close`, `EINTR` retries, `getsockname`/`bind`/`listen` failures) — deliberately
  not chased with brittle fault-injection.
- Patterns catalogue: unchanged.

## References

- Spec §6 (verification), §3 (reliability); AGENTS §10 (coverage bar).
- `gcovr` (`--filter`, `--fail-under-line`); GCC `--coverage`; OpenCppCoverage (Cobertura export).
