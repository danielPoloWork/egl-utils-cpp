# Changelog

All notable changes to `egl-util-cpp` are documented here, following
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning 2.0.0](https://semver.org/).

Every PR that introduces a user-visible change adds a line to `[Unreleased]` in the same
PR. A release PR moves the `[Unreleased]` entries into a new per-version file under
`docs/changelog/v<MAJOR>/v<X.Y.Z>.md` and adds an index row below.

## [Unreleased]

### Added

- Documentation: ADR-0029 recording the library-wide error-handling policy (value-or-error
  vs exceptions; why `std::expected` is not adopted at the C++20 floor), backing spec §5.
  Opens Milestone 11 (Specification & Assurance Hardening).
- Documentation: `docs/architecture/c4-component-diagram.md` — the C4 Level-3 component view
  of the 25 components across their seven modules, with the actual internal dependency edges
  (roadmap 11.2).
- Documentation: `docs/architecture/component-contracts.md` — a per-component contract table
  (thread-safety, exception-safety, allocation behavior, algorithmic complexity) for all 25
  components (roadmap 11.3).
- Documentation: ADR-0030 — distribution model & ABI-stability policy: names the single hybrid
  model (header-only + optional STATIC superset) and states an API/source-stability guarantee
  under SemVer with no cross-toolchain binary-ABI promise (source distribution). Spec §1/§3 and
  `maintenance.md` reworded to match (roadmap 11.4).
- Documentation: `docs/benchmarks/performance-targets.md` — numeric performance targets for the
  eight benchmarked hot paths: machine-independent algorithmic-class invariants plus relative
  regression thresholds (≤1.25× baseline median, p99 ≤2× median) with a stated methodology
  (roadmap 11.6).
- Security: coverage-guided libFuzzer harnesses for the untrusted-input components (`JsonParser`,
  `CliParser`, `BinaryDeserializer`) under `src/fuzz/`, gated by `EGL_UTIL_BUILD_FUZZERS` with a
  `fuzz` preset and a CI smoke job; a threat model (`docs/security/threat-model.md`); and extended
  NIST FIPS 180-4 SHA-256 test vectors (ADR-0031, roadmap 11.5).

### Changed

- Documentation: `hash.hpp` now states the hashes' **non-cryptographic** scope explicitly —
  SHA-256 is an integrity/checksum digest, not a security primitive (not constant-time; not for
  passwords/MACs/signatures). Corrects the prior "the cryptographic digest" wording (ADR-0031).

### Deprecated

### Removed

### Fixed

### Security

---

## Released versions

| Version | Date | Changelog |
|---------|------|-----------|
| v1.0.0 | 2026-07-05 | [docs/changelog/v1/v1.0.0.md](docs/changelog/v1/v1.0.0.md) |
