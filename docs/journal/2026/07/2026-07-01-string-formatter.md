# 2026-07-01 — StringFormatter (roadmap 5.3) — Milestone 5 complete

## What got done

- Implemented `it::d4np::util::StringFormatter` (component #12): a compile-time type-safe,
  `std::format`-like formatter. Format strings are the consteval-validated
  `FormatString<Args...>` type (the C++23 `std::format_string` technique in C++20 form):
  a malformed string (`"{"`, stray `"}"`, indices/specs) or a placeholder/argument count
  mismatch is a compile error at the call site, reported via an undefined non-constexpr
  function whose message string lands in the diagnostic. Arguments are constrained by the
  public `Formattable` concept: string-view-convertible, `char`, `bool`, the StringBuilder
  integer set, `float`, `double`. Grammar is positional-only `{}` plus `{{`/`}}` escapes.
- Rendering builds on `StringBuilder`: `format` returns `std::string`, `format_to` appends
  into a caller-supplied builder and returns it. Floating-point (deferred here from 5.2) uses
  `std::to_chars` (shortest round-trip) behind `__cpp_lib_to_chars >= 201611L`, with a
  classic-locale `ostringstream` fallback at `max_digits10` for older libc++; FP tests pin
  only values whose rendering agrees on both paths (`1.5`, `-2.25`, …).
- **ADR-0012** records the validation strategy (consteval ctor vs NTTP fixed-string vs
  runtime validation) and why rendering is *not* constexpr at the current toolchain floor
  (no `constexpr std::string` in GCC 11 / Clang 14). **Interpreter** was considered for the
  format mini-language and rejected in the patterns catalogue (single-pass scan, no AST).

## Design / correctness notes

- The validator `detail::check_format_string` is an ordinary constexpr function returning an
  error enum, so the compile-failure logic is unit-tested with `static_assert` (a build error
  itself cannot appear in a runtime suite).
- `FormatString`'s constructor is intentionally implicit (like `std::format_string`); the
  `FormatString<Args...>` alias uses `std::type_identity_t` to keep `Args...` non-deduced.
  Dynamic (runtime) format strings are intentionally not representable.
- Rendering helpers assume a *validated* string (every brace is part of `{}`, `{{`, `}}`),
  which makes `rest[i + 1]` reads safe; this invariant is documented on
  `detail::advance_to_placeholder`.

## Project state

- **Milestones 1–5 complete** (5.3 was the last M5 item). Version still `0.0.0`; no release
  PRs have been cut yet — per AGENTS §11 version bumps happen in dedicated release PRs.
- Verified locally on MSVC 14.51: build + `ctest` green; clang-format and clang-tidy
  (LLVM pip binaries) clean; `tools/consistency_lint.py` passing. See
  [[local-verify-toolchain]] for the box-specific invocations.

## How the next session resumes

- Next roadmap item: **6.1 — `Semaphore`** (component #9), a portable counting semaphore,
  opening Milestone 6 (Concurrency & Multithreading). C++20 has `std::counting_semaphore`;
  the design question for the ADR is what "portable" adds over it at the toolchain floor
  (macOS/libc++ quirks, timed waits) — check the spec wording before deciding to wrap vs
  reimplement. All M6 components must be TSan-verified (CI runs the tsan preset; not
  runnable locally on MSVC).
- With Milestone 5 done, a release PR (`0.5.0`-style bump per AGENTS §11) may be due —
  ask the maintainer whether to cut one before starting 6.1.
- One PR at a time: wait for the 5.3 PR to merge before branching.
