# 2026-07-03 — CliParser (roadmap 8.1)

## What got done

- Implemented `it::d4np::util::CliParser` (component #22): a typed command-line argument
  parser with auto-generated help — the first Milestone-8 component and a return to the
  **header-only** tier (it consumes the `char**` the runtime already produced and touches
  no OS API, so unlike `StackTrace`/`Logger` it needs no compiled tier).
- **Design (ADR-0021): direct typed binding + value-or-error.** `add_flag`/`add_option`/
  `add_positional` capture a reference to a caller-owned variable; a successful parse writes
  the converted value straight in — no `result.get<T>("name")` bag, no RTTI, no per-value
  allocation, and a wrong-type read is impossible because the target type *is* the binding.
  Conversion goes through the `CliValue` concept (`detail::cli_parse` overloads for
  `std::string`, `bool`, all integers, `float`/`double`) with **whole-token** `from_chars`
  so `12x` is `invalid_value`, not a silent `12`. `parse` returns a `ParseResult`
  (success / `help_requested` / a `CliError` code + message), contextually `bool`, and never
  throws on user input; only registration misuse (empty/duplicate names, a clash with the
  auto-registered `--help`/`-h`) throws `std::logic_error` — the library's convention that
  bad `argv` is ordinary input and only programmer error throws.
- **Grammar:** GNU/POSIX — `--long`, `--long=value`, `-n`, `-n value`, `-n=value`, attached
  `-nvalue`, clustered short flags `-ab` (optionally ending in a value option that takes the
  cluster remainder), a lone `--` terminating options, positionals in registration order.
  `help()` renders the usage line, description, and column-aligned sections straight from the
  registered specs via `StringBuilder`, so it cannot drift.
- No new design pattern: the fluent `add_*` chain is the same idiom for which GoF **Builder**
  was already rejected on `StringBuilder` (noted in the ADR; patterns catalogue unchanged).

## Fixes folded in during implementation

The draft header carried three latent defects, caught while wiring the build and tests:

- `std::string{1, ch}` (three sites) selected the `initializer_list<char>` constructor and
  produced a two-char string `"\x01" + ch` — corrupting every short-option label/message.
  Switched to `std::string(1, ch)`. A regression test pins `unknown option '-x'` exactly.
- `mutable_option` called `std::distance(options_.data(), option)` mixing `OptionSpec*` and
  `const OptionSpec*`, which fails template deduction (would not compile). Fixed by casting
  the base pointer to `const OptionSpec*`.
- `help()` used `std::max` without `#include <algorithm>`. Added it.

## Test notes

- 20 `TEST_CASE`s / 83 assertions covering the whole grammar, every `CliError` category,
  `--help`/`-h` (including mid-cluster and in the presence of a required-but-absent
  positional), the argc/argv overload skipping `argv[0]`, last-value-wins on repeats,
  re-parse seen-state reset, and all six registration-misuse throws. Help assertions check
  substrings (usage line, `[OPTIONS]`, `<input>`, `-c, --count N`, `(required)`).
- Local gate green: MSVC build + full suite (175 cases / 3456 assertions), `clang-format`
  clean, `clang-tidy` clean on the test TU + header (the sole finding, a missing
  `[[nodiscard]]` on `make_named_spec`, was fixed). Sanitizers run in CI only (MSVC rejects
  the GNU `-fsanitize` flags).

## Project state

- Milestones 1–7 complete; Milestone 8 in progress (8.1 done; only 8.2 `JsonParser` remains).
  Version `0.0.0`; release PR (M5–M7) still on offer.

## How the next session resumes

- Next roadmap item: **8.2 — `JsonParser`** (component #23), "lightweight non-allocating JSON
  parser that operates directly on string views". Design questions for its ADR: the value
  model (a view-backed DOM vs a SAX/pull tokenizer — "non-allocating over string views"
  points at borrowing spans of the source and lazy numeric conversion, echoing
  `StringSplitter`), the error model (reuse the `CliParser`/spec value-or-error shape —
  a `ParseResult`-like outcome with a position, never throw on malformed input), number
  handling (`from_chars`, lazy vs eager), string handling (escapes force a copy — decide
  where owned storage lives when the source can't be borrowed verbatim), and depth/size
  guards against adversarial input. Closing 8.2 completes Milestone 8 → offer the release PR.
- One PR at a time: wait for the 8.1 PR to merge before branching 8.2.
