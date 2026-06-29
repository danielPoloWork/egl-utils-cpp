# ADR-0001: Record architecture decisions

- **Status:** Accepted
- **Date:** 2026-01-01
- **Deciders:** Maintainer
- **Related:** AGENTS.md §7

## Context

This project is held to an enterprise quality bar, where non-obvious design choices must be
auditable long after the original discussion is forgotten. Decisions made only in commit
messages or chat are lost; decisions made only in code are visible but their *rationale* is
not. We need a durable, reviewable record of *why* the architecture is the way it is.

## Decision

We record every architecturally significant decision as an **Architecture Decision Record
(ADR)** — one numbered Markdown file in `docs/adr/`, written in the lightweight Michael
Nygard format ([`template.md`](template.md)). ADRs are numbered sequentially, are immutable
once `Accepted` (a change is a new ADR that supersedes the old one), and are indexed in
[`README.md`](README.md). An ADR is opened when a choice affects the public surface or
compatibility, when two reasonable options exist and the rationale is non-obvious, when a
design pattern is adopted, or when a prior ADR is superseded.

## Alternatives Considered

- **No formal record (rely on commits/PRs).** Rejected — rationale scatters across history
  and is not discoverable; reviewers re-litigate settled questions.
- **A single design document.** Rejected — it becomes a stale monolith; per-decision files
  keep each decision atomic, reviewable, and supersedable.

## Consequences

- Every PR that makes a significant decision ships its ADR in the same PR.
- The ADR index is kept in lockstep with the files (enforced by the consistency lint's
  `adr-index` check).
- New contributors can reconstruct the design's reasoning by reading `docs/adr/` in order.

## References

- Michael Nygard, *Documenting Architecture Decisions* (2011).
- AGENTS.md §7 (Documentation Maintenance).
