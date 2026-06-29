# Documentation Workflow

How documentation is maintained on `egl-util-cpp`. Documentation is part of the
deliverable — every PR ships its own doc updates in the same PR. The rules are in
[`AGENTS.md`](../../AGENTS.md) §7; this expands the *how*.

## Artifacts and when to touch them

| Artifact | Update it when… |
|---|---|
| `README.md` | the public surface, build/test/run flow, or milestone status changes |
| `docs/specs/` | behavior diverges from the frozen spec (update spec **or** add a superseding ADR) |
| `docs/adr/` | a non-trivial design decision is made, or a pattern is adopted/superseded |
| `docs/patterns/README.md` | a pattern is introduced, refined, rejected, or superseded |
| `ROADMAP.md` | an item completes (flip the checkbox) or new work is planned |
| `CHANGELOG.md` | a user-visible change lands (add a line to `[Unreleased]`) |
| `docs/journal/` | a work session changed the project's state (dated checkpoint) |
| `docs/bugs/` | a defect is verified, triaged, or fixed |

## Same-PR discipline

A change to code and its documentation belong to the **same** pull request. "Docs
follow-up" is not allowed (`AGENTS.md` §10). The consistency lint
(`python tools/consistency_lint.py`) mechanically enforces the parts of this that can be
checked: version lockstep, ADR index ↔ files, pattern rows ↔ ADR+code, spec coverage map,
README ↔ ROADMAP milestone agreement, and bug-ledger integrity.

## API documentation

Public symbols are documented with `Doxygen`-compatible comments. The API-docs build
must be warning-free (quality bar, `AGENTS.md` §10). Narrative documentation lives in
Markdown under `docs/`; the split between generated API docs and hand-written narrative is
recorded in an ADR if non-obvious.
