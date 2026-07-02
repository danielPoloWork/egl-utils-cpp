# ADR-0018: `Stopwatch` clock choice and accumulation semantics

- **Status:** Accepted
- **Date:** 2026-07-02
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #19), roadmap 7.1 (opens Milestone 7)

## Context

The spec requires `Stopwatch` (component #19): a "microsecond high-resolution profiler".
Small component, but two choices are classic traps worth recording:

1. **The clock.** `std::chrono::high_resolution_clock` sounds like the obvious pick for a
   "high-resolution profiler" — and is the trap: the standard permits it to alias
   `system_clock` (libstdc++ does exactly that), which is *not monotonic*; an NTP
   adjustment mid-measurement produces negative or wildly wrong elapsed times.
2. **What `elapsed` means** across start/stop cycles: last interval only, or the running
   total? Profilers that time a hot section entered repeatedly need the total; making that
   implicit-but-unspecified is how measurement bugs hide.

## Decision

`Stopwatch` measures exclusively with **`std::chrono::steady_clock`** — monotonic on every
platform of the matrix, with nanosecond-grade native resolution (QPC on Windows,
`CLOCK_MONOTONIC` on Linux/macOS), comfortably finer than the spec's microsecond target.
Semantics are **accumulating**: `start()`/`stop()` add each timed interval to a running
total; `elapsed()` (as `std::chrono::nanoseconds`, plus `elapsed_microseconds()` /
`elapsed_milliseconds()` integer conveniences) reports the total, including the currently
open interval while running; `reset()` zeroes; `restart()` is reset-then-start;
`Stopwatch::start_new()` is the one-liner constructor-and-start. Misuse follows the house
rule (`std::logic_error` for `start()` while running or `stop()` while stopped) so state
bugs surface at the call site instead of as corrupted measurements. The type is a plain
value (copyable, movable) and is not thread-safe — one stopwatch belongs to one thread.

## Alternatives Considered

- **`high_resolution_clock`** — rejected: permitted (and on libstdc++, actual) alias of the
  non-monotonic `system_clock`; the name buys nothing `steady_clock` lacks.
- **Last-interval semantics** (each `start` implicitly resets) — rejected: silently loses
  the accumulated-total use case (timing a section entered N times) and is one
  `restart()` call away for those who want it. Accumulation is the superset.
- **Idempotent `start`/`stop` (no-op on misuse)** — rejected: a `start()` that silently
  does nothing while running turns sequencing bugs into quietly wrong numbers — the worst
  failure mode a measurement tool can have.
- **RAII scope guard (`ScopedStopwatch` reporting on destruction)** — deferred, not
  refused: its natural reporting sink is the `Logger` (roadmap 7.3); designing the
  callback surface before the sink exists repeats the mistake the continuations decision
  avoided in ADR-0015.

## Consequences

- Measurements are immune to wall-clock adjustments; `elapsed()` is non-decreasing while
  running (testable without flaky upper-bound timing assertions).
- Accumulation means `elapsed()` after `stop(); start(); stop();` is the sum — documented
  and pinned by tests; users wanting per-interval numbers call `restart()`.
- The integer conveniences truncate (`duration_cast` semantics) — documented; the
  full-resolution `elapsed()` is the primary API.
- No pattern catalogue change (plain value utility; nothing adopted or force-fit).

## References

- Spec §2 component #19.
- cppreference: `high_resolution_clock` notes ("It is often just an alias for
  `system_clock` or `steady_clock`") — the trap this ADR exists to record.
