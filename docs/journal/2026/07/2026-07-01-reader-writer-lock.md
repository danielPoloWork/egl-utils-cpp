# 2026-07-01 — ReaderWriterLock (roadmap 6.2)

## What got done

- Implemented `it::d4np::util::ReaderWriterLock` (component #8): a **writer-preferring**
  reader/writer lock satisfying the SharedTimedMutex named requirements, so the std lock
  guards (`std::scoped_lock`, `std::unique_lock`, `std::shared_lock`) compose with it
  directly — no bespoke RAII types. Non-copyable, non-movable, header-only.
- **The component's reason to exist is the fairness contract (ADR-0014):**
  `std::shared_mutex` scheduling is unspecified and platform-divergent — glibc's
  `pthread_rwlock_t` defaults to reader preference, under which read-mostly workloads
  starve writers indefinitely. Here the policy is explicit and identical on every matrix
  cell: once a writer waits, new readers are held back until it has run. Documented
  trade-off: a continuous writer stream can starve readers (the right side of the trade for
  read-mostly access; phase-fair recorded as the upgrade path).
- Same engine as `Semaphore` (ADR-0013): Monitor Object over one `std::mutex`, two condvars
  (`readers_allowed_`, `writer_turn_`), Guarded Suspension predicate waits. The patterns
  catalogue rows for Monitor Object and Guarded Suspension were *refined* (second location
  + ADR-0014) rather than duplicated — first use of the catalogue's refinement rule.
- Subtle correctness point, covered by a dedicated test: a **timed writer that gives up
  must wake the readers it was holding back** (they were blocked on its account); handled
  in the shared `writer_wait` engine of `try_lock_for`/`try_lock_until`.
- Unlock paths are deliberately non-throwing (RAII guards call them from destructors);
  precondition violations are documented UB per the SharedMutex requirements — unlike
  `Semaphore::release`, which throws because overflow is a reachable failure of a normal
  operation. The asymmetry is reasoned in ADR-0014.

## Test notes

- Same discipline as 6.1: no elapsed-time assertions; handshakes are polling loops bounded
  by huge iteration counts or `try_lock_shared_for(5s)`. The writer-preference test polls
  `try_lock_shared` until it fails (proof the writer registered) before releasing the
  reader. The contention test runs 4 writers × 1000 increments with a reader thread
  hammering the shared side — the reader records violations into a plain bool instead of
  calling doctest `CHECK` per iteration (millions of assertions would swamp the run).
- Same-thread recursive shared acquisition was removed from the guard-compatibility test:
  it violates the documented non-recursive precondition even though the implementation
  happens to tolerate it.

## Project state

- Milestones 1–5 complete; Milestone 6 in progress (6.1, 6.2 done; 6.3 TaskFuture,
  6.4 LockFreeQueue, 6.5 ThreadPool remain). Version still `0.0.0` (release PRs on request).

## How the next session resumes

- Next roadmap item: **6.3 — `TaskFuture<T>`** (component #6), a "lightweight future/promise
  for cooperative tasks". Design questions for its ADR: what "lightweight" drops relative to
  `std::future` (shared-state allocation? `std::async` coupling? exceptions across the
  boundary?), whether continuation support (`then`) is in scope, and the error model for
  broken-promise / double-set. `Semaphore`-style monitor machinery likely underpins the
  blocking `get()`; ThreadPool (6.5) is the eventual consumer, so keep its needs in view.
- One PR at a time: wait for the 6.2 PR to merge before branching 6.3.
