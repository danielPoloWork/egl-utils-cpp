# Git Workflow

Full conventions for branches, commits, and pull requests on `egl-util-cpp`. The short
version is in [`AGENTS.md`](../../AGENTS.md) §6; this document expands it with examples.

## 1. Boundary between agent and human

Agents are trusted to: create/switch branches; stage, commit, and push to feature branches;
draft PR titles and bodies; run `gh pr create --draft` only when explicitly authorized.

Agents must **never**: push directly to `main`; force-push it; merge or
squash-merge a PR (`gh pr merge`, `git merge` into `main`); skip hooks
(`--no-verify`) or signing unless explicitly asked; delete branches the user did not ask to
delete. The human reviews and merges. When in doubt, push the branch and ask.

## 2. Branch naming

`<type>/<short-kebab-description>`, `type ∈ {feat, fix, refactor, perf, docs, test, build,
chore, ci}`. Lowercase kebab, ≤40 chars, describe the *what* not the issue number.

## 3. Commit messages — Conventional Commits

```text
<type>(<scope>): <imperative subject ≤72 chars>

<body — explain WHY, wrap at ~72 cols>

<optional footers: BREAKING CHANGE: ... | Refs: ADR-XXXX | Refs: #NN>
```

- `scope` is a short subsystem noun. Scopes for this repo: `api` `memory` `concurrency` `strings` `containers` `io` `diagnostics` `parsing` `build` `tests` `docs` `ci` .
- One logical change per commit; imperative subject; the body adds the *why*.
- Rebase/squash before opening the PR if intermediate commits are noisy.

## 4. Pull Requests

### 4.1 Title & body

PR title = lead commit subject. Body follows
[`.github/PULL_REQUEST_TEMPLATE.md`](../../.github/PULL_REQUEST_TEMPLATE.md) — GitHub
auto-populates it. Write the body as you want it to read in `git log` forever (the repo is
configured to use the PR title/body as the squash-merge commit message).

### 4.2 Metadata (every PR)

- `--assignee @me`
- exactly **one** type label matching the lead commit's `type`
- the current open release **milestone** (`vX.Y.Z`)

### 4.3 Drafting flow

1. Branch off `main`.
2. Make changes; commit in logical units.
3. `python tools/consistency_lint.py` — must pass.
4. Push; prepare the PR body. If authorized: `gh pr create --draft ...`. Otherwise print the
   proposed command and wait.
5. **Stop.** The user opens the PR and merges manually.

### 4.4 Responding to review

Address comments with new commits on the same branch; avoid `--amend` on pushed commits.
Squash only when the user requests it. After merge, the user deletes the branch.

## 5. Repository hygiene

- Extend `.gitignore` rather than committing generated files.
- Never commit secrets, credentials, or local toolchain paths.
- Large binary fixtures do not belong in the repo without prior agreement.
