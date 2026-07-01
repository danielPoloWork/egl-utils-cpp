# 2026-07-01 — StringSplitter (roadmap 5.1)

## What got done

- Implemented `it::d4np::util::StringSplitter` (component #11): a zero-allocation, `constexpr`
  string splitter. It is a lazy range — `begin()`/`end()` yield a forward iterator whose tokens
  are `std::string_view`s borrowing the original text (no copies, no heap). Splitting is
  faithful: `n` delimiter occurrences → `n + 1` tokens, so empty/leading/trailing fields are
  preserved, and an empty input yields one empty token. Added to the umbrella header.
- Deliberate API choices (documented in the header, no ADR — a simple utility like type_traits/
  hash): the delimiter is a whole `std::string_view` (multi-char sequences work; pass a literal
  like `","` for one char), and there is **no `char` overload** — that keeps the splitter
  trivially copyable and free of dangling (a `char`-backed `string_view` member would alias a
  temporary or break on copy).

## clang-tidy notes

- `readability-redundant-member-init` (drop `{}` on `string_view` members — the default ctor
  already empties them) and `readability-convert-member-functions-to-static` (`end()` uses no
  members → made `static`; range-for still calls it fine).
- Reminder confirmed: when running clang-tidy **standalone** on a test TU, pass
  `-DDOCTEST_CONFIG_USE_STD_HEADERS` (the `util_tests` target defines it); without it, MSVC's
  `<string_view>` + doctest's forward-declared `std::basic_ostream` produce a false error that
  CI does not see. See [[local-verify-toolchain]].

## Project state

- Milestones 1–4 complete; Milestone 5 in progress (5.1 done; 5.2 StringBuilder, 5.3
  StringFormatter remain). Version still `0.0.0`.
- Verified locally on MSVC 14.51: build + `ctest` green (71 cases / 252 assertions, incl.
  compile-time `static_assert`s); clang-format and clang-tidy (LLVM 22.1.7) clean.

## How the next session resumes

- Next roadmap item: **5.2 — `StringBuilder`** (component #10), a preallocated fluent builder.
  Unlike the splitter this one *owns* a growable buffer (`std::string` or a `std::vector<char>`)
  with `reserve`, fluent `append(...)` returning `*this`, chaining of strings/chars/numbers, and
  a `view()`/`str()` extraction. Decide the numeric-append story (std::to_chars for zero-alloc
  numeric formatting).
- One PR at a time: wait for the 5.1 PR to merge before branching 5.2.
