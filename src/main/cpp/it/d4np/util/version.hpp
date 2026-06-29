// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Version constants for egl-util-cpp. The macro form is preprocessor-checkable
// (e.g. `#if D4NP_UTIL_VERSION_MAJOR >= 1`); the namespaced constants are the
// idiomatic C++ surface. Keep the value in lockstep with the README Status badge
// (enforced by tools/consistency_lint.py).
#ifndef IT_D4NP_UTIL_VERSION_HPP
#define IT_D4NP_UTIL_VERSION_HPP

#include <string_view>

#define D4NP_UTIL_VERSION_MAJOR 0
#define D4NP_UTIL_VERSION_MINOR 0
#define D4NP_UTIL_VERSION_PATCH 0
#define D4NP_UTIL_VERSION_STRING "0.0.0"

namespace it::d4np::util {

inline constexpr int version_major = D4NP_UTIL_VERSION_MAJOR;
inline constexpr int version_minor = D4NP_UTIL_VERSION_MINOR;
inline constexpr int version_patch = D4NP_UTIL_VERSION_PATCH;
inline constexpr std::string_view version_string = D4NP_UTIL_VERSION_STRING;

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_VERSION_HPP
