# Design Patterns Catalogue

Living index of every design pattern **adopted**, **planned**, **considered and rejected**,
or **under evaluation** for `egl-util-cpp`. Mandatory reading whenever a PR introduces
or removes a pattern, and updated in the same PR.

- **Rules** — [`AGENTS.md`](../../AGENTS.md) §8.
- **Canonical taxonomy** — [`design-patterns.md`](design-patterns.md). All pattern names
  used here, in ADRs, and in commit messages must match its spelling and categorisation.

## How to use this catalogue

- **Adding a pattern** — when a PR adopts one, add a row to *Adopted* with the ADR link and
  the code location (a real path under `src/main/cpp/...`).
- **Refining** — update the row and link the new ADR.
- **Rejecting** — add it to *Rejected* with the reason; do not silently drop it.
- **Removing** — move the row to *Superseded*, link the superseding ADR, keep the history.

Status vocabulary: `Planned` (decided in an ADR, not yet landed) · `Implemented` (present
in `src/main/...`, ADR `Accepted`) · `Considered` · `Rejected` · `Superseded`.

## Adopted / Planned

| # | Pattern | Status | Problem it addresses | Code location | ADR / PR |
|---|---------|--------|----------------------|---------------|----------|
| 1 | Object Pool | Implemented | Reuse a fixed set of pre-allocated objects so hot paths avoid per-use allocation and construction cost | [`object_pool.hpp`](../../src/main/cpp/it/d4np/util/object_pool.hpp) | [ADR-0008](../adr/0008-adopt-object-pool-pattern.md) |

## Rejected

| # | Pattern | Considered for | Rejected because | ADR / PR |
|---|---------|----------------|------------------|----------|
| 1 | Builder | `StringBuilder` (component #10) | `StringBuilder` is a fluent string accumulator, not the GoF Builder — there is no director, no product family, and no separation of construction from representation. Labelling it "Builder" would be a force-fit (AGENTS §8); its fluent `append` chain is an idiom, not a pattern adoption. | roadmap 5.2 |
| 2 | Interpreter | `StringFormatter` (component #12) | The `{}` format-string mini-language has no grammar-as-object-structure and no evaluator hierarchy — validation and rendering are single-pass scans over a `string_view`. Building an AST of terminal/non-terminal expression nodes for a two-token grammar would be a force-fit (AGENTS §8). | [ADR-0012](../adr/0012-string-formatter-compile-time-validation.md) |

## Superseded

_No superseded patterns yet._

| # | Pattern | Superseded by | When | ADR / PR |
|---|---------|---------------|------|----------|
| — | —       | —             | —    | —        |

## Candidate patterns to consider

The taxonomy in [`design-patterns.md`](design-patterns.md) lists every pattern in scope. As
the architecture takes shape, narrow that universe to the patterns plausibly applicable to
*this* artifact and list them here by category, each with a one-line "possible application".
A candidate remains a candidate until adopted (own ADR) or explicitly rejected.

## Out-of-scope categories

Record here any taxonomy category pre-classified as not applicable to this artifact (with a
one-line reason), so the policy of explicit rejection is honoured without filling the
*Rejected* table with N/A noise.
