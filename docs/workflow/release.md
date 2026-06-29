# Release Process

The mechanical step-by-step for cutting a release of `egl-util-cpp`. The governance
(which SemVer level, how a fix flows, deprecation/security) is in
[`maintenance.md`](maintenance.md); the agent-vs-human boundary is
[`AGENTS.md`](../../AGENTS.md) §11.

## Versioning

**Semantic Versioning 2.0.0**, annotated tags `vMAJOR.MINOR.PATCH`. Start point:
pre-1.0 milestone-driven.

- Pre-1.0: `MINOR` bumps on each completed roadmap milestone; `PATCH` for hotfixes.
- Post-1.0: `MAJOR` for incompatible changes, `MINOR` for additions, `PATCH` for fixes.

## Cutting a release (the steps)

1. **Bump the version constant** (D4NP_UTIL_VERSION_{MAJOR,MINOR,PATCH,STRING}) in `src/main/cpp/it/d4np/util/version.hpp`; update any
   version-check test.
2. **Roll the changelog** — move the `[Unreleased]` entries into a new per-version file
   `docs/changelog/v<MAJOR>/v<X.Y.Z>.md` and add an index row to `CHANGELOG.md`.
3. **Refresh the README** status badge (and milestone table on a MINOR that closes a
   milestone).
4. **Draft release notes** under `docs/releases/v<X.Y.Z>.md`.
5. **Run the consistency lint** (`python tools/consistency_lint.py`) — version lockstep must
   pass.
6. **Open the release PR** — *the maintainer does this*. The agent prepares it.
7. **Merge** — *the maintainer*.
8. **Tag + draft (carry-through)** — the agent runs `git tag -a v<X.Y.Z> -m "<headline>"` and
   `git push origin v<X.Y.Z>` immediately after merge; the tag push lets CI open the GitHub Release
   as a **draft**. The agent always carries the release this far — only **Publish** is the human's.
9. **Publish** the GitHub Release — *the maintainer* (the deliberate human checkpoint).
10. **CI builds & attaches artifacts** on the tag push.


## Boundary

| Action | Who |
|---|---|
| Bump version, roll changelog, draft notes | Agent |
| Open / merge the release PR | **Human** |
| Create & push the annotated tag, then the **draft** release (CI drafts it on tag-push) | Agent |
| Publish the GitHub Release (click **Publish**) | **Human** |
| Build & attach artifacts | CI |


Agents never publish releases, never amend or delete published tags, and only delete-and-
repush an *unpublished* tag whose release run visibly failed.
