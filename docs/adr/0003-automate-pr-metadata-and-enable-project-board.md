# ADR-0003: Automate PR metadata and enable a project board

- **Status:** Accepted
- **Date:** 2026-06-29
- **Deciders:** Daniel Polo (owner)
- **Related:** AGENTS.md §6.4, `.github/workflows/pr-metadata.yml`, `.github/labels.yml`,
  `docs/workflow/github-setup.md`

> **Amendment (2026-06-30).** The automation does **not** set the milestone. An early version
> assigned "the current open release milestone", which funneled every PR into the single
> `v0.0.0` milestone (M1 and M2 work landed together). GitHub milestones now mirror the roadmap
> one-to-one (`M1 … M10`, each carrying the roadmap goal + items as its description), are
> **assigned per PR by the author** (who knows the roadmap item — it is not derivable generically
> from a PR), and are **closed when their last PR merges**. Assignee and the type label remain
> automated.

## Context

AGENTS.md §6.4 requires every pull request to carry an assignee, exactly one type label,
and the current open release milestone. Until now this was a manual checklist set at
`gh pr create` time, and it was easy to forget — the bootstrap PR landed with none of the
three. The repository is owner-governed and single-maintainer today, so reviewers and
project boards had been left out of the metadata to avoid ceremony with no payoff.

The owner now wants the three metadata fields applied automatically on every PR, and wants
the project tracked on a GitHub **Project (v2) board** with new PRs and issues added
automatically — turning the manual policy into an enforced, standing rule.

## Decision

Make PR metadata a standing rule enforced by automation rather than a manual step:

1. A workflow, `.github/workflows/pr-metadata.yml`, runs on every same-repo pull request
   and idempotently fills any missing field: it assigns the PR author, applies exactly one
   **type label derived from the branch prefix** (`feat/`, `fix/`, `refactor/`, `perf/`,
   `docs/`, `test/`, `build/`, `chore/`, `ci/`). The canonical label set
   (`.github/labels.yml`) is imported once via `docs/workflow/github-setup.md`. The roadmap
   milestone is set per PR by the author (see the Amendment above), not by the workflow.
2. A GitHub **Project (v2)** board for the repository is created and linked, with the
   project's native **"Auto-add to project"** workflow enabled so every new issue and PR is
   added automatically. The native workflow is used (rather than an action) because the
   default `GITHUB_TOKEN` cannot write Projects v2; a workflow-based alternative would
   require a separate fine-grained PAT secret.

This supersedes the prior posture of deferring board/automation until a second collaborator
existed.

## Alternatives Considered

- **Keep it manual (status quo).** Rejected: it already failed on the first PR, and a rule
  that depends on memory is not a rule.
- **`actions/add-to-project` in the workflow for the board.** Rejected as the primary
  mechanism: it needs a PAT with `project` scope stored as a repo secret, adding secret
  lifecycle management for what the Project's native auto-add does tokenlessly. It remains a
  fallback if non-PR item sources ever need it.
- **`pull_request_target` to also cover fork PRs.** Rejected: it runs with a writable token
  in the base context and is an injection risk; a maintainer-driven repo can curate fork-PR
  metadata on merge instead.

## Consequences

- Every same-repo PR is auto-assigned and type-labeled with no manual step; the milestone is
  set per PR by the author, since GitHub milestones mirror the roadmap one-to-one. The
  `gh pr create` flags in §6.4 are the fallback for the automated fields.
- Branch naming becomes load-bearing: the `<type>/<slug>` convention now drives the label, so
  an unknown prefix yields a warning and no type label (caught in review).
- One repository-level admin step is added to `github-setup.md` (create board + enable
  auto-add); it is a one-time UI action like branch protection.
- Fork PRs are not auto-curated (acceptable for the current single-maintainer model).

## References

- AGENTS.md §6.4 (Pull Requests), `.github/workflows/pr-metadata.yml`,
  `.github/labels.yml`, `docs/workflow/github-setup.md`.
