# ADR-0004: Adopt a hybrid header-only + optional STATIC build model

- **Status:** Accepted
- **Date:** 2026-06-30
- **Deciders:** Maintainer
- **Related:** ADR-0002, AGENTS.md §3, §10; ROADMAP 1.8; spec §4

## Context

`egl-util-cpp` is described as a *"header-only (with optional partial compilation)"* C++20
toolkit. Most components — type traits, hashing, contiguous containers, zero-copy strings —
are naturally header-only: they are templates or `constexpr` and carry zero link-time cost.

A minority of the planned surface cannot stay header-only without leaking heavy, order-
sensitive system headers into every consumer translation unit. Milestone 9 (I/O &
Networking: `FileStream`, `TcpSocket`/`TcpServer`, `BinarySerializer`) is explicitly *"the
OS-API-heavy compiled tier"*: pulling `<winsock2.h>`, `<sys/socket.h>`, `epoll`, etc. into a
header forces those macros and platform quirks onto everyone who includes the umbrella
header, and inflates compile times for users who never touch I/O.

Until now the build defined a single `egl-util` INTERFACE (header-only) target. We need a
shape that keeps the header-only default — zero dependencies, nothing to link — while giving
the compiled components a home, decided before any such component lands so the boundary is
stable rather than retrofitted.

## Decision

We adopt a **hybrid two-tier build model**:

- **`egl-util` / `egl-util::egl-util`** — an INTERFACE (header-only) target. It is the
  default and unchanged contract: it carries the include root and the C++20 requirement and
  has zero link-time cost. Header-only consumers depend on this and nothing else.
- **`egl-util-static` / `egl-util::egl-util-static`** — an optional STATIC target, gated
  behind the `EGL_UTIL_BUILD_STATIC` CMake option (default `OFF`). It compiles the
  translation units that require separate compilation and links the INTERFACE target
  `PUBLIC`, so it is a **strict superset**: anything that compiles and links against
  `egl-util::egl-util` also compiles and links against `egl-util::egl-util-static`.

The targets live in the component CMakeLists at `src/main/cpp/it/d4np/util/`. The static
tier is seeded with one genuinely-compiled symbol, `it::d4np::util::library_version()` (in
`version.cpp`): the out-of-line counterpart to the header-only `version_string` constant,
which records the version the binary was built from and so exposes header/binary skew. The
project's own presets and CI enable `EGL_UTIL_BUILD_STATIC` so both tiers are built and
tested on every cell; downstream consumers keep the header-only default.

## Alternatives Considered

- **Stay header-only-only.** Rejected — it would force every OS-heavy Milestone-9 component
  to inline platform headers into the umbrella header, imposing platform macros and compile-
  time cost on all consumers, including those who use only `constexpr` utilities.
- **Always build the STATIC library (no option).** Rejected — it taxes the common
  header-only consumer with a link dependency and an archive they never use, contradicting
  the zero-dependency promise in AGENTS §3.
- **A SHARED (dynamic) library tier.** Rejected for now — it adds ABI-stability and
  symbol-visibility obligations the project is not ready to commit to pre-1.0. A STATIC tier
  defers those costs; a SHARED tier can be added later behind its own option and ADR.
- **A separate compiled sibling package.** Rejected — it fragments the public surface and
  the `it::d4np::util` namespace across two repositories for no compatibility benefit.

## Consequences

- Consumers choose their tier explicitly: `egl-util::egl-util` (header-only, the default) or
  `egl-util::egl-util-static` (adds the compiled symbols). Linking the static tier is
  required only to call out-of-line functions such as `library_version()`; calling one
  without the static tier is an unresolved-symbol error at link time, documented on the
  declaration.
- The static tier must always have at least one translation unit to be a valid CMake target;
  `version.cpp` provides it today, and Milestone-9 components will join it.
- CI cost grows modestly: every build/test cell now also compiles and links the static
  archive and runs the `library_version()` test (added to `util_tests` only when the tier is
  enabled). The Valgrind cell exercises the compiled tier for free.
- The model is reversible per consumer (flip the option) and extensible (a future SHARED tier
  is additive). Changing the *header-only-by-default* contract would require superseding this
  ADR.

## References

- AGENTS.md §3 (Project Overview — "header-only with optional partial compilation"), §10
  (Enterprise Quality Bar).
- ROADMAP.md item 1.8.
- `docs/specs/01_spec_util.md` §4 (Logical architecture).
- ADR-0002 (cross-language source layout — where the targets and TUs live).
