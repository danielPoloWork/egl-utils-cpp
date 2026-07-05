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

### Changed

### Deprecated

### Removed

### Fixed

### Security

---

## Released versions

| Version | Date | Changelog |
|---------|------|-----------|
| v1.0.0 | 2026-07-05 | [docs/changelog/v1/v1.0.0.md](docs/changelog/v1/v1.0.0.md) |
