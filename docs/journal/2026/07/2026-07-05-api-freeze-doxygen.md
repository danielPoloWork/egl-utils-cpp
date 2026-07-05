# 2026-07-05 — API freeze & Doxygen gate (roadmap 10.1)

## What got done

- Opened **Milestone 10 (Hardening & 1.0)** with 10.1: **froze the 1.0 public API and made the
  Doxygen build a hard, warning-free gate.** Recorded in ADR-0026.
- **API freeze.** The public contract is every non-`detail` type/free function in
  `it::d4np::util` reachable from the umbrella header `util.hpp` (memory, containers, strings,
  concurrency, diagnostics, parsing, I/O components + `version_*` / `library_version()`).
  `it::d4np::util::detail` is explicitly not public. Post-1.0 follows SemVer (§11).
- **Doxygen enforcement.** Set `DOXYGEN_WARN_AS_ERROR = FAIL_ON_WARNINGS_PRINT`, so the CI `docs`
  job now fails on any documentation regression instead of passing silently — the standing "docs
  build without warnings" rule (§10) is finally *enforced*.
- **Curated API mainpage.** Added `docs/doxygen-mainpage.md` (an API-focused landing page
  cataloguing every component by module) and set it as `USE_MDFILE_AS_MAINPAGE`, dropping
  README.md from the Doxygen input. README stays the human landing page in the repo; it just is
  no longer parsed by Doxygen.

## Gotchas folded in (all found by actually building the docs)

- **`/* … */` inside a ` ```cpp ` fence breaks Doxygen.** `file_stream.hpp` and `json_parser.hpp`
  had inline C-comments inside example fences; Doxygen lost fence tracking ("reached end of file
  while inside a ``` block"), which **cascaded** into ~12 bogus "symbol not declared/defined"
  errors in `file_stream.cpp` and a spurious `Found ';' while parsing initializer list`. Switching
  those to `//` line comments cleared *all* of them at once. (`binary_serializer.hpp`, which used
  `//`, never warned — that was the tell.)
- **Undocumented parameter.** `StackAllocator::allocate` documented `alignment` but not `bytes`.
- **README as mainpage → 7 unresolved `\ref` warnings.** Its links to `docs/…`, `AGENTS.md`,
  `ROADMAP.md`, etc. resolve on GitHub but not inside the Doxygen input set. Fixed by using a
  dedicated API mainpage whose links point to generated class pages or the repo by full URL.
- **`EXTRACT_ALL = NO` is the wrong bar here.** Tried it to force per-member docs; it produced 160
  "not documented" errors — almost all STL-conformance typedefs/accessors (`value_type`, `size()`,
  `begin()`) and internal `detail::` helpers. Kept `EXTRACT_ALL = YES`: every public *type* has a
  prose block, and `WARN_AS_ERROR` keeps what exists well-formed. Rationale in ADR-0026.

## Verification

- Installed Doxygen 1.17.0 (portable) locally — there is no Doxygen on this box otherwise. Built
  the `docs` target with `WARN_AS_ERROR` on: **0 warnings, exit 0.** (Confirmed the gate bites by
  temporarily flipping `EXTRACT_ALL = NO` → 160 failures, then reverting.)
- Only doc-comment text and the CMake `docs` block changed in code; the library and tests are
  unaffected. `clang-format`/`clang-tidy` unaffected (comment-only edits). `consistency_lint.py`
  passing.
- CI note: Actions minutes still exhausted (billing), so PR checks fail at startup, not on
  content. The docs gate was verified locally with the same Doxygen the CI job installs.

## Project state

- Milestones 1–9 complete; **Milestone 10 in progress** (10.1 done; 10.2 benchmark suite, 10.3
  ≥80% coverage, 10.4 tag v1.0.0 remain). Version still `0.0.0`; the milestone→version bumps
  happen in the batched release PR(s).

## How the next session resumes

- Next roadmap item: **10.2 — complete the benchmark suite and publish baseline results under
  `docs/benchmarks/`.** The bench harness is still the Milestone-1 stub (`src/bench/.../bench_main.cpp`);
  10.2 builds it out with real per-component benchmarks (Google Benchmark or a hand-rolled
  steady-clock harness — decide in its ADR) and commits baseline numbers under `docs/benchmarks/`.
- One PR at a time: wait for the 10.1 PR to merge before starting 10.2.
