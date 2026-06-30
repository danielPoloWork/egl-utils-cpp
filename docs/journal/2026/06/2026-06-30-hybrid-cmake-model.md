# 2026-06-30 — Hybrid CMake model (roadmap 1.8)

## What got done

- Established the hybrid build model (ADR-0004): the header-only `egl-util::egl-util`
  INTERFACE target now sits alongside an optional STATIC tier `egl-util::egl-util-static`,
  gated by `EGL_UTIL_BUILD_STATIC` (default OFF; ON in the project's presets/CI).
- Both targets moved into the component-level CMakeLists at
  `src/main/cpp/it/d4np/util/`; the root CMakeLists now passes the include root and adds the
  subdirectory. The static tier links the interface tier `PUBLIC`, so it is a strict superset.
- Seeded the compiled tier with `it::d4np::util::library_version()` (`version.cpp`) — the
  out-of-line counterpart to the header-only `version_string`, useful as a header/binary skew
  check. Declared in `version.hpp`, covered by `library_version_test.cpp` (compiled into
  `util_tests` only when the static tier is enabled).
- Defined `DOCTEST_CONFIG_USE_STD_HEADERS` on `util_tests` to fix a doctest/MSVC clash
  surfaced by the first non-main test that includes `<string_view>` transitively.
- Completed Milestone 1: README milestone table now marks M1 ✅ done.

## Project state

- Milestones 1 and 2 complete. Version remains `0.0.0` (release PRs are cut separately).
- Verified locally on MSVC 14.51 (VS BuildTools 18): both the static-ON and header-only
  (static-OFF) configurations build and pass `ctest`.

## How the next session resumes

- Next roadmap item: **3.1 — `UniqueRef<T>`** (non-null unique-ownership pointer, component
  #2), the first item of Milestone 3 (Memory & Resource Management). Header-only; lives under
  `src/main/cpp/it/d4np/util/`, added to the umbrella header, covered by doctest + sanitizers.
- One PR at a time: wait for the 1.8 PR to merge before branching the next item.
