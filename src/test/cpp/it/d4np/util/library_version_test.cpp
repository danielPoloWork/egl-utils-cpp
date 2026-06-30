// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Exercises the compiled STATIC tier (egl-util::egl-util-static, ADR-0004): the out-of-line
// library_version() must agree with the header-only version_string constant. This file is
// compiled into util_tests only when EGL_UTIL_BUILD_STATIC is enabled, so the symbol is
// guaranteed to resolve at link time.
#include <doctest/doctest.h>

#include <it/d4np/util/version.hpp>

#include <string_view>

TEST_CASE("compiled library_version() agrees with the header-only version_string") {
    CHECK(it::d4np::util::library_version() == it::d4np::util::version_string);
    CHECK(it::d4np::util::library_version() == std::string_view{"0.0.0"});
}
