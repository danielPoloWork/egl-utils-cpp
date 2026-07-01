# ADR-0008: Adopt the Object Pool pattern for `ObjectPool<T>`

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Maintainer
- **Related:** ADR-0005, ADR-0007; AGENTS.md §8, §9, §10; ROADMAP 3.4; spec §2, §5 (component #1)

## Context

Roadmap 3.4 (Milestone 3) calls for `ObjectPool<T>`: a thread-safe, O(1) pool of
pre-allocated objects that reduces the runtime cost of creating objects (spec component #1).
The recurring need is a hot path that repeatedly needs a short-lived object of a type whose
construction (or the allocation behind it) is not free — reusing a fixed set of already-built
instances removes that per-use cost and the associated allocator churn and fragmentation.

This is the first component in the library to adopt a named **design pattern**, so per
AGENTS §8 it must be justified in an ADR and catalogued in `docs/patterns/`. It is also the
first Milestone-3 type with a concurrency contract: acquire/release must be safe under
contention and verifiable by the CI ThreadSanitizer job.

## Decision

We adopt the **Object Pool** pattern (Creational; see `docs/patterns/design-patterns.md`) and
implement it as `ObjectPool<T>` with these decisions:

- **Fixed capacity, pre-allocated.** The pool constructs `capacity` objects up front — either
  value-initialized or produced by a caller-supplied factory — and never grows. Objects live
  for the pool's lifetime and are lent out, not re-created, on each acquire.
- **O(1) acquire/release via a free-slot stack.** Available slots are tracked by a
  pre-sized index stack (`free_slots_` + `free_count_`); acquire pops, release pushes. Both
  are constant-time and allocation-free.
- **Mutex-based thread-safety.** A single `std::mutex` serializes the free-list operations and
  the size queries. A borrowed object is owned exclusively by one handle, so its *use* needs
  no further synchronization. A lock-free pool is intentionally deferred (it depends on the
  Milestone-6 lock-free primitives); a mutex is correct, simple, and TSan-verifiable now.
- **RAII borrow via a move-only `Handle`.** `try_acquire()` returns `std::optional<Handle>` —
  an engaged optional when a slot is free, `std::nullopt` when exhausted. The handle grants
  access (`operator*`/`operator->`/`get`) and returns its slot to the pool on destruction. No
  raw acquire/release pairing is exposed, so leaks and double-frees are structurally prevented.
- **Non-copyable, non-movable pool.** Handles hold a back-pointer to the pool and the mutex is
  immovable; the pool is a fixed anchor that must outlive its handles.

## Alternatives Considered

- **Throw on exhaustion instead of returning `std::optional`.** Rejected as the default —
  exhaustion of a fixed pool is an expected, recoverable condition; an optional makes the
  caller handle it explicitly without exceptions on the hot path.
- **Construct on acquire / destroy on release (lazy).** Rejected — it reintroduces the
  per-use construction cost the pool exists to remove; the spec explicitly wants
  *pre-allocated* objects reused.
- **Lock-free free list now.** Rejected for this milestone — it would front-load the
  Milestone-6 concurrency work and its ABA/memory-reclamation subtleties; revisit once
  `LockFreeQueue` lands, superseding this ADR if adopted.
- **Return objects to the pool in a reset state automatically.** Rejected for v1 — a generic
  reset policy is type-specific; reused objects retain their prior state and the caller
  reinitializes. A reset hook can be added later without breaking the API.

## Consequences

- Callers get cheap, leak-safe reuse of expensive objects with an RAII borrow; the pool is
  safe to share across threads. Verified by unit tests plus a contended multithreaded test
  under ThreadSanitizer (AGENTS §9).
- The pool must outlive all outstanding handles (documented); a handle after pool destruction
  dangles, as with any borrow.
- First entry in the design-patterns catalogue (`docs/patterns/README.md`), establishing the
  ADR-plus-catalogue workflow for future pattern adoptions.
- Completes Milestone 3 (Memory & Resource Management).

## References

- `docs/patterns/design-patterns.md` (Creational → Object Pool); `docs/patterns/README.md`.
- `docs/specs/01_spec_util.md` §2 (component #1), §5 (Memory public surface).
- ROADMAP.md item 3.4.
- `src/main/cpp/it/d4np/util/object_pool.hpp`.
- ADR-0005 (ownership model), ADR-0007 (raw-arena sibling); AGENTS.md §8–§10.
