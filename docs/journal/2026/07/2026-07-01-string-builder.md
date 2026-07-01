# 2026-07-01 — StringBuilder (roadmap 5.2)

## What got done

- Implemented `it::d4np::util::StringBuilder` (component #10): a fluent, preallocation-friendly
  string builder over an owned `std::string`. Chainable `append` (returns `*this`) and
  `operator<<` synonyms for `std::string_view`, `char`, and integers; `reserve`/`clear`/`size`/
  `capacity`/`empty`; extraction via `view()`, `str() const&` (copy), and `str() &&` (moves the
  buffer out and leaves the builder empty and reusable). Added to the umbrella header.
- Integers are rendered with `std::to_chars` (allocation-free, locale-independent) via an
  `AppendableInteger` concept (integers except `bool` and the character types). **Floating-point
  is intentionally out of scope**: libc++'s `std::to_chars(double)` support is uneven across the
  toolchain matrix, so FP formatting is left to `StringFormatter` (5.3). Documented on the type.
- **Design-pattern call:** despite the name, `StringBuilder` is a fluent accumulator, not the
  GoF **Builder** (no director/product/representation split). Per AGENTS §8 ("never force-fit"),
  it is recorded in the patterns catalogue's **Rejected** table with the reason rather than
  adopted — the first entry in that table.

## clang-tidy / correctness notes

- `str() &&` uses `std::string result = std::move(buffer_); buffer_.clear(); return result;` —
  `std::move` alone leaves a `std::string` in a valid-but-*unspecified* state, so the explicit
  `clear()` makes "empty after extraction" a real guarantee (the test relies on it).
- `bugprone-use-after-move` on the post-`std::move(builder).str()` check is a syntactic match;
  NOLINT'd because the empty state is guaranteed by `str() &&`. Numeric buffer end computed with
  `std::next`/`std::distance` (no pointer arithmetic). See [[local-verify-toolchain]].

## Project state

- Milestones 1–4 complete; Milestone 5 in progress (5.1, 5.2 done; only 5.3 StringFormatter
  remains). Version still `0.0.0`.
- Verified locally on MSVC 14.51: build + `ctest` green (79 cases / 271 assertions);
  clang-format and clang-tidy (LLVM 22.1.7) clean.

## How the next session resumes

- Next roadmap item: **5.3 — `StringFormatter`** (component #12), a compile-time type-safe
  formatter (spec: "similar to std::format"). The last M5 item, completing Milestone 5. Expect a
  `constexpr`-checkable format string (placeholder parsing, arg-count/type checking at compile
  time via `consteval`/`static_assert`), building on StringBuilder for output. This one is novel
  enough to warrant an ADR (compile-time format-string validation strategy). It is also the
  natural home for the floating-point formatting deferred from StringBuilder.
- One PR at a time: wait for the 5.2 PR to merge before branching 5.3.
