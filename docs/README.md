# Documentation

The durable, versioned documentation for `egl-util-cpp`. Everything in `docs/` is a
first-class repository artifact and follows the same review process as source code —
conversational context and scratch notes do not live here.

## Layout

| Path | Purpose |
|---|---|
| `docs/specs/` | Functional and technical specifications. Frozen contracts — diverging requires an ADR. |
| `docs/architecture/` | Architecture views — the C4 component diagram and its dependency-graph rules. |
| `docs/adr/` | Architecture Decision Records — one numbered file per decision. |
| `docs/patterns/` | Living catalogue of design patterns + the canonical taxonomy. |
| `docs/workflow/` | Git, documentation, release, and maintenance conventions. |
| `docs/development/` | Procedural how-to guides for working on the code locally. |
| `docs/journal/` | Dated session checkpoints. |
| `docs/bugs/` | In-repo bug ledger — one record per known defect, with the triage trail. |
| `docs/releases/` | Per-version release notes (one file per release; index of all of them). |
| `docs/benchmarks/` | Reproducible performance methodology + results, backing every performance claim. |


## Reading order for newcomers

1. [`/README.md`](../README.md) — what this project is.
2. [`/AGENTS.md`](../AGENTS.md) — how agents (and humans) work in this repo.
3. [`specs/01_spec_util.md`](specs/01_spec_util.md) — what we are building.
4. [`development/local-build.md`](development/local-build.md) — how to build and test it.
5. [`adr/`](adr/) — why we built it that way.
6. [`patterns/`](patterns/) — which design patterns we exercise and why.
7. [`/ROADMAP.md`](../ROADMAP.md) — what is done and what is next.

## Conventions

- **English only.** All documentation and identifiers are in English.
- **Same-PR updates.** When code changes its public surface or a non-trivial design choice,
  the docs change in the *same* pull request — never as a follow-up.
- **No silent drift.** If the implementation diverges from the spec, update the spec or open
  an ADR that supersedes the relevant section.
