# ADR-0010: `FlatMap<Key, Value>` constexpr support and construction strategy

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Maintainer
- **Related:** ADR-0009; AGENTS.md §9, §10; ROADMAP 4.2; spec §2, §5 (component #14)

## Context

Roadmap 4.2 specifies `FlatMap<Key, Value>` as a "sorted contiguous-array map **with constexpr
support**" (component #14). It extends the flat model of ADR-0009 to key/value pairs, but the
constexpr requirement forces two decisions the set did not face.

1. **Element type.** A node-free sorted array must shift elements on insert/erase, which
   requires the element type to be move-assignable. `std::pair<const Key, Value>` (the
   `std::map` element) is not — the `const` key blocks assignment — so it cannot back a flat,
   shift-based container.
2. **Compile-time construction.** Under the C++20 baseline (AGENTS §10), `std::vector` and its
   `insert`/`erase`, plus `std::lower_bound`, are `constexpr`; but `std::sort` is **not**
   `constexpr` until C++26. FlatSet (ADR-0009) sorts with `std::sort` in its constructors, so it
   is not constexpr-constructible. FlatMap must be, per its roadmap entry.

Note also that a `std::vector` cannot persist as a `constexpr` global (its storage must be
freed before the end of constant evaluation), so "constexpr support" means *usable within a
constant expression* — building and querying a `FlatMap` inside a `constexpr` function behind a
`static_assert` — not declaring a `constexpr FlatMap` object at namespace scope.

## Decision

`FlatMap` stores `std::pair<Key, Value>` (a **non-const** key) in a sorted `std::vector`, and
its **entire public interface is `constexpr`**. To keep construction correct at compile time
without giving up runtime efficiency, the bulk constructors branch on
`std::is_constant_evaluated()`:

- **During constant evaluation:** build by repeated sorted `insert` (`std::lower_bound` +
  `std::vector::insert`, both `constexpr`). O(n²), but paid at compile time.
- **At runtime:** `std::stable_sort` then `std::unique` — O(n log n). `stable_sort` + `unique`
  keeps the *first* entry for a duplicated key, matching the insertion path's "first key wins".

The non-const key is an internal detail: all iterators are `const` (so a key cannot be mutated
in place and break ordering), and values are mutated only through `at()` / `operator[]`.

## Alternatives Considered

- **Back it with `std::array<pair<Key,Value>, N>` for a purely compile-time map.** Rejected —
  it would fork FlatMap from FlatSet into a fixed-size, compile-time-only type, losing the
  runtime map the spec also implies and duplicating the flat machinery.
- **Write a hand-rolled `constexpr` sort and always use it.** Rejected — an O(n log n) constexpr
  sort is more code to verify, and at runtime the standard `std::sort`/`stable_sort` are better
  tested and optimized. `is_constant_evaluated()` lets each context use the right tool.
- **Store `std::pair<const Key, Value>` like `std::map`.** Rejected — not move-assignable, so it
  cannot be shifted within a flat array. The `const`-key guarantee is instead provided by
  exposing only const iterators.
- **Require callers to pass pre-sorted input.** Rejected — error-prone and surprising versus the
  standard associative-container contract.

## Consequences

- `FlatMap` can be constructed and queried in constant expressions (validated by `static_assert`
  tests); at runtime it retains O(n log n) construction and O(log n) lookup.
- The non-const stored key is safe because the public surface never hands out a mutable key;
  this is documented on the type.
- Compile-time construction is O(n²); acceptable for the small, fixed tables constexpr maps are
  used for, and documented. Runtime construction is unaffected.
- Extends ADR-0009 rather than superseding it; FlatSet remains non-constexpr (no such
  requirement) and could adopt the same `is_constant_evaluated` technique later if needed.

## References

- `docs/specs/01_spec_util.md` §2 (component #14), §5 (Containers public surface).
- ROADMAP.md item 4.2.
- `src/main/cpp/it/d4np/util/flat_map.hpp`.
- ADR-0009 (flat container model); AGENTS.md §9–§10; `std::is_constant_evaluated` (C++20).
