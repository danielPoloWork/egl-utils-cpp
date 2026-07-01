# 2026-07-01 — UniqueRef<T> (roadmap 3.1)

## What got done

- Implemented `it::d4np::util::UniqueRef<T>` (component #2, ADR-0005): a move-only, non-null
  unique-ownership smart pointer. No constructible null state — deleted default and
  `nullptr` constructors, no `operator bool`/`reset()`. Created via `make_unique_ref<T>()` or
  by adopting a non-null `std::unique_ptr<T>`.
- Composes a `std::unique_ptr<T>` member (leak-safe, `sizeof == sizeof(T*)`), so the type is
  fully clang-tidy-clean with no `cppcoreguidelines-owning-memory` suppression.
- C++20 `requires`-constrained derived→base converting move; rvalue-only `to_unique_ptr()`
  escape hatch; `swap`. Added to the umbrella header.
- Tests (`unique_ref_test.cpp`): compile-time contract via `static_assert` (not
  default/copy/null-constructible; move-only; thin), plus runtime ownership/lifetime,
  move-transfer, polymorphic conversion, and `to_unique_ptr` handoff using a live-instance
  counter (ASan/Valgrind-verified in CI).

## Project state

- Milestones 1–2 complete; Milestone 3 in progress (3.1 done; 3.2 HeapArray, 3.3
  StackAllocator, 3.4 ObjectPool remain). Version still `0.0.0`.
- Verified locally on MSVC 14.51: build + `ctest` green (13 cases / 42 assertions);
  clang-format and clang-tidy (LLVM 22) clean on the new header and test.

## How the next session resumes

- Next roadmap item: **3.2 — `HeapArray<T>`** (fixed-size, bounds-checked heap array,
  component #4). Header-only; will manage a contiguous heap buffer — decide bounds-check
  error model (assert vs. throwing `at()`), and mind `cppcoreguidelines-owning-memory` (prefer
  composing a `std::unique_ptr<T[]>` per the ADR-0005 precedent).
- One PR at a time: wait for the 3.1 PR to merge before branching 3.2.
