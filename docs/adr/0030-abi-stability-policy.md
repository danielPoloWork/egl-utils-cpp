# ADR-0030: Distribution model & ABI-stability policy

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** [ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md) (the hybrid
  two-tier build model this makes an explicit stability contract for), spec §1 / §3 / §4,
  AGENTS §11 (SemVer), [`docs/workflow/maintenance.md`](../workflow/maintenance.md) (the SemVer
  decision tree), roadmap 11.4

## Context

The post-1.0 specification review flagged that the phrase *"header-only **and** partial
compilation"* reads as two mutually-exclusive distribution models, and that the project states
SemVer for its **API** but never says what — if anything — it guarantees at the **ABI** level.
[ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md) already settled the *build*
shape (one hybrid model: a header-only INTERFACE target plus an optional STATIC superset), and
[`maintenance.md`](../workflow/maintenance.md) already defines the SemVer decision tree — but
"ABI where applicable" was left undefined. For a C++ library that word matters: ABI breakage is
silent (it links and crashes, rather than failing to compile), so the guarantee has to be stated,
not assumed. This ADR closes that gap by naming the single distribution model unambiguously and
writing down the exact stability contract for each tier.

## Decision

**`egl-util-cpp` has one distribution model — source distribution of a hybrid library — and
makes an API/source-stability guarantee under SemVer, but deliberately makes *no cross-toolchain
binary-ABI guarantee*. Both tiers are consumed as source, built by the consumer with the
consumer's own toolchain.**

- **One model, two tiers, not two models.** The library is header-only *by default*; the STATIC
  tier is an **opt-in superset** of the same source tree ([ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md)),
  not an alternative product. "Header-only with optional partial compilation" means exactly this
  hybrid — the two tiers are additive, never exclusive. Wording that suggests otherwise is a
  documentation bug.

- **What SemVer protects (the *API/source* surface):** the public types and functions in
  `it::d4np::util`, their signatures and documented semantics, the two CMake target names
  (`egl-util::egl-util`, `egl-util::egl-util-static`), the `EGL_UTIL_BUILD_STATIC` option, and the
  include root `<it/d4np/util/…>`. A change that makes an existing consumer **fail to compile or
  link, or behave differently**, is a MAJOR per the [maintenance.md](../workflow/maintenance.md)
  decision tree. This is the guarantee consumers actually rely on for a source-built library.

- **ABI stability is explicitly *out of scope* for now — and that is the correct, honest
  position:**
  - The **header-only tier is not an ABI at all.** Its templates, `inline`, and `constexpr`
    entities are compiled *into the consumer's* translation units; there is no library binary,
    so its "ABI" is whatever the consumer's compiler and flags produce. Nothing here to promise
    or break across toolchains — only the source API matters.
  - The **STATIC tier ships as source, not as a prebuilt archive.** The consumer's build produces
    the `.a`/`.lib` with the consumer's compiler, standard-library, and flags, so the object-level
    ABI is theirs, not ours. We therefore do **not** promise that an archive built by one
    toolchain links against objects built by another. What we *do* guarantee is that the
    *linkable symbol surface* (e.g. `it::d4np::util::library_version()`) is API/source-stable
    under SemVer, and that **header/binary skew is detectable**: `library_version()` (out-of-line,
    `version.cpp`) returns the version its TU was built from, to compare against the header-only
    `version_string` constant (ADR-0004).
  - There is **no SHARED/dynamic tier.** A prebuilt shared library is the only artifact for which
    a cross-toolchain binary-ABI guarantee would be meaningful, and the project ships none. Adding
    one later is additive and would carry **its own ADR** committing to symbol visibility, an
    export map, and an ABI-stability window — obligations this ADR declines to take on today.

- **Consequently, mixing binaries built from different `egl-util-cpp` versions or toolchains is
  unsupported: rebuild consumers from source against a single version.** This is the standard
  contract for a header-first, source-distributed C++ library.

## Alternatives Considered

- **Promise a stable binary ABI for the STATIC tier now** — rejected: the archive is built in the
  consumer's environment, so its ABI is a function of *their* compiler/stdlib/flags, not ours; a
  guarantee we cannot enforce is worse than an honest "none." It would also freeze internal
  layouts prematurely.
- **Ship a SHARED library with a versioned ABI** — rejected (deferred): it is the only shape where
  a binary-ABI promise is real, but it demands symbol-visibility control, an export/version map,
  and an ongoing ABI-review discipline. Additive later behind its own option and ADR; not owed at
  1.0.
- **Leave ABI "undefined / where applicable"** (the status quo) — rejected: silence on ABI is the
  trap the review named. Stating "API-stable, ABI-not-guaranteed, rebuild from source" removes the
  ambiguity even though the practical outcome is the same.
- **Fold this into ADR-0004** — rejected: ADR-0004 is an accepted, dated record of the *build*
  decision. The stability *contract* is a distinct, later decision; a new ADR that references 0004
  keeps the history honest (AGENTS §7).

## Consequences

- Spec §1 is reworded to name the single hybrid model (no "two models" ambiguity) and §3's
  API-stability bullet now points here for the ABI position; `maintenance.md`'s "what the version
  protects" cites this ADR instead of the vague "ABI where applicable".
- Consumers get a crisp rule: **build everything from one version of the source with one
  toolchain; trust SemVer for the API; expect no binary compatibility across versions/toolchains.**
- The door to a SHARED tier stays open and is now explicitly gated on a future ADR, so no one
  mistakes today's silence for a promise.
- No code change and no pattern-catalogue change: this is a policy/stability contract.

## References

- ADR-0004 (hybrid header-only + optional STATIC build model), ADR-0002 (source layout).
- spec §1 (distribution), §3 (NFR: stable public API), §4 (logical architecture / tiers).
- [`docs/workflow/maintenance.md`](../workflow/maintenance.md) (SemVer decision tree),
  AGENTS §11 (versioning & release).
- `version.cpp` / `version.hpp` (`library_version()` vs `version_string` — header/binary skew).
