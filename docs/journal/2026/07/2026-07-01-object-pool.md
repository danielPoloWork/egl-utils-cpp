# 2026-07-01 — ObjectPool<T> (roadmap 3.4) — Milestone 3 complete

## What got done

- Implemented `it::d4np::util::ObjectPool<T>` (component #1, ADR-0008): a thread-safe,
  fixed-capacity pool of pre-allocated objects — the library's **first catalogued design
  pattern** (Object Pool, Creational). `try_acquire()` returns `std::optional<Handle>`
  (engaged when a slot is free, `std::nullopt` when exhausted); the move-only RAII `Handle`
  returns its slot to the pool on destruction. O(1) acquire/release via a mutex-guarded
  pre-sized free-slot index stack. Value-initialized or factory-constructed slots.
  `capacity`/`available`/`in_use`. Non-copyable/non-movable. Added to the umbrella header.
- First component with a concurrency contract. Kept the release path genuinely `noexcept`
  (pre-sized free stack → the return write can't allocate), used `std::scoped_lock`, and
  verified the design is race-free with a 4-thread contended acquire/release test that the CI
  **TSan** job exercises.
- Documented the pattern: ADR-0008 + the first row in `docs/patterns/README.md` (Object Pool
  → ADR-0008 → object_pool.hpp), establishing the ADR-plus-catalogue workflow.

## clang-tidy notes (local MSVC vs CI libstdc++)

- `modernize-use-scoped-lock` (use `std::scoped_lock`), `bugprone-exception-escape` (the
  noexcept release path must not call throwing `vector::push_back` — switched to a pre-sized
  index stack), and `bugprone-unchecked-optional-access` all fired locally this time.
- The optional-access check does **not** understand doctest's `REQUIRE`; it only accepts
  `if (opt)` / `if (opt.has_value())` guards (and early `continue`). Restructured the tests to
  use those guards instead of `REQUIRE(...); opt->...`. See [[local-verify-toolchain]].

## Project state

- **Milestones 1, 2, 3 complete.** Version still `0.0.0` (release PRs are cut separately).
  Next milestone is 4 (Contiguous Containers).
- Verified locally on MSVC 14.51: build + `ctest` green (38 cases / 137 assertions);
  clang-format and clang-tidy (LLVM 22.1.7) clean on the new header and test.

## How the next session resumes

- Next roadmap item: **4.1 — `FlatSet<T>`** (sorted-vector set, component #15), the first item
  of Milestone 4 (Contiguous Containers). Header-only; a sorted `std::vector` with binary-search
  lookup and unique-key insertion. Consider a custom comparator template parameter and whether
  to offer `constexpr` support (FlatMap 4.2 explicitly wants constexpr).
- One PR at a time: wait for the 3.4 PR to merge before branching 4.1.
