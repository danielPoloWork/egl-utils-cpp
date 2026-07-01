# 2026-07-01 — StackAllocator<Size> (roadmap 3.3)

## What got done

- Implemented `it::d4np::util::StackAllocator<Size>` (component #3, ADR-0007): a monotonic
  bump allocator over an in-object `alignas(std::max_align_t) std::array<std::byte, Size>`
  buffer. `allocate(bytes, alignment)` bumps an offset via `std::align` and throws
  `std::bad_alloc` on exhaustion; `allocate_uninitialized<T>(count)` is the typed wrapper.
  Reclamation is wholesale (`reset()`) or scoped (`mark()`/`rewind()`) — no per-allocation
  free. Non-copyable and non-movable (outstanding pointers alias the buffer). Added to the
  umbrella header.
- First component that genuinely manages raw storage (the ADR-0005 exception). Kept it
  suppression-free: `std::array` instead of `T[]` (avoids `avoid-c-arrays`); cursor math via
  `std::next`/`std::distance` and `static_cast<std::byte*>` (avoids `pro-bounds-pointer-
  arithmetic` / `reinterpret-cast`); buffer value-initialized (satisfies `pro-type-member-init`,
  at a one-time zero-fill cost).
- Tests (`stack_allocator_test.cpp`, 7 cases): capacity accounting, alignment (checked with a
  `std::align`-based probe, no `reinterpret_cast`), sequential bumping, `std::bad_alloc` on
  exhaustion, and the `reset`/`mark`/`rewind` reclamation model.

## Project state

- Milestones 1–2 complete; Milestone 3 nearly done (3.1, 3.2, 3.3 ✅; only 3.4 ObjectPool
  remains). Version still `0.0.0`.
- Verified locally on MSVC 14.51: build + `ctest` green (32 cases / 111 assertions);
  clang-format and clang-tidy (LLVM 22.1.7) clean on the new header and test.

## How the next session resumes

- Next roadmap item: **3.4 — `ObjectPool<T>`** (thread-safe O(1) object pool, component #1) —
  the last M3 item, which completes Milestone 3. It is a **concurrency** component: it will
  need a mutex (or lock-free free-list) and must be verified under **TSan**, not just
  ASan/Valgrind. Expect a free-list over pre-allocated slots and acquire/release semantics
  (likely returning a pooled handle/`UniqueRef`-like guard).
- One PR at a time: wait for the 3.3 PR to merge before branching 3.4.
