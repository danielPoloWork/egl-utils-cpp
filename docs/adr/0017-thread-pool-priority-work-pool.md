# ADR-0017: `ThreadPool` priority work pool design

- **Status:** Accepted
- **Date:** 2026-07-02
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #5), roadmap 6.5 (closes Milestone 6),
  [ADR-0013](0013-semaphore-monitor-based-implementation.md) (monitor engine),
  [ADR-0015](0015-task-future-lightweight-promise-future.md) (`TaskFuture`, the result
  channel), [ADR-0016](0016-lock-free-queue-bounded-vyukov-mpmc.md) (why the lock-free
  queue is *not* the pool's spine)

## Context

The spec requires `ThreadPool` (component #5): a "priority-queue work pool" — the Milestone
6 closer and the component the milestone's other primitives were built toward. Four design
questions dominate:

1. **The queue.** "Priority-queue" rules out plain FIFO — and with it `LockFreeQueue`
   (6.4), which is FIFO by construction; concurrent lock-free *priority* queues are a
   research topic, not an enterprise building block. Workers must also *block* when idle,
   which wants a condition variable regardless.
2. **The task representation.** Submitted tasks carry a `TaskPromise` (move-only), so the
   classic `std::function<void()>` erasure does not compile — it requires copyable
   callables. `std::move_only_function` solves this but is C++23, absent at the floor.
3. **The result channel.** `submit` must report results and exceptions; 6.3 built exactly
   this (`TaskFuture`), including the abandoned-promise semantics that make shutdown
   design honest.
4. **Shutdown semantics.** What happens to queued-but-unstarted tasks when the pool stops:
   run them (drain) or drop them (their promises die → `BrokenTaskPromise`)?

## Decision

`ThreadPool` is a fixed-size pool of workers over a **monitor-guarded binary heap**:
a `std::vector` managed with `std::push_heap`/`std::pop_heap` under one mutex, ordered by
`(priority desc, sequence asc)` — an explicit **stable priority queue** (a monotonic
sequence number breaks ties), so equal-priority tasks run in submission order, which
`std::priority_queue` does not guarantee. Tasks are type-erased into a small internal
**move-only callable wrapper** (`unique_ptr`-based virtual dispatch), since
`std::function` cannot hold the move-only promise and `std::move_only_function` is beyond
the floor. `submit(priority, callable)` (and a priority-0 convenience overload) wraps the
callable so the worker delivers its return value — or its exception — through a
`TaskFuture<R>` returned to the caller; a nullary callable is the contract (callers
capture their arguments). **Shutdown drains**: `shutdown()` stops admissions
(`std::logic_error` on later `submit`) and workers finish everything already queued before
joining; the destructor calls `shutdown()`. Thread count is fixed at construction
(zero throws `std::invalid_argument`); `thread_count()` and a `pending()` snapshot are
observable.

## Alternatives Considered

- **`std::priority_queue` as the store** — the obvious engine. Rejected on a mechanical
  point: `top()` returns `const&`, so moving a task out requires `const_cast` (banned by
  the enabled tidy set) or a copy (impossible: tasks are move-only). `std::pop_heap` on a
  plain vector moves the top element to the back, where it can be moved out cleanly —
  same algorithm, no cast, plus free access for the stable-ordering comparator.
- **`LockFreeQueue` as the spine** — dogfooding appeal. Rejected: it is FIFO (no
  priorities) and non-blocking (idle workers would spin); a condvar-based monitor is
  needed for parking workers anyway, at which point the lock-free store buys nothing.
  Force-fitting it would trade the spec's "priority-queue" for symbolism.
- **Abandon-on-shutdown** (drop queued tasks; futures see `BrokenTaskPromise`) — rejected
  as the *default*: silently discarding accepted work is the surprising choice for a
  general-purpose pool, and a caller who wants prompt teardown can already build it
  (bounded submissions + drain). The machinery exists either way — if a `cancel_pending()`
  is ever added, dropped tasks' promises already report correctly.
- **Per-task arguments (`submit(f, args...)` like `std::async`)** — rejected as surface
  creep: lambdas capture arguments with full control over copy/move/ownership; a
  forwarding tuple-storage layer would duplicate what closures already do.
- **Work stealing / per-worker deques** — rejected as premature for "internal messaging"
  scale; the single monitor is the simplest thing that honors the spec, and the private
  engine can be swapped later without an API break (same reasoning as ADR-0013/0014).

## Consequences

- Priorities are plain `int` (higher first), with **deterministic FIFO within a priority
  level** — a documented, testable contract (`std::priority_queue` would make it
  unspecified).
- Every submission allocates twice (the erased task node, the shared future state) and
  synchronizes on one mutex — the honest cost profile of a monitor pool; fine for
  coarse-grained tasks, wrong for nanosecond work items (then you want ADR-0016's queue
  and no futures).
- Exceptions never kill workers: the wrapper catches everything a task throws and routes
  it into the task's future; the worker loop itself runs no user code outside that guard.
- Drain shutdown means `~ThreadPool()` blocks until queued work finishes — documented
  loudly; tasks that never return will hang teardown (the caller owns task discipline).
- `submit` after shutdown is misuse (`std::logic_error`), consistent with the house error
  model; `shutdown()` is idempotent but not safe to call concurrently from multiple
  threads (documented).
- **Patterns (AGENTS §8):** **Thread Pool** is adopted (this ADR); Monitor Object and
  Guarded Suspension rows gain their fourth location. The pool *realizes* Producer-Consumer
  (submitters/workers over the guarded heap) and Future/Promise (via 6.3) as constituent
  structure; Future/Promise is catalogued against `task_future.hpp`/ADR-0015 — an omission
  from the 6.3 PR repaired in this one.

## References

- Spec §2 component #5, §5 public interface & error model.
- ADR-0013/0014/0015/0016 — the Milestone 6 lineage this component composes.
- Schmidt et al., *POSA2* — Thread Pool variant of Leader/Followers discussion;
  Goetz et al., *Java Concurrency in Practice* — executor drain-vs-abandon semantics.
