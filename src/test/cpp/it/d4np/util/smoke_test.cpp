// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Milestone 1 demonstrative smoke test: proves the build system, the doctest harness,
// and the public umbrella header are wired correctly. Real per-component tests arrive
// with each module (roadmap M2..M9).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <it/d4np/util/util.hpp>

#include <string_view>

TEST_CASE("version constants are coherent with the released version") {
    CHECK(it::d4np::util::version_major == 0);
    CHECK(it::d4np::util::version_minor == 0);
    CHECK(it::d4np::util::version_patch == 0);
    CHECK(it::d4np::util::version_string == std::string_view{"0.0.0"});
}
