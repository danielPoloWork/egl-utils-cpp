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
