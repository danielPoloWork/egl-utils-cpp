# egl-util-cpp — C++20 high-performance utilities library.
# The library follows a HYBRID model (ADR-0004): an INTERFACE (header-only) tier with zero
# runtime dependencies, plus an optional STATIC tier that gates the compiled translation
# units. Test, benchmark, docs, and the static tier are all gated behind options so a
# header-only consumer pays for nothing.
cmake_minimum_required(VERSION 3.20)

project(
    egl-util-cpp
    VERSION 0.0.0
    DESCRIPTION "Header-only C++20 toolkit for high-performance systems"
    HOMEPAGE_URL "https://github.com/danielPoloWork/egl-util-cpp"
    LANGUAGES CXX)

# Detect whether we are the top-level project (vs. pulled in via add_subdirectory).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(EGL_UTIL_IS_TOP_LEVEL ON)
else()
    set(EGL_UTIL_IS_TOP_LEVEL OFF)
endif()

option(EGL_UTIL_BUILD_TESTS "Build the egl-util-cpp test suite" ${EGL_UTIL_IS_TOP_LEVEL})
option(EGL_UTIL_BUILD_BENCH "Build the egl-util-cpp benchmark suite" ${EGL_UTIL_IS_TOP_LEVEL})
# Fuzz harnesses for the untrusted-input components (ADR-0031). Built in standalone replay mode
# by default (so they compile on the whole matrix and stay in the clang-tidy compile database);
# set EGL_UTIL_FUZZER_ENGINE=libfuzzer with a Clang toolchain for coverage-guided fuzzing.
option(EGL_UTIL_BUILD_FUZZERS "Build the egl-util-cpp fuzz harnesses" ${EGL_UTIL_IS_TOP_LEVEL})
set(EGL_UTIL_FUZZER_ENGINE "" CACHE STRING "Fuzzing engine: '' (standalone replay) or 'libfuzzer'")
option(EGL_UTIL_BUILD_DOCS "Add the 'docs' Doxygen target when Doxygen is available" ${EGL_UTIL_IS_TOP_LEVEL})
# Header-only is the default contract; the compiled STATIC tier is strictly opt-in (ADR-0004).
option(EGL_UTIL_BUILD_STATIC "Build the optional compiled STATIC library tier" OFF)

# Line-coverage instrumentation for the CI `coverage` gate (roadmap 10.3, ADR-0028). GCC/Clang
# only (MSVC has no --coverage); a no-op elsewhere, so the option is safe to leave defined.
option(EGL_UTIL_COVERAGE "Instrument for line coverage (GCC/Clang)" OFF)
if(EGL_UTIL_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# Emit a compile database for clang-tidy and editor tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep all runtime artifacts in the build root so CI paths (e.g. build/debug/util_tests)
# are stable across generators.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# --- The library targets (hybrid INTERFACE + optional STATIC tier) -------------------------
# The include root that maps `#include <it/d4np/util/...>` onto the source tree; passed to
# the component CMakeLists so both tiers share one definition.
set(EGL_UTIL_SRC_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp")
add_subdirectory(src/main/cpp/it/d4np/util)

# --- Tests & benchmarks --------------------------------------------------------------------
if(EGL_UTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/test/cpp/it/d4np/util)
endif()

if(EGL_UTIL_BUILD_BENCH)
    add_subdirectory(src/bench/cpp/it/d4np/util)
endif()

if(EGL_UTIL_BUILD_FUZZERS)
    add_subdirectory(src/fuzz/cpp/it/d4np/util)
endif()

# --- API documentation (Doxygen) -----------------------------------------------------------
# Adds an opt-in `docs` target (built with `cmake --build --preset <p> --target docs`); it is
# not part of the default build, so a toolchain without Doxygen still builds the library.
if(EGL_UTIL_BUILD_DOCS)
    find_package(Doxygen)
    if(Doxygen_FOUND)
        set(DOXYGEN_PROJECT_NAME "egl-util-cpp")
        set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
        set(DOXYGEN_PROJECT_BRIEF "${PROJECT_DESCRIPTION}")
        set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen")
        set(DOXYGEN_RECURSIVE YES)
        # EXTRACT_ALL renders the whole surface (incl. STL-conformance members that inherit their
        # meaning); every public type carries a prose doc block, and WARN_AS_ERROR below keeps the
        # documentation that exists well-formed and the build clean (roadmap 10.1, ADR-0026).
        set(DOXYGEN_EXTRACT_ALL YES)
        set(DOXYGEN_GENERATE_HTML YES)
        set(DOXYGEN_GENERATE_LATEX NO)
        set(DOXYGEN_QUIET YES)
        # Fail the docs build on any Doxygen warning: the 1.0 public API is frozen and every
        # public symbol must stay documented (roadmap 10.1, ADR-0026).
        set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS_PRINT)
        # A curated, API-focused landing page rather than README.md — the README's repo-relative
        # links resolve on GitHub but not inside the Doxygen input set.
        set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen-mainpage.md")
        doxygen_add_docs(
            docs "${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp" "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen-mainpage.md"
            COMMENT "Generating API documentation with Doxygen")
    else()
        message(STATUS "Doxygen not found; the 'docs' target is unavailable.")
    endif()
endif()
