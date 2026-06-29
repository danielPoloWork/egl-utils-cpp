## Summary

One or two sentences: what changes and why it matters.

## Motivation

Link to the spec section, ADR, roadmap item, or issue that prompted this work.

## Changes

- bulleted list of meaningful changes (not a file list)

## Design Patterns

- list every pattern adopted/refined/rejected in this PR, with a one-line rationale and a
  link to the ADR.
- if none, write "None — straightforward implementation."

## Verification

- [ ] Builds cleanly on the full CI matrix (Linux x86_64 (GCC>=11, Clang>=14), Windows x86_64 (MSVC>=19.30), macOS arm64 (Apple Clang>=14))
- [ ] Unit tests pass; new/changed behavior covered (≥ 80% line)
- [ ] `clang-format (LLVM-derived, 4-space, 120 col)` clean; `clang-tidy (bugprone/cert/cppcoreguidelines/modernize/performance/portability/readability)` clean on the diff
- [ ] ASan, UBSan, TSan, Valgrind green (where applicable)
- [ ] Benchmark numbers attached (when perf-relevant)
- [ ] `python tools/consistency_lint.py` passes

## Documentation Impact

- [ ] README.md updated (if user-facing surface changed)
- [ ] ROADMAP.md checkbox flipped
- [ ] ADR added/updated (if a non-trivial design decision was made)
- [ ] docs/patterns/README.md updated (if a pattern was introduced, refined, or rejected)
- [ ] Spec updated (if behavior diverges from `docs/specs/`)
- [ ] CHANGELOG.md updated (for user-visible changes)
- [ ] PR metadata set — assignee, one type label, release milestone
