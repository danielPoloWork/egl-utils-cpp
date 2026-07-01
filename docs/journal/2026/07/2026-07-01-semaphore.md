# 2026-07-01 — Semaphore (roadmap 6.1)

## What got done

- Implemented `it::d4np::util::Semaphore` (component #9), opening Milestone 6: a portable
  counting semaphore whose API mirrors `std::counting_semaphore` (`acquire`, `try_acquire`,
  `try_acquire_for`, `try_acquire_until`, `release(n)`, static `max()`) plus an advisory
  `available()` snapshot. Non-copyable, non-movable, header-only.
- **Implementation strategy (ADR-0013):** a Monitor Object over `std::mutex` +
  `std::condition_variable` with Guarded Suspension for the blocking wait — *not* a wrapper
  over `std::counting_semaphore`, whose floor implementations are defective (libstdc++
  GCC PR 100806 / PR 104928 in the `try_acquire*` family; early libc++ races), and *not*
  native OS handles (three platform paths, `sem_init` deprecated on macOS, OS-API code
  belongs to the compiled tier per ADR-0004). Mutex/condvar are exactly the portable veneer
  over OS primitives the spec asks for, and TSan models them precisely.
- Error model honored: negative initial count / release update → `std::invalid_argument`;
  overflowing release → `std::overflow_error` (vs UB in the standard type). Documented on
  the type; all error paths tested.
- **Patterns:** first multi-pattern adoption — **Monitor Object** and **Guarded
  Suspension** both catalogued (rows 2 and 3 in Adopted) with ADR-0013 as justification.

## Test notes

- Thread cases synchronize through the semaphore itself (done-semaphores with generous 5s
  `try_acquire_for` bounds) — no elapsed-time assertions, so they hold on loaded CI runners.
  Cases: blocked-acquirer wakeup, bulk `release(n)` waking every waiter, and a 100-round
  ping-pong alternation that would deadlock or disorder on a broken semaphore.
- TSan verification happens in CI only (the tsan preset uses GNU-style flags MSVC rejects;
  see [[local-verify-toolchain]]).

## Project state

- Milestones 1–5 complete; Milestone 6 in progress (6.1 done; 6.2–6.5 remain).
- Version still `0.0.0`; the maintainer was offered a Milestone-5 release PR and chose to
  continue with implementation instead — a release PR remains available on request.

## How the next session resumes

- Next roadmap item: **6.2 — `ReaderWriterLock`** (component #8), a read-optimized shared
  mutex. Design question for its ADR: what it adds over `std::shared_mutex` (read-mostly
  fairness policy? upgrade path? observability?) — check the spec wording ("optimized for
  read-mostly access") before wrapping vs reimplementing; the ADR-0013 alternatives
  analysis (wrap std / go native / build on mutex+condvar) is the template for that
  discussion.
- One PR at a time: wait for the 6.1 PR to merge before branching 6.2.
