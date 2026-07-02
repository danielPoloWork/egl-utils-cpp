# 2026-07-02 — Stopwatch (roadmap 7.1)

## What got done

- Implemented `it::d4np::util::Stopwatch` (component #19), opening Milestone 7
  (Diagnostics & Instrumentation): an accumulating profiler over
  `std::chrono::steady_clock`. `start`/`stop` add intervals to a running total;
  `elapsed()` returns `std::chrono::nanoseconds` including the open interval;
  `elapsed_microseconds()`/`elapsed_milliseconds()` integer conveniences; `reset`,
  `restart`, `Stopwatch::start_new()`. Plain value type (copyable), not thread-safe.
- **ADR-0018** records the two trap-decisions: `steady_clock` over
  `high_resolution_clock` (the standard permits — and libstdc++ chooses — aliasing the
  non-monotonic `system_clock`; an NTP step mid-measurement corrupts timings), and
  accumulating semantics over last-interval (the superset; `restart()` recovers the
  other). Sequencing misuse throws `std::logic_error` — idempotent start/stop was rejected
  because a silently ignored `start()` turns state bugs into quietly wrong numbers.
- An RAII scope guard (`ScopedStopwatch` reporting on destruction) was **deferred** to
  when `Logger` (7.3) exists as its reporting sink — same reasoning as deferring
  continuations in ADR-0015.
- No pattern catalogue change.

## Test notes

- Timing assertions are **lower-bound or monotonicity only**: `sleep_for` guarantees at
  least the requested duration against the same steady clock the stopwatch reads; upper
  bounds would flake on loaded CI runners. Cases: zero start, non-decreasing while
  running, freeze on stop, accumulation across cycles, reset/restart, the misuse matrix,
  value-copy independence.

## Project state

- Milestones 1–6 complete; Milestone 7 in progress (7.1 done; 7.2 StackTrace, 7.3 Logger
  remain). Version `0.0.0` — **the maintainer has been offered a release PR twice (M5, M6
  both unreleased); still pending their go-ahead.**

## How the next session resumes

- Next roadmap item: **7.2 — `StackTrace`** (component #21), "native-API stack capture".
  This is the first component ADR-0004 anticipated for the **compiled STATIC tier**
  (OS-API-heavy: `CaptureStackBackTrace`/DbgHelp on Windows, `backtrace(3)`/`dladdr` on
  Linux/macOS). Design questions for its ADR: header-only capture with opt-in compiled
  symbolization vs fully compiled; symbolization on capture or lazily on format (capture
  must be cheap — it runs in crash paths); frame skipping; and what the header-only build
  does (empty trace vs raw addresses). Check how `EGL_UTIL_BUILD_STATIC` gates
  `library_version()` for the wiring precedent, and remember `std::stacktrace` is C++23 —
  beyond the floor, which is the component's reason to exist.
- One PR at a time: wait for the 7.1 PR to merge before branching 7.2.
