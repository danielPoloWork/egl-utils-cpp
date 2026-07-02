# egl-util-cpp

> Header-only C++20 toolkit for high-performance systems: controlled allocation, advanced concurrency, zero-copy strings, contiguous containers, I/O & networking, diagnostics, and parsing.

![Status](https://img.shields.io/badge/Status-v0.0.0-blue)

Part of the **Enterprise-Grade Libraries (EGL)** series. A
library written in **C++20**, built and governed to an enterprise quality
bar: full CI matrix, static analysis, sanitizers, documented design decisions, and SemVer
releases.

## What it is

egl-util-cpp is a header-only (with optional partial compilation) C++20 library for building high-performance systems with controlled resource allocation and advanced concurrency. It targets open-source C++ developers who need a zero-dependency, cache-friendly, leak-free toolkit spanning allocation, concurrency, zero-copy strings, contiguous containers, I/O & networking, diagnostics, and parsing without pulling in a heavyweight framework. The design rests on three pillars: strict RAII for safe resource lifetimes, zero-copy data flow (move semantics, std::string_view, std::span), and compile-time optimization via SFINAE/Concepts and constexpr.

The frozen specification is in
[`docs/specs/01_spec_util.md`](docs/specs/01_spec_util.md).

## Build, test, run

```bash
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

- **Toolchain:** CMake (>=3.20) + Ninja, CMakePresets.json, doctest (FetchContent, test scope only), clang-format (LLVM-derived, 4-space, 120 col), clang-tidy (bugprone/cert/cppcoreguidelines/modernize/performance/portability/readability).
- **Supported platforms:** Linux x86_64 (GCC>=11, Clang>=14), Windows x86_64 (MSVC>=19.30), macOS arm64 (Apple Clang>=14).
- Consumers import the public surface via: `#include <it/d4np/util/util.hpp>`.

### Linking (hybrid model)

The library ships two CMake targets ([ADR-0004](docs/adr/0004-adopt-hybrid-header-only-plus-static-build-model.md)):

| Target | Kind | When to use |
|---|---|---|
| `egl-util::egl-util` | INTERFACE (header-only) | The default. Zero dependencies, nothing to link. |
| `egl-util::egl-util-static` | STATIC (opt-in: `-DEGL_UTIL_BUILD_STATIC=ON`) | A strict superset that also provides the compiled components (the OS-API-heavy tier). |

```cmake
# Header-only (default):
target_link_libraries(my_app PRIVATE egl-util::egl-util)
# Or, when you need the compiled tier:
target_link_libraries(my_app PRIVATE egl-util::egl-util-static)
```

See [`docs/development/local-build.md`](docs/development/local-build.md) for the full local
setup.

## How this project is run

| Document | Purpose |
|---|---|
| [`AGENTS.md`](AGENTS.md) | How AI agents (and humans) work in this repo — the contract. |
| [`ROADMAP.md`](ROADMAP.md) | The numbered plan and what is done. |
| [`docs/adr/`](docs/adr/) | Why it is built the way it is (Architecture Decision Records). |
| [`docs/patterns/`](docs/patterns/) | Design patterns adopted, rejected, or considered. |
| [`docs/workflow/`](docs/workflow/) | Git, documentation, release, and maintenance conventions. |
| [`CHANGELOG.md`](CHANGELOG.md) | User-visible changes per release. |
| [`SECURITY.md`](SECURITY.md) | How to report a vulnerability. |

## Milestones

| # | Title | Status |
|---|---|---|
| 1 | Project bootstrap & CI | ✅ done |
| 2 | Foundations — Type Traits & Hashing | ✅ done |
| 3 | Memory & Resource Management | ✅ done |
| 4 | Contiguous Containers | ✅ done |
| 5 | Zero-Copy Strings | ✅ done |
| 6 | Concurrency & Multithreading | ✅ done |
| 7 | Diagnostics & Instrumentation | ✅ done |
| 8 | Parsing & Input | ⏳ planned |
| 9 | I/O & Networking | ⏳ planned |
| 10 | Hardening & 1.0 | ⏳ planned |


## License

MIT © 2026 Daniel Polo. See [`LICENSE`](LICENSE).
