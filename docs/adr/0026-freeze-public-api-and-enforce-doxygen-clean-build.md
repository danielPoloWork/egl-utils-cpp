# ADR-0026: Freeze the 1.0 public API and enforce a warning-clean Doxygen build

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §3 (non-functional: documentation), §5 (public interface), roadmap 10.1
  (opens Milestone 10), [ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md)
  (the two tiers that define what "public" means), the umbrella header `util.hpp`

## Context

Milestone 10 hardens the library toward 1.0. Its first item (10.1) is to **freeze the public
API and document every public type**. Two problems stand in the way. First, the public surface
has never been *declared* — it is implicit in the headers, so there is no reference for what 1.0
promises to keep stable. Second, the `docs` target ran with `EXTRACT_ALL = YES` and **no warning
enforcement**, so documentation defects shipped silently: two components had `/* … */` C-comments
inside a ` ```cpp ` example fence (which broke Doxygen's fence tracking and cascaded into a dozen
"symbol not declared" errors), `StackAllocator::allocate` had an undocumented parameter, and the
README-as-mainpage emitted seven unresolved `\ref` warnings for its repo-relative links.

Forces: the SemVer commitment (§11) needs a concrete "this is the public API" list to reason
about breaking vs. additive changes; the enterprise bar (§10) already requires "Doxygen builds
without warnings", but nothing *enforced* it; and the docs should have an API-focused landing
page, not one whose links only resolve on GitHub.

## Decision

**Declare the `it::d4np::util` public surface frozen for 1.0, and make the Doxygen build a hard,
warning-free gate.**

- **The frozen public API** is every non-`detail` type and free function in `it::d4np::util`
  reachable from the umbrella header `util.hpp`: the memory, container, string, concurrency,
  diagnostics, parsing, and I/O components catalogued on the Doxygen main page, plus the
  `version_*` constants and the compiled-tier `library_version()`. Anything under
  `it::d4np::util::detail` is implementation and explicitly **not** part of the contract. Post-1.0
  this surface follows SemVer: additions are MINOR, breaking changes MAJOR (§11).
- **The docs build fails on any warning.** `DOXYGEN_WARN_AS_ERROR = FAIL_ON_WARNINGS_PRINT`
  (prints every warning, then fails) turns the standing "Doxygen builds without warnings" rule
  into an enforced gate — the CI `docs` job now goes red on a doc regression instead of passing
  silently.
- **A curated API mainpage replaces README for Doxygen.** `docs/doxygen-mainpage.md` is the
  `USE_MDFILE_AS_MAINPAGE`, and README.md is dropped from the Doxygen input set. The README's
  relative links to `docs/…`, `AGENTS.md`, etc. resolve on GitHub but not inside the Doxygen
  input, so they produced `\ref` warnings; the curated page links to the generated class pages
  (which resolve) and to the repository by full URL (which Doxygen treats as external).
- **`EXTRACT_ALL` stays `YES`.** The whole surface renders — including STL-conformance members
  (`value_type`, `size()`, `begin()`/`end()`) that inherit their meaning — while the bar for
  *prose* documentation is set at the type level: every public class/struct/enum carries a
  descriptive doc block, and `WARN_AS_ERROR` keeps whatever documentation exists well-formed.
- **The existing defects are fixed** in the same PR: the `/* … */` fences become `//` line
  comments, and `StackAllocator::allocate`'s `bytes` parameter is documented.

## Alternatives Considered

- **`EXTRACT_ALL = NO` to force per-member documentation** — rejected: it surfaced 160 "not
  documented" errors, almost all STL-conformance typedefs/accessors/overloads whose meaning is
  standard, plus a demand to document internal `detail::` helpers that should not appear in the
  public docs at all. Documenting every one would be low-value churn that obscures the types that
  matter; type-level prose + a warning-clean build is the right, enforceable bar.
- **Keep README.md as the Doxygen mainpage** — rejected: its repo-relative links cannot resolve
  inside the Doxygen input and produced seven `\ref` warnings; making them absolute URLs would
  degrade the README for GitHub readers. A dedicated API landing page is clearer anyway.
- **Leave the docs build unenforced (`WARN_AS_ERROR = NO`)** — rejected: that is exactly how the
  broken code fences and the undocumented parameter shipped unnoticed.
- **A formal "public API" header manifest / ABI dump** — deferred: for a header-only C++20
  library with a small compiled tier, the namespace rule (non-`detail` in `util.hpp`) plus the
  Doxygen catalogue is a sufficient, low-maintenance contract; a machine-checked ABI baseline can
  come with the compiled tier's post-1.0 maintenance if needed.

## Consequences

- A documentation regression (a broken reference, a malformed `@param`, an unterminated code
  fence) now fails the CI `docs` job rather than passing silently.
- The public/implementation boundary is explicit: `it::d4np::util` is the contract,
  `it::d4np::util::detail` is not; SemVer reasoning has a concrete surface.
- The Doxygen site opens on an API-focused overview; README.md remains the human-facing landing
  page in the repository (it is simply no longer parsed by Doxygen).
- New public types must ship with a doc block (already the working norm, now enforced). Trivial
  STL-conformance members are not required to carry individual prose.
- Patterns catalogue: unchanged.

## References

- Spec §3 (documentation as a non-functional requirement), §5 (public interface), §11 (SemVer).
- ADR-0004 (header-only vs compiled tier — the definition of the public surface).
- Doxygen `WARN_AS_ERROR` (`FAIL_ON_WARNINGS_PRINT`), `USE_MDFILE_AS_MAINPAGE`, `EXTRACT_ALL`.
