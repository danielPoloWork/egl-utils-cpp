# ADR-0007: `StackAllocator<Size>` bump-arena semantics

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Maintainer
- **Related:** ADR-0005, ADR-0006; AGENTS.md §9, §10; ROADMAP 3.3; spec §2, §5 (component #3)

## Context

Roadmap 3.3 (Milestone 3) calls for `StackAllocator<Size>`: a fast allocator that reserves a
fixed block of storage — on the stack when declared as a local — for temporary allocation of
short-lived data (spec component #3). ADR-0005 and ADR-0006 built their types by composing
standard owners (`std::unique_ptr`, `std::vector`) because those components' purposes did not
require raw memory. A bump allocator is the opposite case: its entire job *is* to hand out
suitably-aligned slices of a raw byte buffer, so it must manage untyped storage, alignment,
and a bump offset directly. This is the "raw memory is warranted" exception ADR-0005 carves
out. The open questions are the reclamation model, the ownership/movability contract, and how
to do the unavoidable low-level work without tripping the quality bar (AGENTS §10).

## Decision

`StackAllocator<Size>` is a **monotonic bump allocator over an in-object
`alignas(std::max_align_t) std::array<std::byte, Size>` buffer**:

- **Allocation is a pointer bump.** `allocate(bytes, alignment)` aligns the current cursor with
  `std::align`, advances the offset, and returns raw uninitialized storage; it throws
  `std::bad_alloc` when the remaining capacity cannot satisfy the request.
  `allocate_uninitialized<T>(count)` is a typed convenience wrapper.
- **No per-allocation free.** Memory is reclaimed wholesale with `reset()`, or unwound to a
  saved high-water mark with `mark()` / `rewind()`. There is deliberately no `deallocate`,
  because a monotonic allocator cannot free an arbitrary interior block.
- **Neither copyable nor movable.** Returned pointers point into the object's own buffer;
  moving the allocator would dangle every outstanding allocation. All copy/move operations are
  deleted.
- **Lint-clean without suppressions.** The buffer uses `std::array` (not a `T[]`, which would
  trip `avoid-c-arrays`); cursor math uses `std::next` / `std::distance` and casts use
  `static_cast<std::byte*>` (avoiding `pro-bounds-pointer-arithmetic` and
  `pro-type-reinterpret-cast`); and the buffer is value-initialized to satisfy
  `pro-type-member-init`.

## Alternatives Considered

- **Return `nullptr` on exhaustion instead of throwing.** Rejected as the default — throwing
  `std::bad_alloc` matches allocator convention and makes the failure impossible to ignore. A
  non-throwing `try_allocate` can be added later if a use case appears.
- **Support LIFO `deallocate` (rewind only the most recent allocation).** Rejected for v1 —
  `mark()`/`rewind()` already provide scoped reclamation more generally and without tracking
  per-allocation sizes.
- **Leave the buffer uninitialized for speed (suppress `pro-type-member-init`).** Rejected —
  it would require a lint suppression the quality bar forbids; the one-time zero-fill is a
  negligible, predictable cost, and the storage is still overwritten by callers before use.
- **Make it a C++ `Allocator` (rebind/`value_type`) for use with std containers.** Rejected as
  out of scope — this is an arena, not a container allocator; a std-conforming adaptor would be
  a separate item.

## Consequences

- Callers get very fast scratch allocation with trivial teardown (`reset()` at scope exit; no
  destructors run for trivially-destructible data). For non-trivial types the caller is
  responsible for constructing and destroying objects in the returned storage.
- The type is a scope-local resource by contract: pass it by reference, never by value.
- Establishes the raw-arena pattern that `ObjectPool<T>` (roadmap 3.4) will build on, and
  balances the composition pattern of ADR-0005/0006 — the library reaches for raw memory only
  where the component's purpose demands it, and even then stays suppression-free.

## References

- `docs/specs/01_spec_util.md` §2 (component #3), §5 (Memory public surface).
- ROADMAP.md item 3.3.
- `src/main/cpp/it/d4np/util/stack_allocator.hpp`.
- ADR-0005 (ownership-by-composition; the raw-memory exception), ADR-0006; AGENTS.md §9–§10.
