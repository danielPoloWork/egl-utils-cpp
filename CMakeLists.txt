# egl-util-cpp — header-only C++20 high-performance utilities library.
# The library itself is an INTERFACE (header-only) target with zero runtime dependencies;
# test and benchmark dependencies are gated behind options so a consumer pays for nothing.
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
option(EGL_UTIL_BUILD_DOCS "Add the 'docs' Doxygen target when Doxygen is available" ${EGL_UTIL_IS_TOP_LEVEL})

# Emit a compile database for clang-tidy and editor tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep all runtime artifacts in the build root so CI paths (e.g. build/debug/util_tests)
# are stable across generators.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# --- The header-only library ---------------------------------------------------------------
add_library(egl-util INTERFACE)
add_library(egl-util::egl-util ALIAS egl-util)

target_include_directories(
    egl-util INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp>"
                       "$<INSTALL_INTERFACE:include>")
target_compile_features(egl-util INTERFACE cxx_std_20)

# --- Tests & benchmarks --------------------------------------------------------------------
if(EGL_UTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/test/cpp/it/d4np/util)
endif()

if(EGL_UTIL_BUILD_BENCH)
    add_subdirectory(src/bench/cpp/it/d4np/util)
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
        set(DOXYGEN_EXTRACT_ALL YES)
        set(DOXYGEN_GENERATE_HTML YES)
        set(DOXYGEN_GENERATE_LATEX NO)
        set(DOXYGEN_QUIET YES)
        set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
        doxygen_add_docs(
            docs "${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp" "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            COMMENT "Generating API documentation with Doxygen")
    else()
        message(STATUS "Doxygen not found; the 'docs' target is unavailable.")
    endif()
endif()
