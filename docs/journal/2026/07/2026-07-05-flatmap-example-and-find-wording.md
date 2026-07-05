# 2026-07-05 — FlatMap constexpr example + find wording (roadmap 11.7, closes M11)

## What got done

The last Milestone-11 item — the original discrepancy that started this milestone.

- **Reconciled the spec §5 error-model wording.** It claimed "`FlatMap::find` returns an
  optional-like result"; the shipped API returns a `const_iterator` compared to `end()` (the
  STL-idiomatic form). Reworded §5 to describe the actual per-channel model (iterator for
  associative lookup, `std::optional` for a missing element, status types for N-way outcomes),
  pointing at ADR-0029 and the contract table.
- **Made the "constexpr FlatMap" example real and verified.** The intake §3 example used
  `find(1).has_value()`, which never compiled against the iterator API. Added the corrected
  example (a) as a compile-time-verified `static_assert` in `flat_map_test.cpp`
  (`config_map_example()` — `FlatMap<int, std::string_view>`, insert, iterator-based `find`, all in
  a constant expression) and (b) as a fenced Doxygen snippet in `flat_map.hpp`. The example now
  both compiles and matches its "constexpr" title.
- Marked Milestone 11 done in README + ROADMAP.

## Why no change to the untracked intake

The broken example lives in `d4np-cpp.md`, the untracked raw intake — not a repository artifact
(the frozen spec is `docs/specs/01_spec_util.md`). The durable fix belongs in the repo: the
reconciled §5 wording plus a mechanically-verified example. `d4np-cpp.md` is left untracked.

## Verification

- `python tools/consistency_lint.py` → OK (M11 now all-checked; README ↔ ROADMAP consistent).
- Local MSVC build: `flat_map_test` static_asserts compile (the new constexpr example evaluates at
  compile time); full `util_tests` green; clang-format + clang-tidy clean on the changed sources.

## Project state

- **Milestones 1–11 complete.** Milestone 11 (Specification & Assurance Hardening) is closed:
  11.1 error-model ADR, 11.2 C4 diagram, 11.3 contract table, 11.4 ABI policy, 11.5 security/fuzzing,
  11.6 benchmark targets, 11.7 this. Version still `1.0.0`.

## How the next session resumes

- **Release PR.** M11 added user-visible surface (the `EGL_UTIL_BUILD_FUZZERS` option + `fuzz`
  preset, threat model, extended vectors) — a **MINOR** bump under SemVer (`v1.1.0`). Per AGENTS
  §11: bump `D4NP_UTIL_VERSION_*` in `version.hpp` + the README `Status-vX.Y.Z` badge (consistency
  lint checks version lockstep), roll `[Unreleased]` in `CHANGELOG.md` into
  `docs/changelog/v1/v1.1.0.md` (+ index row), draft release notes under `docs/releases/`, and
  close the "M11 — Specification & Assurance Hardening" GitHub milestone. The agent drafts; the
  maintainer opens/merges and publishes.
