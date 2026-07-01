# 2026-07-02 — LockFreeQueue (roadmap 6.4)

## What got done

- Implemented `it::d4np::util::LockFreeQueue<T>` (component #7): **Vyukov's bounded MPMC
  queue** — a pre-sized ring of slots, each pairing an atomic sequence number with the
  payload; producers/consumers claim positions via relaxed CAS on their ticket counters and
  the slot sequence (acquire load / release store) is the handshake that publishes payloads
  and vacancies. **The memory-ordering table in ADR-0016 is normative** — future edits must
  keep it and the code in lockstep; TSan is necessary but not sufficient for this component.
- API in the `CircularBuffer` vocabulary: `try_push(T)` → `bool` (false = full =
  backpressure, normal flow control), `try_pop()` → `std::optional<T>`, `capacity()`.
  Capacity rounds up to a power of two (observable, documented); zero/unrepresentable
  capacities throw `std::invalid_argument`. Per-producer FIFO (global order is ticket
  interleaving — the standard MPMC contract).
- **Key decisions (ADR-0016):** bounded Vyukov over Michael–Scott unbounded (correct node
  reclamation needs hazard pointers/epochs — an SMR subsystem that dwarfs the queue; bounded
  also gives backpressure). Slot storage is `std::optional<T>`, not raw aligned storage:
  the raw layout needs `reinterpret_cast`/union access, both banned by the enabled tidy set,
  and optional makes destructor drain automatic; the sequence protocol guarantees
  single-thread access to each optional. Ticket counters are cache-line padded; slots stay
  dense. `T` must be nothrow-move-constructible (`static_assert`) — a throwing move inside
  a claimed slot would wedge the ring.
- **Progress-guarantee honesty** documented on the type and in the ADR: the algorithm is
  *lockless* (no kernel blocking, a handful of atomics) but not formally lock-free — a
  thread suspended between ticket claim and sequence release stalls that slot.
- No pattern catalogue change: algorithmic component, nothing adopted or force-fit.

## Test notes

- Single-threaded: FIFO, full/empty transitions, capacity rounding, 1000-lap wrap-around on
  a size-2 ring, move-only payloads, destructor drain proven with an instance-counting type.
- MPMC stress: 4 producers × 2500 values through a size-64 ring (forces contention and
  wrap), 4 consumers; post-join verification that every (producer, sequence) pair arrived
  exactly once and per-producer sequences ascend within each consumer. All assertions after
  the joins.

## Project state

- Milestone 6: 6.1–6.4 done; only **6.5 ThreadPool** remains. Version `0.0.0` (release PRs
  on request; M5 release offer still open).

## How the next session resumes

- Next roadmap item: **6.5 — `ThreadPool`** (component #5), "priority-queue work pool" —
  the milestone closer and the consumer of TaskFuture (6.3). Design questions for the ADR:
  priority model (the spec says priority-queue — a `std::priority_queue` under a monitor is
  the likely engine; a lock-free priority queue is a research topic, don't force
  LockFreeQueue in), task type (type-erased `std::function`-like? move-only callables need
  `std::move_only_function`, absent at the floor — may need a small move-only function
  wrapper), submit API returning `TaskFuture` (promise satisfied inside the worker,
  set_exception on throw), shutdown semantics (drain vs abandon — abandoned tasks'
  promises → BrokenTaskPromise falls out naturally), and thread count policy. Closing 6.5
  completes Milestone 6 → offer the maintainer a release PR.
- One PR at a time: wait for the 6.4 PR to merge before branching 6.5.
