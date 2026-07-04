# 2026-07-04 — JsonParser (roadmap 8.2) — Milestone 8 complete

## What got done

- Implemented `it::d4np::util::JsonParser` (component #23): a **non-allocating, pull-style
  (SAX)** JSON parser over a `std::string_view` — the Milestone-8 closer, and header-only
  (it reads a view and touches no OS API).
- **Design (ADR-0022): pull events, not a DOM.** "Non-allocating" (spec §3) rules out an
  owning tree — a DOM must heap-allocate its containers. Instead `next()` walks the document
  once and returns a `JsonEvent` (`begin_object`/`end_object`, `begin_array`/`end_array`,
  `key`, scalars `string`/`number`/`boolean`/`null_value`, then `end`) whose payload is a
  `string_view` borrowing the source. Parsing copies nothing and touches no heap. This is the
  `StringSplitter` lazy-cursor idiom lifted to a grammar.
- **Borrow, convert on demand.** Scalars are the raw source slice (numbers = the literal;
  strings/keys = inner bytes, escapes intact). `to_number<T>()` = whole-token `std::from_chars`
  (so `1.5`→int fails rather than truncating, overflow → `nullopt`), no allocation; `to_bool()`
  reads the literal; `decode_string()` applies the JSON escapes (`\uXXXX` incl. UTF-16
  surrogate pairs → UTF-8) and allocates only the destination string, only when asked.
- **Allocation-free bounded nesting.** Nesting lives in a fixed `std::array<Frame, MaxDepth>`
  (template `MaxDepth`, default 64) inside the parser — no heap, no unbounded recursion. Input
  deeper than `MaxDepth` is a clean `error`, so the adversarial-depth guard falls out of the
  non-allocating design for free.
- **Value-or-error, sticky, positioned.** Malformed input never throws: `next()` returns an
  `error` event and latches; `error_message()`/`error_offset()` describe the first failure by
  message and byte offset. Same boundary discipline as `CliParser` (ADR-0021). Grammar is
  strict RFC 8259 (no trailing commas, no leading zeros, control bytes must be escaped, exactly
  one top-level value) — strictness is a security property for untrusted input.
- No new design pattern: the pull cursor is the `StringSplitter` idiom; **Interpreter** was
  considered and rejected (single-pass scan, no AST — as for `StringFormatter`, ADR-0012).
  Patterns catalogue unchanged (noted in the ADR).

## Test notes

- 14 `TEST_CASE`s / 186 assertions: full event streams (scalars, objects, arrays, nesting,
  empties), `to_number` (int/double/overflow/fractional-into-int), `to_bool`, `decode_string`
  (basic escapes, `\u` BMP, surrogate pair, and five malformed-escape classes), every
  malformed-document class (trailing commas, unterminated string/container, unescaped control
  char, bad numbers `01`/`-`/`1.`/`1e`/`.5`, unknown literal, missing colon, non-string key,
  trailing chars), the byte-accurate error offset, and `MaxDepth` (exactly-at-limit accepted,
  over-limit a bounded error).
- **Gotcha folded in:** the `€`/`😀` test inputs must be built with a `'\x5C'`
  backslash char + `"u20AC"` rather than a literal `\u…`, because a literal universal-character
  -name in the source is compiled to the character itself (would make the escape test vacuous).
- Local gate green: MSVC build + full suite (189 cases / 3642 assertions), `clang-format`
  clean, `clang-tidy` clean (folded in `modernize-use-designated-initializers` for the
  `JsonEvent` constructions and avoided `bugprone-unchecked-optional-access` /
  `modernize-raw-string-literal`). Sanitizers/Valgrind run in CI (header-only, no threads).

## Project state

- **Milestones 1–8 complete.** Only Milestone 9 (I/O & Networking) and Milestone 10 (Hardening
  & 1.0) remain. Version `0.0.0`; the release PR (now M5–M8) is on offer — a separate
  maintainer action per AGENTS §6.1/§11.

## How the next session resumes

- Next roadmap item: **9.1 — `FileStream`** (component #16), "RAII buffered file wrapper".
  This opens Milestone 9, the OS-API-heavy compiled tier — so it belongs in the STATIC tier
  (ADR-0004), like `StackTrace`/`Logger`: declarations in `file_stream.hpp`, definitions in
  `file_stream.cpp`, OS headers out of consumer TUs. Design questions for its ADR: the handle
  abstraction (`FILE*` vs raw `fd`/`HANDLE` — buffering is the requirement, so `std::FILE` +
  RAII is the low-effort fit, but a raw descriptor gives more control for the later
  `TcpSocket`), the buffering policy (rely on stdio's buffer vs an owned buffer), the
  open/read/write/seek/flush surface and its error model (value-or-error `expected`-like vs
  `errno`/status), and move-only ownership semantics. Consider a milestone release PR first
  (M5–M8) before starting 9.1.
- One PR at a time: wait for the 8.2 PR to merge before branching 9.1.
