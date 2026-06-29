# Local Build & Test

How to build, test, and check `egl-util-cpp` on your machine. CI runs the same commands
on Linux x86_64 (GCC>=11, Clang>=14), Windows x86_64 (MSVC>=19.30), macOS arm64 (Apple Clang>=14); reproducing them locally avoids a red round-trip.

## Prerequisites

- **C++20** toolchain.
- **Build system:** CMake (>=3.20) + Ninja, CMakePresets.json.
- **Package manager:** vcpkg / Conan (test/bench deps only; library stays zero-dependency).
- **Formatter / linter:** clang-format (LLVM-derived, 4-space, 120 col), clang-tidy (bugprone/cert/cppcoreguidelines/modernize/performance/portability/readability).
- **Docs:** Doxygen (for the API docs build).

## Commands

```bash
# Build
cmake --build --preset debug

# Test
ctest --preset debug --output-on-failure

# Format check
clang-format --dry-run --Werror <files>

# Lint
clang-tidy -p build/debug --warnings-as-errors='*' <changed .cpp/.hpp/.h>

# Benchmark
cmake --build --preset bench

# Cross-artifact congruence (run before drafting any PR)
python tools/consistency_lint.py
```

## Before you open a PR

1. `clang-format --dry-run --Werror <files>` and `clang-tidy -p build/debug --warnings-as-errors='*' <changed .cpp/.hpp/.h>` are clean.
2. `ctest --preset debug --output-on-failure` passes; new/changed behavior is covered (≥ 80% line).
3. ASan, UBSan, TSan, Valgrind are green where applicable.
4. `python tools/consistency_lint.py` passes.
5. The relevant docs (README, ROADMAP, ADRs, patterns, changelog) are updated in the same
   PR — see [`../workflow/documentation.md`](../workflow/documentation.md).
