# ADR-0013: `Semaphore` monitor-based implementation over mutex + condition variable

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #9), §3 (TSan-clean concurrency), roadmap 6.1,
  [ADR-0008](0008-adopt-object-pool-pattern.md) (mutex-guarded concurrency precedent)

## Context

The spec requires `Semaphore` (component #9): a "portable counting semaphore over OS
synchronization primitives", the first Milestone 6 concurrency component. C++20 ships
`std::counting_semaphore`, so the central design question is *wrap, reimplement, or go
native*. Three forces matter:

1. **Quality at the toolchain floor.** `std::counting_semaphore` at the floor is the
   buggiest corner of C++20 concurrency: libstdc++'s futex-backed implementation had
   lost-wakeup and premature-timeout defects in the `try_acquire*` family through the
   GCC 11 series (GCC PR 100806, PR 104928), and libc++'s early `<semaphore>` (pre-LLVM 13,
   the Apple Clang 14 era) had its own wait/notify races. A component whose reason to exist
   is *reliability across the matrix* cannot sit on that foundation.
2. **Architecture tiers.** Header-only is the default tier (ADR-0004); raw OS handles
   (`sem_t`, `dispatch_semaphore_t`, `CreateSemaphore`) belong to the compiled,
   OS-API-heavy tier and would force three platform code paths for one primitive.
   `std::mutex` and `std::condition_variable` *are* the portable veneer over the OS
   primitives (pthread mutex/condvar, SRWLOCK/CONDITION_VARIABLE) — which is exactly what
   the spec's "over OS synchronization primitives" asks for.
3. **Verifiability.** Every shared-state type must be TSan-clean and its thread-safety
   contract explicit (spec §3). Mutex/condvar synchronization is precisely modeled by
   ThreadSanitizer and helgrind; futex-based fast paths historically produce
   tool-specific false positives/negatives.

## Decision

`Semaphore` is a non-template, header-only class implemented as a **Monitor Object**: one
internal `std::mutex` serializes every operation on the count, and acquisition uses
**Guarded Suspension** — `acquire` blocks on a `std::condition_variable` with the predicate
`count > 0`, immune to spurious wakeups by construction. The API mirrors
`std::counting_semaphore` (`acquire`, `try_acquire`, `try_acquire_for`,
`try_acquire_until`, `release(n)`, static `max()`) plus an advisory `available()` snapshot
that the standard type cannot offer. Error handling is explicit and documented: a negative
initial count or negative release update throws `std::invalid_argument`, and a release that
would overflow the counter throws `std::overflow_error`; no other member throws. The type
is non-copyable and non-movable.

## Alternatives Considered

- **Thin wrapper over `std::counting_semaphore`** — near-zero implementation cost and the
  fastest uncontended path. Rejected because the floor implementations are defective
  (libstdc++ PR 100806/104928 in the `try_acquire*` family; early libc++ races), the
  `LeastMaxValue` template parameter leaks into every API that stores a semaphore, and the
  count is unobservable even as a diagnostic snapshot.
- **Native OS handles per platform** (`sem_t` / `dispatch_semaphore_t` /
  `CreateSemaphore`) — the literal reading of "OS synchronization primitives". Rejected:
  three divergent code paths for one primitive, `sem_init` is deprecated on macOS (POSIX
  unnamed semaphores unsupported), and OS-API-heavy code belongs to the compiled tier
  (ADR-0004), which would demote a basic primitive out of the header-only default.
- **Lock-free atomic count + `std::atomic::wait`** — elegant on paper. Rejected: C++20
  atomic wait/notify sits on the same immature floor machinery as
  `std::counting_semaphore` (it is how libstdc++ implements it), reintroducing the exact
  defects being avoided.

## Consequences

- Correctness and portability first: timed waits behave per `std::condition_variable`
  semantics on every matrix cell, and TSan/helgrind model the synchronization exactly.
- The cost is a heavier fast path than a futex semaphore — an uncontended
  `acquire`/`release` takes a mutex lock/unlock instead of one atomic RMW. For the
  library's use cases (resource gating, worker handoff) this is negligible; a benchmark can
  quantify it if a future component needs the futex path, and the class layout leaves room
  to swap the engine without an API break (the members are private implementation).
- `available()` is documented as an instantaneous snapshot — stale the moment it returns;
  valid for diagnostics/tests, never for synchronization decisions.
- `release` throwing on overflow (instead of UB like `std::counting_semaphore` beyond
  `max()`) honors the spec's error model: misuse is diagnosed, not undefined.
- **Patterns (AGENTS §8):** **Monitor Object** (all state accessed under one internal
  lock) and **Guarded Suspension** (blocking until `count > 0` via a condvar predicate)
  are both adopted deliberately and catalogued; this ADR is their justification.

## References

- Spec §2 component #9, §3 non-functional requirements, §5 public interface.
- GCC bugzilla PR 100806, PR 104928 (`counting_semaphore`/`__atomic_semaphore` defects).
- Schmidt et al., *Pattern-Oriented Software Architecture Vol. 2* — Monitor Object;
  Lea, *Concurrent Programming in Java* — Guarded Suspension.
