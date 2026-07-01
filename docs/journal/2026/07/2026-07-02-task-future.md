# 2026-07-02 — TaskFuture (roadmap 6.3)

## What got done

- Implemented `it::d4np::util::TaskFuture<T>` / `TaskPromise<T>` (component #6): a
  lightweight, move-only, single-shot promise/future pair over one `make_shared` allocation
  of a monitor-protected state (mutex + condvar + `std::variant`). API: `get_future()`
  (once), perfect-forwarding `set_value` (single constrained template instead of std's
  overload pair; a `requires`-constrained nullary overload covers `T = void` without a class
  specialization), `set_exception`; `valid()`, `ready()`, `wait[_for/_until]` (plain `bool` —
  no deferred state, so no status enum), single-shot `get()` that moves the value out and
  invalidates the future.
- **What "lightweight" drops (ADR-0015):** `std::async` coupling, `shared_future`,
  allocators, and the `future_error` taxonomy. **Continuations (`then`) deferred, not
  refused** — ThreadPool (6.5) is what would give them an executor; the shared state is
  private so they can be added later without an API break.
- **Error model:** misuse → `std::logic_error`; abandoned promise → `BrokenTaskPromise`
  (first custom exception type in the library) thrown from `get()`. Design nicety: the
  shared state's variant has a dedicated *abandoned* alternative (trivial tag), so the
  promise destructor's abandonment path is genuinely `noexcept` — the `BrokenTaskPromise`
  object is constructed in the getter's thread, not allocated during promise teardown.
- Move semantics subtlety handled: `TaskPromise` move *assignment* abandons the
  currently-owned unsatisfied state before adopting the other's (default would silently
  strand the old future forever). Covered by a test.
- Patterns: third exercise of Monitor Object + Guarded Suspension; catalogue rows refined
  with `task_future.hpp` + ADR-0015.

## Test notes

- 9 cases: same-thread and cross-thread handoff, `void` payload, exception rethrow
  (`CHECK_THROWS_WITH_AS` pins the message), abandonment via destruction *and* via
  move-assignment, the full misuse matrix, timed waits, a `std::unique_ptr` payload proving
  the move-out path, and converting `set_value` (`const char*` → `std::string`).
- `[[nodiscard]]` calls inside `CHECK_THROWS_AS` need `static_cast<void>(...)` or the
  discarded-result warning fires under warnings-as-errors.

## Project state

- Milestone 6: 6.1–6.3 done; 6.4 LockFreeQueue and 6.5 ThreadPool remain. Version `0.0.0`
  (release PRs on request).

## How the next session resumes

- Next roadmap item: **6.4 — `LockFreeQueue<T>`** (component #7), "lock-free MPMC queue for
  fast internal messaging". This is the milestone's hard one and a departure from the
  monitor pattern of 6.1–6.3: expect a bounded ring of slots with per-slot sequence
  counters (Vyukov-style bounded MPMC) — the unbounded Michael–Scott design needs a memory
  reclamation scheme (hazard pointers/epochs), which is a project in itself; boundedness
  also matches CircularBuffer precedent and ThreadPool's needs. ADR must justify the
  algorithm choice, the boundedness, and the memory-ordering arguments; TSan is necessary
  but not sufficient for lock-free code — reason the orderings in the ADR and consider a
  stress test with many producers/consumers.
- One PR at a time: wait for the 6.3 PR to merge before branching 6.4.
