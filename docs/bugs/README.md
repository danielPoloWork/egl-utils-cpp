# Bug Ledger

The **source of truth** for defects in `egl-util-cpp`. One Markdown file per defect,
`BUG-NNNN-<slug>.md`, under a discovery-date tree `docs/bugs/<YYYY>/<MM>/`. `NNNN` is a
globally-monotonic id, never reused or renumbered. Template: [`template.md`](template.md).

A record is created only for a **verified, reproducible** defect. A third-party report is
reproduced and root-caused first; an unsubstantiated report is still recorded — as
`cannot-reproduce` / `rejected` / `duplicate` — so the triage trail is preserved. When a fix
lands, flip the record to `status: fixed`, set `fixed-in`, link the PR, and add the
`CHANGELOG` `Fixed` line in the same PR.

Structural integrity (frontmatter keys, the `status`/`severity`/`reporter` vocabularies,
filename↔`id` and path↔`discovered` agreement, monotonic ids, the index bijection, and that
a `fixed` record names its `fixed-in`) is enforced by the consistency lint's `bugs` check.

## Index

_No defects recorded yet._

| Bug | Title | Severity | Status | Fixed in |
|-----|-------|----------|--------|----------|
| —   | —     | —        | —      | —        |
