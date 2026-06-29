# Post-Release Maintenance Protocol

How `egl-util-cpp` is governed in the maintained-product phase (post-`v1.0.0`): how to
decide a release's SemVer level, how a fix reaches users, how security issues and
deprecations are handled. The mechanical release steps are in [`release.md`](release.md);
the agent-vs-human boundary is [`AGENTS.md`](../../AGENTS.md) §11.

## What the version number protects

The "public API" the version protects is the project's stable surface: the
public functions/types/endpoints and any documented compatibility guarantees (incl. ABI
where applicable), plus any user-visible configuration knobs and the
package/target name.

## Decision tree — which level?

1. **Does the change remove, rename, or alter the signature/semantics of any public symbol,
   knob, or target — such that existing consumers fail to compile/link or behave
   differently?** → **MAJOR.** Own ADR (justifying the break) + a migration note. Prefer the
   deprecation path below over an abrupt break.
2. **Does it add new public surface or an opt-in capability while every existing use keeps
   working?** → **MINOR.** (Closing a roadmap milestone is the canonical MINOR.) New
   capabilities are planned on the roadmap first — usually a new milestone.
3. **Otherwise** (bug fix, docs, packaging, perf, CI with no public-API change) → **PATCH.**

When ambiguous, treat it as the **higher** level — a wrongly-low version number breaks
consumers who trusted SemVer. Record the call in the release notes.

## Bug lifecycle

Defects are tracked in [`docs/bugs/`](../bugs/) (source of truth). A fix: (1) is recorded as
a `confirmed` ledger file; (2) has its SemVer level assessed; (3) lands through the hotfix
flow below — in the **same PR**, flip the record to `status: fixed`, set `fixed-in`, link
the PR, and add the `CHANGELOG` `Fixed` (or `Security`) line.

## Hotfix & backport

- **`main` is releasable** (common case): fix on a `fix/<name>` branch off
  `main`, add a test + `Fixed` changelog line, merge, cut the next PATCH.
- **`main` has unreleasable WIP**: branch `hotfix/v<X.Y.Z+1>` **from the
  released tag**, apply the minimal fix + test, cut the PATCH from that branch, then
  **forward-port** (cherry-pick) to `main`. The forward-port is mandatory.

A hotfix is always the smallest change that fixes the defect — no refactors ride along.

## Security fixes

Report privately (see [`SECURITY.md`](../../SECURITY.md)); triage & fix under embargo;
coordinated release then advisory; record under a `Security` changelog entry with the
advisory/CVE; backport to every supported line.

## Deprecation policy

1. **Deprecate in a MINOR** — mark deprecated in the API docs + a `Deprecated` changelog
   line; the symbol keeps working; record the replacement in an ADR.
2. **Honour a window** — keep it for at least the rest of the current MAJOR line.
3. **Remove in the next MAJOR** — with the breaking-change ADR + a migration note.

## Consistency lint — failure → remediation

`python tools/consistency_lint.py` runs before every PR and in CI. Each failure prints
`[check] message`; fix per the check's intent (version lockstep, ADR index, pattern rows,
spec coverage map, milestone agreement, bug-ledger integrity). See the lint's docstring for
the full contract.
