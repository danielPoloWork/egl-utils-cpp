# ADR-0006: `HeapArray<T>` as a fixed-size restriction over `std::vector`

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Maintainer
- **Related:** ADR-0005; AGENTS.md §9, §10; ROADMAP 3.2; spec §2, §5 (component #4)

## Context

Roadmap 3.2 (Milestone 3) calls for `HeapArray<T>`: a "fixed-size, bounds-checked heap array"
that is "optimized to avoid resize overhead" (spec component #4). The intent is a container
that sits between `std::array<T, N>` (size fixed at *compile* time, stack-resident) and
`std::vector<T>` (heap-resident but growable): a heap block whose size is a *runtime* value
fixed once at construction and never changed, with contiguous cache-friendly storage and a
bounds-checked accessor.

The implementation question is what backs the storage. The obvious "primitive" choice — a
`std::unique_ptr<T[]>` plus a size, or hand-rolled `operator new` storage with placement-new
— runs headlong into the project quality bar (AGENTS §10, "no broad lint disables"):

- `std::unique_ptr<T[]>` / `std::make_unique<T[]>(n)` name the type `T[]`, which
  `cppcoreguidelines-avoid-c-arrays` and `modernize-avoid-c-arrays` flag at every use site;
  clearing them would require suppressing those checks.
- Raw `operator new` storage additionally trips `cppcoreguidelines-owning-memory` and
  `cppcoreguidelines-pro-type-reinterpret-cast`, and puts exception-safe element
  construction/destruction on us.

ADR-0005 established the governing principle: *prefer standard owners and add invariants,
reaching for raw memory only where a component's purpose requires it.* A fixed-size heap
array does not require raw memory.

## Decision

`HeapArray<T>` **composes a private `std::vector<T>`** and exposes a deliberately
fixed-size, value-semantic surface over it:

- Construction fixes the size: `HeapArray(n)` (value-initialized), `HeapArray(n, value)`
  (filled), and `HeapArray(std::initializer_list<T>)`.
- The **growth API is not exposed** — there is no `push_back`, `resize`, `reserve`,
  `insert`, `capacity`, or `clear`. The size is immutable for the object's lifetime, so the
  container never reallocates.
- `at()` delegates to `std::vector::at`, inheriting its `std::out_of_range` throw;
  `operator[]` is unchecked with a debug assertion. `front`/`back`/`data`/`begin`/`end`/
  `size`/`empty`/`fill`/`swap` complete the surface.
- Special members follow the **rule of zero**: `std::vector` already provides deep-copy value
  semantics, a `noexcept` move that empties the source, and leak-free destruction.

## Alternatives Considered

- **`std::unique_ptr<T[]>` + size.** Rejected — a single tight allocation, but every mention
  of `T[]` trips the `avoid-c-arrays` checks, forcing suppressions the quality bar forbids;
  it also constrains *all* constructors to default-constructible `T` and double-initializes on
  fill (value-init then assign).
- **Raw `operator new` storage + placement new.** Rejected — reintroduces owning raw pointers,
  `reinterpret_cast`, and manual exception-safe lifetime management, contradicting ADR-0005 for
  no functional gain here.
- **Tell users to just use `std::vector`.** Rejected — `std::vector`'s growth API is exactly
  what invites accidental reallocation and size drift; the value of `HeapArray` is the
  type-level "this never grows" guarantee and the always-available bounds-checked ergonomics.

## Consequences

- Fully lint-clean (no `avoid-c-arrays`, `owning-memory`, `reinterpret-cast`, or pointer-
  arithmetic findings) with zero suppressions, and correct-by-construction lifetime/exception
  behavior inherited from `std::vector`.
- The fill and initializer-list constructors work for non-default-constructible `T`; only the
  sized `HeapArray(n)` constructor requires default-constructibility (it value-initializes),
  which is documented on the type.
- Minor footprint cost versus a bare `T* + size`: `std::vector` carries three pointers and may
  hold `capacity() >= size()`. This is an accepted trade for safety and lint-cleanliness; a
  future tighter-footprint variant, if ever needed, would supersede this ADR.
- Reinforces the Milestone-3 pattern from ADR-0005: build ownership primitives by composing
  and constraining standard owners.

## References

- `docs/specs/01_spec_util.md` §2 (component #4), §5 (Memory public surface).
- ROADMAP.md item 3.2.
- `src/main/cpp/it/d4np/util/heap_array.hpp`.
- ADR-0005 (ownership-by-composition precedent); AGENTS.md §9–§10.
