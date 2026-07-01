# 2026-07-01 — CircularBuffer<T> (roadmap 4.3) — Milestone 4 complete

## What got done

- Implemented `it::d4np::util::CircularBuffer<T>` (component #13, ADR-0011): a fixed-capacity
  FIFO ring buffer over a pre-sized `std::vector`, with O(1) push/pop and no reallocation.
  Two push policies — `try_push` (rejects when full, lossless back-pressure) and
  `push_overwrite` (drops the oldest, latest-wins for streams); `pop()` → `std::optional<T>`;
  `front`/`back`/`size`/`capacity`/`empty`/`full`/`clear`. Added to the umbrella header.
- Full/empty distinguished by a `count_` (whole capacity usable). Index wrapping uses
  compare-and-subtract (`advance`/`tail_index`/`back_index`), not `%` — faster and no
  divide-by-zero. Rule of zero (copy/move/destroy from the vector + indices). Requires
  default-constructible + assignable `T` (slots value-initialized and reused).
- Tests (`circular_buffer_test.cpp`, 7 cases): FIFO order, full/empty accounting, both push
  policies, wrap-around under interleaved push/pop, `clear`, and a move-only element
  (`std::unique_ptr<int>`).

## Project state

- **Milestones 1, 2, 3, 4 complete.** Version still `0.0.0` (release PRs are cut separately).
  Next milestone is 5 (Zero-Copy Strings).
- Verified locally on MSVC 14.51: build + `ctest` green (63 cases / 241 assertions);
  clang-format and clang-tidy (LLVM 22.1.7) clean on the new header and test (only a comment
  alignment nit from clang-format, auto-fixed).

## How the next session resumes

- Next roadmap item: **5.1 — `StringSplitter`** (component #11), the first item of Milestone 5
  (Zero-Copy Strings). A `std::string_view`-based, zero-allocation splitter: iterate/collect
  substrings around a delimiter (char or string_view) without copying. Consider an iterator or
  a callback/`next()` style, handling of empty tokens and trailing delimiters, and a `constexpr`
  path (string_view ops are constexpr). No ADR likely needed unless a non-obvious API decision
  arises.
- One PR at a time: wait for the 4.3 PR to merge before branching 5.1. **Close the M4 milestone
  on merge** (4.3 is its last item).
