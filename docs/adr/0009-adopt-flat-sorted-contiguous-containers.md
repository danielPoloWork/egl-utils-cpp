# ADR-0009: Adopt flat (sorted-contiguous) associative containers

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Maintainer
- **Related:** ADR-0006; AGENTS.md §9, §10; ROADMAP 4.1–4.3; spec §2, §4, §5 (components #13, #14, #15)

## Context

Milestone 4 (Contiguous Containers) specifies `FlatSet<T>` (4.1, component #15),
`FlatMap<Key, Value>` (4.2, component #14), and `CircularBuffer<T>` (4.3, component #13). The
spec's architectural stance (§4) is explicit: *"contiguous storage for cache locality
(FlatMap/FlatSet are sorted arrays with binary search)."* The design question for the
associative pair is the data structure: a node-based tree (like `std::set`/`std::map`) versus
a single sorted contiguous array.

Node-based trees give O(log n) insert/erase/lookup and stable iterators/references, but each
node is a separate heap allocation, so iteration chases pointers across the heap — cache-hostile
— and per-element overhead is high. For the read-heavy, insert-light workloads these utilities
target, a sorted array wins on the axes that matter here: memory density, iteration speed, and
allocation count.

## Decision

We implement the associative containers of Milestone 4 as **flat containers: a single sorted,
contiguous `std::vector`** ordered by a comparator, with binary-search lookup. This mirrors the
model standardized as `std::flat_set`/`std::flat_map` in C++23, which the project cannot yet
depend on across its C++20 toolchain matrix.

Concrete consequences of the flat model, applied uniformly:

- **Complexity:** lookup / `lower_bound` / `upper_bound` are O(log n); `insert` and `erase` are
  O(n) because they shift the tail. This is the deliberate trade — cheap reads and iteration in
  exchange for costlier mutation.
- **Immutable elements / const iterators:** keys are stored in sorted order, so mutating an
  element in place could break the invariant. All iterators are `const`; mutation goes through
  `insert`/`erase`.
- **Comparator as a type parameter** (default `std::less<T>`), stored in the container;
  equivalence is "neither element compares less than the other," matching the standard
  associative-container contract.
- **Storage by composition over `std::vector`** — leak-safe, contiguous, and lint-clean, per
  the ADR-0006 precedent. `reserve` is exposed to amortize bulk insertion; the growth API that
  would violate sortedness (`push_back`, etc.) is not.

`FlatSet<T>` (this PR) is the first application; `FlatMap<Key, Value>` (4.2) extends the same
model to key/value pairs and adds the `constexpr` support its roadmap entry calls for; the two
share this rationale.

## Alternatives Considered

- **Node-based (`std::set`/`std::map` internally or a hand-rolled tree).** Rejected — poor
  cache locality, one allocation per element, and it contradicts the spec's explicit
  sorted-array direction.
- **Hash-based (`std::unordered_*`).** Rejected — unordered iteration, no ordered queries
  (`lower_bound`/range), and it does not match the "sorted array" requirement. A hashed flat
  container could be a separate future component.
- **Wait for / require C++23 `std::flat_set`.** Rejected — not available across the C++20
  baseline toolchains (AGENTS §10 build matrix); this provides the capability now with a
  compatible surface.

## Consequences

- Predictable, cache-friendly reads and iteration with a compact footprint; mutation cost is
  O(n) and documented, steering users toward build-then-query or `reserve`-then-bulk-insert
  usage.
- Iterators and references are invalidated by any insert/erase (as with `std::vector`); this is
  documented on the types.
- One rationale covers all Milestone-4 associative containers; 4.2/4.3 reference this ADR
  rather than re-arguing the model. A future need for stable references or O(log n) mutation
  would require a node-based variant under a new ADR.

## References

- `docs/specs/01_spec_util.md` §2 (components #13–#15), §4 (logical architecture — sorted
  arrays), §5 (Containers public surface).
- ROADMAP.md items 4.1–4.3.
- `src/main/cpp/it/d4np/util/flat_set.hpp`.
- ADR-0006 (composition over `std::vector`); AGENTS.md §9–§10; C++23 `std::flat_set` (prior art).
