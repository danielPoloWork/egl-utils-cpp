# 2026-07-05 — C4 component diagram (roadmap 11.2)

## What got done

- Added `docs/architecture/c4-component-diagram.md`: the **C4 Level-3 (Component)** view of the
  library — the 25 components grouped into their seven modules (spec §4) with the **actual**
  internal dependency edges, plus a Level-2 (Container) note on the two CMake targets (ADR-0004).
- Diagram is a Mermaid `flowchart` (renders on GitHub), compiled-tier components styled distinctly.
- Linked from `docs/specs/01_spec_util.md` §4, `docs/README.md` layout table, and a new
  `docs/architecture/` row.

## How the edges were derived (not guessed)

Scanned the internal `#include` graph over `src/main/cpp/it/d4np/util/`. The real coupling is
deliberately tiny — only **7 edges** across 25 components:

- intra-module: `StringFormatter → StringBuilder`, `ThreadPool → TaskFuture`
- cross-module: `CliParser → StringBuilder`, `Logger → {StringFormatter, StringBuilder, UniqueRef}`,
  `StackTrace → StringBuilder`

18 of 25 components have **zero** internal dependencies; `HashAlgorithms`/`TypeTraits` are pure
constexpr foundations with no internal dependents. The graph is a DAG with three sinks
(`StringBuilder`, `TaskFuture`, `UniqueRef`).

## Gotcha caught in review

First draft claimed "a single sink, `StringBuilder`, and every cross-module edge ends there."
That was wrong: `Logger → UniqueRef` is a cross-module edge terminating in `memory`. Corrected
both the diagram doc and the spec §4 pointer to say the graph has three sinks and that
cross-module edges terminate in the `strings`/`memory` foundations. The diagram is only useful
if it is *exactly* the include graph — so the doc states the edge set is mechanically
re-derivable and must be updated in the same PR as any new cross-component include.

## Verification

- `python tools/consistency_lint.py` → OK.
- Doc-only; no code, no build impact. Version stays `1.0.0`.

## Project state

- Milestones 1–10 complete. **Milestone 11 in progress:** 11.1, 11.2 done; **11.3–11.7 remain.**

## How the next session resumes

- One PR at a time (AGENTS §6.1): wait for the 11.2 PR to merge, then start **11.3 — per-module
  contract table** (all 25 components: thread-safety guarantee, exception-safety level, allocation
  behavior, algorithmic complexity). ADR-0029 (error model) and the ADR set are the sources for
  the exception-safety/allocation columns; the benchmark baseline doc has complexity hints.
