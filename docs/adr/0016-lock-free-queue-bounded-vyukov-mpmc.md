# ADR-0016: `LockFreeQueue<T>` bounded Vyukov MPMC design

- **Status:** Accepted
- **Date:** 2026-07-02
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #7), §3 (TSan-clean, no UB), roadmap 6.4,
  [ADR-0011](0011-circular-buffer-ring-semantics.md) (bounded-ring precedent),
  roadmap 6.5 (`ThreadPool`, a natural consumer)

## Context

The spec requires `LockFreeQueue<T>` (component #7): a "lock-free MPMC queue for fast
internal messaging". Multi-producer/multi-consumer without locks is the hardest primitive
in this library: the design space trades progress guarantees against memory-management
complexity, and the classical unbounded algorithms drag in a safe-memory-reclamation (SMR)
scheme — hazard pointers or epochs — because a dequeuer may free a node another thread is
still traversing. An SMR subsystem is a project in itself and a disproportionate liability
for "fast internal messaging". Three further forces: the toolchain floor's atomics are
mature (plain `std::atomic` load/store/CAS — unlike the C++20 wait/notify machinery
avoided since ADR-0013); every concurrency component must be TSan-verified but TSan alone
cannot prove lock-free algorithms correct, so the ordering argument must be written down;
and the repository already establishes bounded-ring semantics (`CircularBuffer`,
ADR-0011) and value-or-error non-blocking APIs (spec §5).

## Decision

`LockFreeQueue<T>` is a **bounded MPMC ring with per-slot sequence counters — Dmitry
Vyukov's bounded MPMC queue**. The capacity is fixed at construction (rounded up to the
next power of two for mask indexing; the actual capacity is observable via `capacity()`),
storage is a pre-sized array of slots, and the API is non-blocking value-or-error:
`try_push(T)` returns `false` when full, `try_pop()` returns `std::optional<T>` (empty when
the queue is), matching `CircularBuffer`'s vocabulary. Each slot pairs an atomic sequence
number with an `std::optional<T>`; producers and consumers claim positions with a relaxed
CAS on their respective tickets, and the slot's sequence — loaded acquire, stored release —
is the handshake that both orders the claim and publishes the payload. The type is
non-copyable, non-movable, and destroys any undelivered elements on destruction.

### Memory-ordering argument (normative for future edits)

| Operation | Ordering | Why |
|---|---|---|
| `slot.sequence.load` (both sides) | `acquire` | Synchronizes-with the counterpart's release store: a consumer that observes `seq == pos + 1` sees the producer's fully-constructed value; a producer that observes `seq == pos` sees the slot fully vacated. |
| `enqueue_pos_` / `dequeue_pos_` CAS | `relaxed` | The ticket only *claims* an index; no payload data is published through it. The slot sequence carries all inter-thread ordering (Vyukov's original design). |
| `slot.sequence.store` after write/read | `release` | Publishes the value just emplaced (producer) or the vacancy just created (consumer) to the next acquire-loader. |
| Position reload on contention | `relaxed` | Pure retry hint; correctness never depends on its freshness. |

## Alternatives Considered

- **Michael–Scott unbounded lock-free queue** — the textbook MPMC list. Rejected: correct
  node reclamation requires hazard pointers or epoch-based SMR (freeing a node another
  thread still holds is use-after-free; leaking instead is unbounded growth). That
  subsystem dwarfs the queue itself and its bugs are the worst kind (rare, silent,
  platform-dependent). Boundedness also provides natural backpressure, which "internal
  messaging" wants anyway.
- **Mutex + `std::deque`** — trivially correct. Rejected: the spec says lock-free; under
  N-way contention a single mutex serializes exactly the hot path this component exists to
  keep parallel.
- **Raw aligned storage + placement `new` per slot** (Vyukov's original layout) — rejected
  in favor of `std::optional<T>` per slot: the raw layout needs `reinterpret_cast`/launder
  or union member access, both banned by the enabled clang-tidy set
  (`cppcoreguidelines-pro-type-reinterpret-cast`, `-pro-type-union-access`), and manual
  drain logic in the destructor. `std::optional` costs one engaged-flag byte per slot
  (amortized into slot padding), makes destruction automatic, and keeps every line
  cast-free. The sequence protocol guarantees the optional is accessed by exactly one
  thread at a time, so it needs no atomicity of its own.
- **Per-slot cache-line padding** — rejected for the default: it multiplies memory by
  ~8–16× for small `T`. The two ticket counters *are* padded to separate cache lines
  (they are the guaranteed contention hot spots); adjacent-slot false sharing is workload
  noise by comparison, and the bounded ring keeps slots dense for the common
  drain-in-order case.

## Consequences

- **Progress-guarantee honesty:** the Vyukov queue is *lockless*, not lock-free in the
  formal sense — a producer suspended between claiming its ticket and releasing the slot
  sequence stalls consumers of that slot (and vice versa). No thread ever blocks in a
  kernel primitive and the fast path is a handful of atomics, which is what "lock-free" of
  this component's spec means in practice; the formal caveat is documented on the type. A
  formally lock-free MPMC needs the unbounded designs rejected above.
- Bounded capacity is a feature (backpressure) but callers must size it; `try_push`
  returning `false` is normal flow control, not an error.
- Capacity is rounded up to a power of two — observable and documented; requesting more
  than the largest representable power of two throws `std::invalid_argument` (and zero is
  rejected the same way, per the house misuse rule).
- TSan/ASan/Valgrind run in CI, but the ordering table above is the actual correctness
  argument — future edits must keep the table and the code in lockstep.
- FIFO is per-producer under contention (global order is decided by ticket interleaving) —
  the standard MPMC contract, pinned by the stress test.
- No pattern catalogue change: this is an algorithmic component; no classical design
  pattern is adopted or force-fit.

## References

- Spec §2 component #7, §3 non-functional requirements, §5 error model.
- D. Vyukov, *Bounded MPMC queue* (1024cores.net) — the algorithm and its ordering scheme.
- Michael & Scott, *Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue
  Algorithms* (PODC '96) — the rejected unbounded alternative.
- ADR-0011 (`CircularBuffer` ring semantics), ADR-0013 (why C++20 atomic wait/notify is
  avoided at the floor).
