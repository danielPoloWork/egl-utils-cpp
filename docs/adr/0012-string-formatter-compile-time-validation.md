# ADR-0012: `StringFormatter` compile-time format-string validation strategy

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #12), spec §3 (constexpr requirement), roadmap 5.3,
  [ADR-0009](0009-adopt-flat-sorted-contiguous-containers.md) (constexpr precedent),
  `string_builder.hpp` (roadmap 5.2, deferred floating-point formatting)

## Context

The spec requires `StringFormatter` (component #12): a "compile-time type-safe formatting
engine, similar to `std::format`", and lists it among the components that must be
"constexpr-evaluable where applicable" (§3). `std::format` itself is unavailable at the
project's toolchain floor (GCC 11 and Clang 14 ship no usable `<format>`), so the component
must provide the two guarantees that make `std::format` safe — *the format string is
checked before the program runs* and *arguments are type-checked* — with C++20 features
that all floor compilers implement.

Three forces shape the design:

1. **Where "compile time" can actually reach.** Rendering produces a `std::string`;
   `constexpr std::string` (P0980) is missing from GCC 11's and Clang 14's standard
   libraries, so *rendering* cannot be constant-evaluated at the floor. *Validation* of the
   format string, however, needs only `std::string_view` scanning, which is fully constexpr
   in C++20.
2. **Prior art in the codebase.** `StringBuilder` (roadmap 5.2) already renders integers
   via `std::to_chars` and deliberately deferred floating-point formatting to this
   component, because libc++'s floating-point `std::to_chars` is uneven across the matrix.
3. **API familiarity.** Call sites should read like `std::format`, not like a template
   metaprogramming exercise.

## Decision

`StringFormatter` adopts the C++23 `std::format_string` technique in C++20 form: the
format string parameter is a dedicated type, `FormatString<Args...>`, whose **`consteval`
constructor** validates the string during constant evaluation — a malformed string or a
placeholder/argument count mismatch is a *compile error* at the call site, reported by
calling an undefined non-constexpr function whose `reason` argument appears in the
diagnostic. `Args...` sit in a non-deduced context (`std::type_identity_t`), so they are
deduced solely from the value arguments; arguments are constrained by a public
`Formattable` concept (string-view-convertible types, `char`, `bool`, the
`StringBuilder` integer set, `float`, `double`), which is the type-safety half of the
contract. The accepted grammar is minimal and positional: `{}` placeholders plus `{{` /
`}}` escapes — no argument indices and no format specifications. Rendering happens at run
time on top of `StringBuilder` (`format` returns a `std::string`; `format_to` appends into
a caller-supplied builder); floating-point values use `std::to_chars`
(shortest-round-trip) where `__cpp_lib_to_chars` announces it, with a classic-locale
`ostringstream` fallback at `max_digits10` for standard libraries that lack it.

## Alternatives Considered

- **NTTP fixed-string template — `format<"x={}">(42)`** — pass the format string as a
  class-type non-type template parameter and parse it entirely in template context.
  Rejected because the call syntax diverges from `std::format` for no added safety (the
  `consteval` constructor already guarantees compile-time validation), every distinct
  format string mints a new template instantiation (code-bloat and symbol-length cost),
  and class-type NTTPs are the shakiest C++20 corner at the toolchain floor.
- **Runtime validation — throw `std::format_error`-style exceptions** — validate the
  string on each call. Rejected because it abandons the spec's defining requirement:
  a typo like `"{"` or a missing argument must fail the build, not the production run.
- **Full constexpr rendering** — make `format` itself constant-evaluable. Rejected at this
  time because it requires `constexpr std::string` (absent at the GCC 11 / Clang 14 floor)
  or a bespoke fixed-capacity constexpr string type that the spec does not ask for.
  Validation — the part with compile-time value — is constexpr; rendering is not. This can
  be revisited without an API break once the floor moves to P0980-capable libraries.
- **Format specifications (`{:>8.2f}` …) in the first cut** — rejected as scope creep: the
  spec asks for *similar to* `std::format`, not a re-implementation. The grammar can grow
  later; strings valid today stay valid.

## Consequences

- Malformed format strings and arity mismatches are compile errors at every call site;
  argument types are policed by the `Formattable` concept. Nothing is left to runtime
  validation, so the runtime path has no error states to test beyond rendering itself.
- The validator is an ordinary `constexpr` function returning an error enum, so its logic
  is unit-testable with `static_assert` even though the failure path itself (a build
  failure) cannot appear in a runtime test suite.
- `FormatString`'s constructor is intentionally implicit (like `std::format_string`):
  string literals at call sites convert transparently. A non-literal (runtime) format
  string cannot form a `FormatString` — that is the point, and it is documented; dynamic
  format strings are out of scope for this component.
- Floating-point output is shortest-round-trip on toolchains with floating-point
  `std::to_chars` and round-trip-exact-but-longer (`max_digits10`) on the fallback path,
  so tests pin only values whose rendering agrees on both paths (e.g. `1.5`, `-2.25`).
- Rendering reuses `StringBuilder` (single growable buffer, `std::to_chars` integers), so
  the formatter adds no new allocation strategy; `format_to` lets callers amortize even
  the result-string allocation.
- **Pattern note (AGENTS §8):** the `{}` mini-language was checked against the
  **Interpreter** pattern and rejected — there is no grammar-as-object-structure and no
  evaluator hierarchy, just a single-pass scan. Recorded in the patterns catalogue's
  *Rejected* table.

## References

- Spec §2 component #12, §3 non-functional requirements, §5 public interface.
- P2216R3 (`std::format` compile-time checking) — the technique adopted here.
- P0980R1 (`constexpr std::string`) — why rendering stays runtime at the current floor.
- `docs/journal/2026/07/2026-07-01-string-builder.md` — the floating-point deferral.
