# 2026-07-02 — ThreadPool (roadmap 6.5) — Milestone 6 complete

## What got done

- Implemented `it::d4np::util::ThreadPool` (component #5), closing Milestone 6 by composing
  its primitives: `submit([priority,] callable)` returns a `TaskFuture` (6.3) carrying the
  task's result or exception; the priority-ordering test parks the worker with a `Semaphore`
  (6.1). Fixed thread count (zero throws); `thread_count()` and a `pending()` snapshot.
- **Key decisions (ADR-0017):**
  - **Stable priority queue** as a monitor-guarded `std::vector` binary heap
    (`std::push_heap`/`pop_heap`) ordered by (priority desc, sequence asc) — equal
    priorities run in submission order, a contract `std::priority_queue` cannot give;
    `std::priority_queue` was also mechanically unusable (`top()` is `const&`, so moving a
    move-only task out needs a banned `const_cast`; `pop_heap` moves it to `back()` where
    it moves out cleanly).
  - **Move-only task erasure** (`detail::MoveOnlyTask`, unique_ptr + virtual dispatch):
    tasks own a `TaskPromise`, so `std::function` (copyable-only) cannot hold them and
    `std::move_only_function` is C++23, beyond the floor.
  - **`LockFreeQueue` deliberately not the spine**: FIFO (no priorities) and non-blocking
    (idle workers would spin); the parking condvar makes a monitor the honest engine.
  - **Drain shutdown**: `shutdown()` stops admissions (`std::logic_error` after) and
    workers finish everything queued before joining; dtor calls it. Documented loudly that
    a never-returning task hangs teardown. Abandon-on-shutdown rejected as default —
    the `BrokenTaskPromise` machinery makes a future `cancel_pending()` cheap if wanted.
  - Constructor is exception-safe: if a worker fails to spawn, the already-started ones
    are joined before rethrowing.
- **Patterns:** **Thread Pool** adopted (row 5). Repaired a 6.3 omission: **Future /
  Promise** (in the taxonomy) is now catalogued for `task_future.hpp`/ADR-0015 (row 4).
  Monitor Object + Guarded Suspension rows gained their fourth location.

## Test notes

- 7 cases: value/void/move-only result delivery; throwing task surfaces via the future and
  the worker survives; the priority contract proven observable on a single-worker pool
  parked by a `Semaphore` gate (order `5,3,1` then FIFO ties `100,200`); drain-on-shutdown
  (32 queued tasks all execute during `~ThreadPool`, every future satisfied); misuse matrix;
  500 tasks × 4 workers exactly-once; nested submit from inside a task (needs ≥2 workers —
  a 1-worker pool deadlocks on inner `get()`, documented pool-usage hazard, not a bug).

## Project state

- **Milestones 1–6 complete.** Milestone 7 (Diagnostics: Stopwatch, StackTrace, Logger) is
  next. Version still `0.0.0`; with two milestones now unreleased (M5, M6), a release PR is
  overdue — offer it before starting 7.1.

## How the next session resumes

- **First: offer the maintainer a release PR** (version bump + changelog roll per AGENTS
  §11; two completed milestones pending release).
- Next roadmap item otherwise: **7.1 — `Stopwatch`** (component #19), a microsecond
  high-resolution profiler — small, single-threaded, `std::chrono::steady_clock`-based;
  likely needs no ADR unless the API grows scopes/laps. Note `StackTrace` (7.2) and the
  async `Logger` (7.3) are the components ADR-0004 anticipated for the compiled STATIC
  tier — 7.2/7.3 will need build-model decisions, not 7.1.
- One PR at a time: wait for the 6.5 PR to merge before branching.
