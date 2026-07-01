# ADR-0005: `UniqueRef<T>` non-null unique-ownership semantics

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Maintainer
- **Related:** AGENTS.md §5, §9; ROADMAP 3.1; spec §2, §5 (component #2)

## Context

Roadmap 3.1 (Milestone 3, Memory & Resource Management) calls for `UniqueRef<T>`: a
"non-null unique-ownership smart pointer that guarantees an always-valid instance at compile
time, with no null state" (spec component #2). `std::unique_ptr<T>` provides unique ownership
but is nullable — it is default-constructible to empty, constructible from `nullptr`, and
carries an `operator bool` precisely because callers must check for the empty state. A large
class of bugs comes from that reachable null. We want a type that removes it from the API
surface so "holds a valid object" is a type-level guarantee rather than a runtime check.

Two forces are in tension. (1) *Non-null* wants no empty state to exist. (2) *Unique
ownership* implies move semantics, and a move must leave the source in some state. C++ cannot
enforce no-use-after-move at compile time, so a moved-from object is unavoidable.

## Decision

`UniqueRef<T>` is a move-only smart pointer that owns exactly one heap object and exposes no
null state through its constructible API:

- The default constructor and the `std::nullptr_t` constructor are **deleted**; there is no
  `operator bool`, no `reset()`, and no null-yielding `release()`. Instances are created by
  the `make_unique_ref<T>(args...)` factory (which allocates, so it can only succeed with a
  valid object or throw) or by adopting a `std::unique_ptr<T>` whose non-null-ness is a
  documented precondition (asserted in debug).
- The **single** reachable empty state is a *moved-from* instance (including after the
  rvalue-only `to_unique_ptr()` escape hatch). Dereferencing a moved-from `UniqueRef` is
  undefined behavior, exactly as for a moved-from `std::unique_ptr`; debug builds assert.
- A C++20 `requires`-constrained converting constructor allows `UniqueRef<Derived>` →
  `UniqueRef<Base>` when the pointers are convertible, preserving polymorphic ownership.
- Ownership is implemented by **composing** a `std::unique_ptr<T>` member rather than a raw
  owning pointer. The type adds only the non-null invariant on top of a battle-tested owner;
  `sizeof(UniqueRef<T>) == sizeof(T*)`.

## Alternatives Considered

- **`gsl::not_null<std::unique_ptr<T>>`.** Rejected — it pulls in the GSL as a dependency
  (the library is zero-dependency, AGENTS §3) and `not_null` still permits a moved-from empty
  `unique_ptr` inside, so it does not tighten the invariant meaningfully.
- **A raw owning `T*` with hand-written `new`/`delete`.** Rejected — it reimplements lifetime
  management the standard library already gets right, and trips `cppcoreguidelines-owning-memory`,
  which would force a broad clang-tidy disable that AGENTS §10 forbids. Composition over
  `std::unique_ptr` is leak-safe and lint-clean without suppressions.
- **Make the type non-movable to keep the invariant absolute.** Rejected — it could not be
  returned from a factory or stored in containers, defeating "unique ownership". The
  moved-from caveat is the accepted, idiomatic cost.
- **Keep an `operator bool`/`release()` for parity with `unique_ptr`.** Rejected — those exist
  to manage the empty state this type is designed not to have; omitting them is the point.

## Consequences

- Callers holding a `UniqueRef<T>` never null-check: the object is guaranteed present unless
  they moved from it, which is visible at the call site. This removes a whole bug class from
  consumer code.
- The moved-from state is the one sharp edge; it is documented on the type and covered by
  tests that assert `get() == nullptr` after a move. Static analysis (`bugprone-use-after-move`)
  catches accidental use in consumer code.
- Custom deleters and array ownership are intentionally out of scope; array/bulk ownership is
  served by `HeapArray<T>` (roadmap 3.2) and pooled reuse by `ObjectPool<T>` (3.4).
- The composition choice sets the pattern for the rest of Milestone 3: prefer standard owners
  and add invariants, reaching for raw memory only where a component's purpose requires it.

## References

- `docs/specs/01_spec_util.md` §2 (component #2), §5 (Memory public surface).
- ROADMAP.md item 3.1.
- `src/main/cpp/it/d4np/util/unique_ref.hpp`.
- AGENTS.md §3 (zero-dependency), §9 (coding conventions), §10 (no broad lint disables).
