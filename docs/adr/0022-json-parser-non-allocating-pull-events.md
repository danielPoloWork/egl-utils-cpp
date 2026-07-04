# ADR-0022: `JsonParser` non-allocating pull (SAX) event model over string views

- **Status:** Accepted
- **Date:** 2026-07-04
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #23), §3 (zero-copy / non-allocating), §5 (error model),
  roadmap 8.2, [ADR-0021](0021-cli-parser-typed-binding-value-or-error.md) (the same
  value-or-error convention), the `StringSplitter` lazy-view precedent,
  [ADR-0012](0012-string-formatter-compile-time-validation.md) (the Interpreter rejection this
  echoes)

## Context

The spec requires `JsonParser` (component #23): a "lightweight, **non-allocating** JSON parser
that operates directly on string views". "Non-allocating" is the load-bearing word and it
forecloses the obvious design. Three questions decide the shape. **What does parsing
produce?** — a materialized DOM (an owning tree of nodes) or a stream of events over the
untouched source. **How is malformed input reported?** — JSON from the network or a file is
adversarial by default, so the boundary must survive garbage without unwinding. **How is
unbounded input contained?** — a recursive-descent parser over attacker-controlled nesting is
a stack-overflow waiting to happen, and a heap-backed nesting stack is itself an allocation.

Forces: spec §3's zero-copy pillar (borrow via `std::string_view`/`std::span`, don't copy);
the library's convention that user-driven input never throws and only programmer misuse does
(ADR-0021's `CliParser`, `Stopwatch`, `TaskFuture`); the existing `StringSplitter` precedent
of a lazy, allocation-free view over borrowed text; and the zero-dependency contract (spec §3)
that rules out simdjson/RapidJSON/nlohmann.

## Decision

**`JsonParser` is a pull (SAX-style) tokenizer: `next()` walks the document once and returns a
`JsonEvent` whose payload is a `std::string_view` borrowing the source. Parsing allocates
nothing and copies nothing; conversion is opt-in and separate; malformed input latches a
sticky `error` event; nesting is bounded by a compile-time `MaxDepth`.**

- **Pull events, not a DOM.** A DOM must own its arrays and objects, which means heap
  allocation per container — incompatible with "non-allocating". Instead `next()` yields a
  flat pre-order stream: `begin_object`/`end_object`, `begin_array`/`end_array`, `key`, and
  the scalars (`string`, `number`, `boolean`, `null_value`), terminated by `end`. The
  document `{"a":1,"b":[true,null]}` becomes exactly ten events. This is the `StringSplitter`
  model (a lazy cursor over borrowed text) extended to a grammar.
- **Borrow, then convert on demand.** Scalar events carry the *raw* source slice: numbers are
  the literal, strings/keys are the inner bytes with escapes intact (quotes excluded). Nothing
  is decoded during the walk. `to_number<T>()` runs `std::from_chars` over the literal
  (whole-token, so `1.5` into an integer fails rather than truncating) and never allocates;
  `to_bool()` reads the literal; `decode_string()` applies the JSON escape rules (`\uXXXX`
  with UTF-16 surrogate pairs, encoded to UTF-8) and allocates **only** the destination string,
  **only** when the caller asks. A slice with no backslash needs no decode at all.
- **Allocation-free, bounded nesting.** Object/array nesting is tracked in a fixed
  `std::array<Frame, MaxDepth>` (a two-`bool`-plus-flag frame per level) held inside the
  parser — no heap, and no unbounded recursion. `MaxDepth` is a template parameter (default
  64); input deeper than that yields an `error`, not a crash. The depth guard is therefore a
  free by-product of the non-allocating design, not a bolted-on limit.
- **Value-or-error, sticky, positioned.** Malformed input is ordinary input: `next()` returns
  an `error` event (never throws) and latches — every later call returns `error`, and
  `error_message()` / `error_offset()` describe the *first* failure by message and byte
  offset. `done()` / `failed()` probe the state. This is the same boundary discipline as
  `CliParser` (ADR-0021): the caller branches on the result without a `try`.
- **Strict RFC 8259 grammar.** No trailing commas, no leading zeros, a digit required after
  `.` and after an exponent sign, unescaped control bytes (`< 0x20`) rejected inside strings,
  and exactly one top-level value with only whitespace around it. Strictness is a security
  property for a parser fed untrusted data: ambiguity is rejected, not guessed.
- **Header-only.** Like `CliParser`, the parser calls no OS API — it reads a `string_view` —
  so it stays in the header-only tier and closes Milestone 8 without touching the compiled tier.

No new design pattern is adopted. The pull cursor is the same lazy-iterator idiom already used
by `StringSplitter` (not catalogued as GoF **Iterator** there, and not here). **Interpreter**
was considered and rejected for the same reason as `StringFormatter` (ADR-0012): the grammar
is validated and emitted by a single-pass scan over the `string_view`, with no
terminal/non-terminal expression hierarchy to build — an AST would be a force-fit (AGENTS §8).

## Alternatives Considered

- **A view-backed DOM** (an owning `JsonValue` tree whose scalars borrow the source) —
  rejected for this component: the tree's child containers still allocate, contradicting the
  spec's "non-allocating". It is a strictly higher-level convenience that can be *layered on
  top* of this pull core in a future roadmap item without changing it; deferred, not refused.
- **Throw on malformed input** — rejected: JSON is frequently untrusted, and a parser that
  unwinds the stack on bad bytes pushes a `try` onto every call site. The sticky `error` event
  matches the spec's value-or-error model and the library convention.
- **A heap-backed (or unbounded-recursion) nesting stack** — rejected: an allocation per parse
  and an attacker-controlled recursion depth. The fixed `MaxDepth` array is allocation-free and
  turns deep-nesting attacks into a clean error.
- **Eager conversion** (decode strings / parse numbers during the walk) — rejected: it forces
  allocation (decoded strings) into the hot path and does work the caller may never need. Lazy
  conversion keeps the walk allocation-free and hands the caller typed values on request.
- **A third-party parser (simdjson, RapidJSON, nlohmann/json)** — rejected: the library has a
  zero-dependency contract (spec §3); component #23 exists to provide this in-house.
- **Lenient parsing** (trailing commas, comments, leading zeros) — rejected: leniency is an
  attack surface and an interop hazard for a data-interchange format; the parser follows
  RFC 8259 strictly.

## Consequences

- Parsing an entire document touches zero heap and copies zero bytes: event payloads are views
  into the caller's buffer, which must outlive the parser and any retained event (documented on
  the type). The parser is not thread-safe (one parser, one thread).
- The caller drives the walk and reconstructs structure from the event stream — lower-level
  than a DOM, but the honest cost of "non-allocating". A DOM convenience layer remains open as
  future work.
- Conversion cost is paid only where used: `to_number` is allocation-free; `decode_string`
  allocates one string and only when a string actually needs unescaping.
- Untrusted input is safe by construction: bounded depth (no stack overflow, no allocation),
  strict grammar (no ambiguity), and a non-throwing, positioned error report.
- `MaxDepth` is part of the type (`JsonParser<128>`), so a deeper limit is a compile-time
  choice with no runtime cost; the default 64 suits typical configuration/API payloads.
- Patterns catalogue: no change (pull cursor is the `StringSplitter` idiom; Interpreter stays
  rejected, now for a second component — noted here rather than duplicated in the catalogue).

## References

- Spec §2 component #23, §3 non-functional (zero-copy / non-allocating, zero-dependency), §5
  error model.
- RFC 8259 (The JavaScript Object Notation Data Interchange Format); the JSON number grammar
  and string escape rules including `\uXXXX` UTF-16 surrogate pairs.
- ADR-0021 (`CliParser` value-or-error convention), ADR-0012 (Interpreter rejection), the
  `StringSplitter` lazy-view precedent; `std::from_chars` for locale-independent conversion.
