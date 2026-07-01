# ADR-0011: `CircularBuffer<T>` ring-buffer semantics

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Maintainer
- **Related:** ADR-0006; AGENTS.md §9, §10; ROADMAP 4.3; spec §2, §5 (component #13)

## Context

Roadmap 4.3 (Milestone 4) specifies `CircularBuffer<T>`: a fixed circular container optimized
for streaming byte flows (spec component #13). Unlike the sorted flat containers of ADR-0009,
this is a FIFO ring: a fixed-capacity buffer that producers append to and consumers drain, with
constant-time push/pop and no reallocation. The design questions are the storage model, the
full-buffer policy, and the empty/full accounting.

## Decision

`CircularBuffer<T>` is a **fixed-capacity ring buffer backed by a single `std::vector<T>`**
sized once at construction and reused via a `head_` index and a `count_`:

- **Storage by composition over a pre-sized `std::vector<T>`.** Slots are value-initialized up
  front (so `T` must be default-constructible) and reused by assignment on push; there is no
  per-element allocation, no reallocation, and — per the ADR-0006 precedent — no raw storage,
  so the type stays leak-safe and lint-clean.
- **Index arithmetic without modulo.** Wrapping uses a compare-and-subtract (`advance`,
  `tail_index`, `back_index`) rather than `%`, which is faster and sidesteps any divide-by-zero
  concern. Capacity must be non-zero (a documented precondition, asserted in debug).
- **`count_` for full/empty.** A separate element count distinguishes the full and empty states
  unambiguously (rather than the "waste one slot" trick), so the whole capacity is usable.
- **Two explicit push policies.** `try_push` returns `false` when full (lossless back-pressure);
  `push_overwrite` drops the oldest element to admit the newest (the lossy "latest wins"
  behavior live streams want). `pop()` returns `std::optional<T>` — the value, or `nullopt` when
  empty — so draining an empty buffer is not undefined.
- **Rule of zero.** `std::vector` + two indices gives correct copy/move/destroy for free.

## Alternatives Considered

- **Raw aligned storage + placement new/destroy per slot.** Rejected — it avoids the
  default-constructible requirement and eager construction, but reintroduces
  `owning-memory`/lifetime management and lint suppressions for no benefit at this buffer's
  typical element types (bytes, PODs, small movable objects).
- **A single full-buffer policy.** Rejected — reject-when-full and overwrite-oldest are both
  legitimate and mutually exclusive needs; exposing both as named operations is clearer than a
  policy template parameter or a runtime flag.
- **Compile-time capacity (`std::array<T, N>`).** Rejected as the default — streaming buffer
  sizes are usually a runtime/config choice; a runtime capacity is more broadly useful and
  matches the other Milestone-3/4 containers. A fixed-N variant could be added later.
- **`bool pop(T& out)` sentinel style.** Rejected — `std::optional<T>` is clearer and matches
  `ObjectPool::try_acquire`'s convention in this library.

## Consequences

- O(1) push/pop with a compact, contiguous, reallocation-free footprint; both back-pressure and
  latest-wins streaming are first-class.
- `T` must be default-constructible and assignable (the slots are value-initialized and reused);
  this is documented on the type. `front()`/`back()` on an empty buffer are undefined (asserted
  in debug), matching `std::queue`/`std::vector` conventions.
- Completes Milestone 4 (Contiguous Containers).

## References

- `docs/specs/01_spec_util.md` §2 (component #13), §5 (Containers public surface).
- ROADMAP.md item 4.3.
- `src/main/cpp/it/d4np/util/circular_buffer.hpp`.
- ADR-0006 (composition over `std::vector`); AGENTS.md §9–§10.
