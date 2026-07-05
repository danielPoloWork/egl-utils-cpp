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

#define D4NP_UTIL_VERSION_MAJOR 1
#define D4NP_UTIL_VERSION_MINOR 0
#define D4NP_UTIL_VERSION_PATCH 0
#define D4NP_UTIL_VERSION_STRING "1.0.0"

namespace it::d4np::util {

inline constexpr int version_major = D4NP_UTIL_VERSION_MAJOR;
inline constexpr int version_minor = D4NP_UTIL_VERSION_MINOR;
inline constexpr int version_patch = D4NP_UTIL_VERSION_PATCH;
inline constexpr std::string_view version_string = D4NP_UTIL_VERSION_STRING;

/// Returns the version string the *compiled* library tier was built from.
///
/// Unlike @ref version_string — a header-only constant resolved in the caller's own
/// translation unit — this reads the value baked into the compiled static library, so a
/// disagreement between the two reveals header/binary skew. It is the single ODR-bound
/// symbol that anchors the optional STATIC tier (ADR-0004).
///
/// @note Requires linking `egl-util::egl-util-static`. Header-only consumers (those linking
///       only `egl-util::egl-util`) must use @ref version_string instead — calling this
///       function without the static tier is an unresolved-symbol error at link time.
[[nodiscard]] std::string_view library_version() noexcept;

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_VERSION_HPP
