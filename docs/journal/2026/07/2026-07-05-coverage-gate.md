# 2026-07-05 — Line-coverage gate ≥80% (roadmap 10.3)

## What got done

- Stood up **coverage measurement + an enforced ≥80% gate** and confirmed the library clears it.
  Recorded in ADR-0028.
- **CI gate.** New `EGL_UTIL_COVERAGE` CMake option (adds `--coverage -O0 -g` for GCC/Clang, a
  no-op under MSVC), a `coverage` CMake preset (Debug + static tier + the option), and a CI
  `coverage` job (Linux/GCC) that builds it, runs `util_tests`, and enforces
  `gcovr --filter src/main/cpp/ --fail-under-line 80`.
- **Local measurement.** Installed OpenCppCoverage (silent install) to measure the MSVC build —
  there is no `gcov`/`clang` on this box, so gcovr can't run locally. Cobertura export gives a
  per-file breakdown.
- **Baseline: 93.0% overall, every module ≥80%.** Only `tcp_socket.cpp` started below (65.6%);
  raised to **82.7%** with targeted tests. 220 tests / 3856 assertions, all green.

## Filling the tcp_socket.cpp gap

The socket component's happy path was covered but its edges were not. Added deterministic,
single-threaded tests for: `wait_writable` (ready), `set_non_blocking` toggle, **both**
move-assignments (`TcpSocket` and `TcpServer`), an idle listener's `wait_readable` timeout +
would-block `accept`, an unresolvable-host `connect` (getaddrinfo failure), a **port-0** connect
(immediate hard failure — exercises the connect loop's failure branch without the Windows
loopback-refusal flakiness), a **blocking-mode** round trip, a send that fills the kernel buffer
until `would_block`, and closed-socket/closed-server graceful failure. 65.6% → 82.7%.

## Gotchas folded in

- **Two coverage toolchains, by necessity.** MSVC has no `gcov`, so local = OpenCppCoverage, CI =
  `gcovr`/GCC. Numbers differ slightly (a platform `#ifdef` branch counts on the OS that compiles
  it), so the CI gate is the **overall** rate at 80% with wide margin (actual 93%), and
  per-module ≥80% is verified/recorded, not per-file CI-gated. Rationale in ADR-0028.
- **`EGL_UTIL_COVERAGE` must not break MSVC.** Guarded the `--coverage` flags on
  `CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"`; confirmed the `coverage` preset still *configures*
  cleanly under MSVC (flags become a no-op) so the preset/JSON is valid end-to-end.
- **CI tidy on the modified test** (`bugprone-implicit-widening-of-multiplication-result`):
  `64U * 1024U` → `std::size_t{64} * 1024` so the multiply happens in `size_t`.
- Residual uncovered lines are hard-to-trigger OS-error branches (failing `send`/`recv`/`close`,
  `EINTR` retries, `bind`/`listen`/`getsockname` failures) — deliberately not chased with brittle
  fault injection.

## Verification

- Local: full suite 220/220; OpenCppCoverage 93.0% overall, min module `tcp_socket.cpp` 82.7%.
  `clang-format`/`clang-tidy` clean on the modified test, `consistency_lint.py` passing. The
  `coverage` preset configures under MSVC (no-op guard).
- The gcovr CI job itself is unverified locally (no GCC/gcov on this box); it mirrors the existing
  Linux jobs and uses a standard, well-margined gcovr invocation. CI note: Actions minutes still
  exhausted (billing).

## Project state

- Milestones 1–9 complete; **Milestone 10 in progress** (10.1, 10.2, 10.3 done; **10.4 — tag and
  release v1.0.0 — remains**). Version still `0.0.0`.

## How the next session resumes

- Final roadmap item: **10.4 — tag and release v1.0.0 under SemVer.** This is the release PR: bump
  `D4NP_UTIL_VERSION_*` in `version.hpp` (and the README `Status-vX.Y.Z` badge — consistency_lint
  checks version lockstep), roll `[Unreleased]` in `CHANGELOG.md` into a per-version file under
  `docs/changelog/v1/v1.0.0.md` (+ index row), draft release notes under `docs/releases/`, mark
  README milestone 10 done + close the M10 GitHub milestone. Note the pre-1.0 milestone→version
  history (M5–M9 were never individually released) — decide whether v1.0.0 subsumes them (likely
  yes: one 1.0.0 covering the whole surface). Per AGENTS §11 the agent bumps/rolls/drafts and the
  maintainer opens/merges the release PR and publishes.
- One PR at a time: wait for the 10.3 PR to merge before starting 10.4.
