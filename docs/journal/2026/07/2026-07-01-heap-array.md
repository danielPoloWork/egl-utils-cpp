# 2026-07-01 — HeapArray<T> (roadmap 3.2)

## What got done

- Implemented `it::d4np::util::HeapArray<T>` (component #4, ADR-0006): a fixed-size,
  bounds-checked heap array with deep-copy value semantics. Sized / fill / initializer-list
  construction; `at()` throws `std::out_of_range`, `operator[]` is unchecked (debug assert);
  `front`/`back`/`data`/`begin`/`end`/`cbegin`/`cend`/`fill`/`swap`. No growth API, so the
  container never reallocates. Added to the umbrella header.
- Storage composes a private `std::vector<T>` (rule of zero). The "primitive" backings —
  `std::unique_ptr<T[]>` and raw `operator new` — were rejected because `T[]` trips
  `avoid-c-arrays` and raw storage trips `owning-memory`/`reinterpret-cast`; clearing those
  would need suppressions the quality bar forbids. Documented in ADR-0006, reinforcing the
  ADR-0005 "compose and constrain standard owners" pattern.
- Tests (`heap_array_test.cpp`, 12 cases): construction variants, checked/unchecked access,
  deep-copy independence, move-empties-source, iteration, `fill`/`swap`, and a live-instance
  counter proving every element is destroyed exactly once (ASan/Valgrind-verified in CI).

## Project state

- Milestones 1–2 complete; Milestone 3 in progress (3.1, 3.2 done; 3.3 StackAllocator, 3.4
  ObjectPool remain). Version still `0.0.0`.
- Verified locally on MSVC 14.51: build + `ctest` green (25 cases / 89 assertions);
  clang-format and clang-tidy (LLVM 22) clean on the new header and test.

## How the next session resumes

- Next roadmap item: **3.3 — `StackAllocator<Size>`** (stack-backed bump allocator, component
  #3). Unlike 3.1/3.2 this genuinely manages raw storage (a `std::byte[Size]` buffer + a bump
  offset), so expect to confront alignment, `std::launder`, and the `owning-memory`/pointer-
  arithmetic lint that composition avoided here — this is the "purpose requires raw memory"
  case ADR-0005 carves out.
- One PR at a time: wait for the 3.2 PR to merge before branching 3.3.
