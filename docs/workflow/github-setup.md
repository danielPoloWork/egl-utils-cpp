# GitHub Repository Setup

The one-time, repo-level configuration that cannot live as a committed file — branch
protection, rulesets, merge strategy, Discussions, Pages, labels, the first milestone. Run
these once, with admin rights, after creating the GitHub repository for `egl-util-cpp`.
Everything here reproduces the reference project's GitHub governance; the in-repo automation
(CI, Dependabot, issue forms, CODEOWNERS, release draft) ships as files and needs no setup.

> Prerequisites: the [`gh`](https://cli.github.com/) CLI, authenticated (`gh auth login`),
> and `OWNER=danielPoloWork` / `REPO=egl-util-cpp` exported.

```bash
OWNER=danielPoloWork
REPO=egl-util-cpp
BRANCH=main
```

## 1. Merge strategy — squash only, PR title/body as the commit

```bash
gh api -X PATCH repos/$OWNER/$REPO \
  -F allow_squash_merge=true -F allow_merge_commit=false -F allow_rebase_merge=false \
  -F delete_branch_on_merge=true \
  -F squash_merge_commit_title=PR_TITLE -F squash_merge_commit_message=PR_BODY
```

This is why the PR title/body is written "as it should read in `git log` forever"
(AGENTS.md §6.4).

## 2. Labels (one type-label per PR)

```bash
# Requires yq. Imports .github/labels.yml idempotently.
yq -o=json '.[]' .github/labels.yml | jq -c . | while read -r l; do
  name=$(jq -r .name <<<"$l"); color=$(jq -r .color <<<"$l"); desc=$(jq -r .description <<<"$l")
  gh label create "$name" --color "$color" --description "$desc" --force
done
```

## 3. Branch protection / ruleset for `main`

Require PRs, a green CI, linear history, and conversation resolution; block direct pushes and
force-pushes. (Agents never push to the default branch — this enforces it server-side.)

```bash
gh api -X PUT repos/$OWNER/$REPO/branches/$BRANCH/protection \
  --input - <<JSON
{
  "required_status_checks": {
    "strict": true,
    "contexts": ["consistency / lint"]
  },
  "enforce_admins": false,
  "required_pull_request_reviews": { "required_approving_review_count": 0 },
  "required_linear_history": true,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "required_conversation_resolution": true,
  "restrictions": null
}
JSON
```

Add the build matrix contexts (e.g. `build / ubuntu-24.04 / …`) to `contexts` once you have
seen their exact names in the first CI run.

## 4. Discussions, Pages, and the security policy

```bash
# Enable Discussions (questions/ideas; linked from the issue chooser).
gh api -X PATCH repos/$OWNER/$REPO -F has_discussions=true

# GitHub Pages from the docs/ folder on the default branch (optional doc site).
gh api -X POST repos/$OWNER/$REPO/pages \
  -F "source[branch]=$BRANCH" -F "source[path]=/docs" 2>/dev/null \
  || echo "Pages already configured or needs the web UI once."
```

Private vulnerability reporting (the SECURITY.md target) is enabled in the web UI:
**Settings → Code security → Private vulnerability reporting → Enable**.

## 5. First release milestone

PRs carry the current open release milestone (AGENTS.md §6.4):

```bash
gh api -X POST repos/$OWNER/$REPO/milestones -f title="v0.0.0" \
  -f state=open -f description="First release slice (Milestone 1)."
```

## 6. Project board & auto-add (ADR-0003)

Track the project on a GitHub Project (v2) board and add new issues/PRs automatically. The
default `GITHUB_TOKEN` cannot write Projects v2, so this is a one-time owner step (the
`gh project` commands need a token with the `project` scope: `gh auth refresh -s project`).

```bash
# Create the board and link it to the repository.
gh project create --owner "$OWNER" --title "egl-util-cpp"
# (note the project number it prints, then:)
gh project link <number> --owner "$OWNER" --repo "$REPO"
```

Then enable native auto-add in the board UI: **Project → ⋯ → Workflows → "Auto-add to
project" → enable** for issues and pull requests. This populates the board with no token at
runtime; PR assignee/label/milestone are handled by `.github/workflows/pr-metadata.yml`.

## Re-running

Every command here is idempotent or safely re-runnable. Re-run after changing labels, after a
new CI check name should become required, or when onboarding a second collaborator (then bump
`required_approving_review_count` to 1 and add reviewers to CODEOWNERS).
