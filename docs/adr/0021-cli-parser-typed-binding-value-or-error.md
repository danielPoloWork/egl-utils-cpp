# ADR-0021: `CliParser` typed variable-binding with a value-or-error boundary

- **Status:** Accepted
- **Date:** 2026-07-03
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #22), §5 (error model), roadmap 8.1,
  [ADR-0012](0012-string-formatter-compile-time-validation.md) (the `StringBuilder` help
  renders through), the `StringBuilder` Builder rejection (`docs/patterns/README.md`)

## Context

The spec requires `CliParser` (component #22): a "flexible, typed command-line argument
parser with auto-generated help". Three questions dominate the design. **Where do parsed
values land?** — a parser can hand back a heterogeneous bag the caller queries by name and
casts, or it can write typed values straight into caller-owned variables. **How are errors
surfaced?** — a bad `argv` is *normal input*, not a programmer error, yet the same object
also has genuinely-illegal uses (registering two `--verbose` flags). **What OS surface does
it need?** — every other component that touched the compiled STATIC tier
([ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md)) did so because it
called OS APIs; a CLI parser reads a `char**` the runtime already handed it and needs none.

Forces: the library's house style is value-or-error at boundaries the *user* drives and
exceptions only for *programmer* error (the `Stopwatch`/`TaskFuture`/`Logger` precedent —
misuse throws `std::logic_error`, bad runtime input does not); binding must be type-safe
without RTTI or a variant bag; the grammar must meet the GNU/POSIX conventions users
already know (`--long`, `--long=value`, `-x`, `-xvalue`, clustered `-abc`, the `--`
terminator); and the help text must stay in sync with the registered arguments for free,
because a help block maintained by hand drifts.

## Decision

**Arguments are declared fluently and each binds to a caller-owned variable; `parse`
returns a `ParseResult` value-or-error object and never throws on user input; only
registration misuse throws `std::logic_error`; the whole component is header-only.**

- **Direct typed binding, no result bag.** `add_flag(name, short, help, bool&)`,
  `add_option(name, short, help, T&, required, value_name)`, and
  `add_positional(name, help, T&, required)` each capture a reference to a caller variable.
  A successful parse writes the converted value straight into it; there is no
  `result.get<int>("count")` to name-and-cast, and a type mismatch is impossible because
  the target type *is* the binding. Bound variables keep their pre-parse value (the
  caller's default) unless a match overwrites them, so defaults are set the ordinary way —
  by initialising the variable.
- **Conversion through the `CliValue` concept.** A target type is usable iff a
  `detail::cli_parse(string_view, T&)` overload exists; the box ships `std::string`, `bool`
  (a fixed `true/false/1/0/yes/no/on/off` vocabulary for explicit `--flag=value` options),
  every standard integer, and `float`/`double`. Numbers go through `std::from_chars` with
  **whole-token consumption required** (so `12x` is rejected, not silently truncated to
  `12`), with a classic-locale `istringstream` fallback only where libc++ still lacks
  floating-point `from_chars`. Binding an unsupported type is a compile error at the
  registration call site, not a runtime surprise.
- **A `ParseResult` value-or-error boundary.** `parse` returns success, `help_requested`,
  or a failure carrying a machine-readable `CliError` (`unknown_option`, `missing_value`,
  `unexpected_value`, `invalid_value`, `missing_required`, `unexpected_positional`) plus a
  human-readable message. It is contextually convertible to `bool` (true iff success) so a
  call site reads `if (!parser.parse(argc, argv)) { ... }`. `--help`/`-h` is *not* an error:
  it short-circuits to `help_requested`, and the caller prints `help()` and exits `0`.
- **Exceptions only for programmer error.** Empty names, duplicate long/short/positional
  names, and names colliding with the auto-registered `--help`/`-h` throw
  `std::logic_error` from the registration methods — they are bugs in the calling program,
  caught in development, never reachable from `argv`.
- **GNU/POSIX grammar, auto-generated help.** Long `--name[=value]`, short
  `-n`/`-n value`/`-n=value`/`-nvalue`, clustered short *flags* (`-ab` == `-a -b`) optionally
  ending in a value option that takes the cluster remainder, a lone `--` ending option
  processing, positionals consumed in registration order. `help()` renders a usage line, the
  description, and column-aligned positional/option sections directly from the registered
  specs (through `StringBuilder`, ADR-0012's sibling), so it can never fall out of sync.
- **Header-only.** The parser calls no OS APIs — it consumes the `char**` the runtime
  already produced — so it stays in the header-only tier. It is the first Milestone-8
  component and deliberately does *not* pull the compiled tier in for a job that does not
  need it.

No new design pattern is adopted. The fluent `add_*` chain is the same *fluent-interface
idiom* already recorded for `StringBuilder` (where GoF **Builder** was rejected as a
force-fit — there is no director, product family, or construction/representation split);
`ParseResult` is a plain value-return, not a pattern. The patterns catalogue is unchanged.

## Alternatives Considered

- **A heterogeneous result bag** (`result.get<T>("name")` over a `map<string, any>` or a
  `variant`) — rejected: it defers type checking to run time (a `get<int>` on a value the
  user typed as text fails at the call site, far from the declaration), needs RTTI or a
  variant visitor, and forces an allocation per parsed value. Direct binding gives the same
  ergonomics with compile-time type safety and zero indirection.
- **Throw on bad `argv`** — rejected: a mistyped flag is ordinary user input, not an
  exceptional condition; a CLI tool wants to print a friendly message and exit `2`, not
  unwind a stack. This matches the spec's value-or-error model and the library's convention
  that only programmer misuse throws (`Stopwatch`, `TaskFuture`, `Logger`).
- **Return codes only, no message** — rejected: the `CliError` code serves programmatic
  handling, but a tool must also print *why* it rejected the line; carrying both a code and
  a message costs one already-owned `std::string` and spares every consumer from
  reconstructing the text.
- **A third-party parser (CLI11, argparse, Boost.Program_options)** — rejected: the library
  has a zero-dependency contract (spec §3), and the component exists precisely to provide
  this in-house.
- **Subcommands / `git`-style verbs, environment-variable fallback, config files** —
  deferred, not rejected: out of scope for component #22's "typed parser with auto-generated
  help". They would layer on top of this core without changing it; if planned, they become
  new roadmap items.
- **The compiled STATIC tier** — rejected as unnecessary: nothing here touches an OS header,
  so tier placement (ADR-0004) would add a link requirement for no benefit.

## Consequences

- Call sites are terse and type-safe: declare a variable, bind it, parse, use it — the
  compiler rejects an unsupported target type and the parser can never hand back a value of
  the wrong type. Every bound reference must outlive the `parse` call (documented on the
  type); the parser is not thread-safe (register and parse from one thread).
- Error handling is uniform with the rest of the library: user input never throws, so a
  `main` can branch on `ParseResult` without a `try`; misuse throws loudly during
  development. All seven outcomes (`ok`, `help_requested`, and the six `CliError`
  categories) are covered by tests.
- Help is generated, not maintained: adding an argument updates the help block for free.
  The column alignment is computed from the widest label across positionals and options.
- Numbers are validated strictly (whole-token `from_chars`): `--count=12x` is
  `invalid_value`, not a silent `12`. The bound variable is untouched on any failure.
- A bare negative-number token (e.g. `-5`) is read as a short-option cluster unless it
  follows `--` or is the value of an option (`--count -5` works, because the value is taken
  verbatim). This is the standard getopt trade-off and is documented on the type.
- Header-only: consumers link nothing extra; the umbrella header gains one include.
- Patterns catalogue: no change (fluent interface is an idiom, per the `StringBuilder`
  precedent).

## References

- Spec §2 component #22, §5 error model.
- ADR-0004 (why this component stays *out* of the compiled tier), ADR-0012 (`StringBuilder`,
  the help renderer), the `StringBuilder` Builder rejection in `docs/patterns/README.md`.
- POSIX Utility Syntax Guidelines (Issue 7) and the GNU `getopt_long` conventions for the
  option grammar; `std::from_chars` for locale-independent numeric conversion.
