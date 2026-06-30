// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Compiled tier of egl-util-cpp. This translation unit anchors the optional STATIC library
// (egl-util::egl-util-static, ADR-0004): it provides the out-of-line definition of
// library_version(), the single ODR-bound symbol that records which version the binary was
// compiled from. Header-only consumers never link this translation unit.
#include <it/d4np/util/version.hpp>

#include <string_view>

namespace it::d4np::util {

std::string_view library_version() noexcept { return D4NP_UTIL_VERSION_STRING; }

} // namespace it::d4np::util
