// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for StringBuilder (component #10, roadmap 5.2): the fluent string builder. Covers
// chained appends, string/char/integer overloads, operator<<, reserve/clear/size, and the
// value- and move-extraction paths.
#include <doctest/doctest.h>

#include <it/d4np/util/string_builder.hpp>

#include <string>
#include <utility>

namespace {

using it::d4np::util::StringBuilder;

} // namespace

TEST_CASE("a default builder is empty") {
    StringBuilder builder;
    CHECK(builder.empty());
    CHECK(builder.size() == 0);
    CHECK(builder.view().empty());
}

TEST_CASE("append is fluent and chains") {
    StringBuilder builder;
    StringBuilder &same = builder.append("a").append("b").append("c");
    CHECK(&same == &builder); // each append returns *this
    CHECK(builder.view() == "abc");
    CHECK(builder.size() == 3);
}

TEST_CASE("appends strings, chars, and integers") {
    StringBuilder builder;
    builder.append("count=").append(42).append(' ').append("neg=").append(-7);
    CHECK(builder.view() == "count=42 neg=-7");
}

TEST_CASE("integer append handles the full width and unsigned values") {
    StringBuilder builder;
    builder.append(0).append(',').append(9223372036854775807LL).append(',').append(18446744073709551615ULL);
    CHECK(builder.view() == "0,9223372036854775807,18446744073709551615");
}

TEST_CASE("operator<< is a synonym for append") {
    StringBuilder builder;
    builder << "x=" << 10 << ';' << "y=" << 20;
    CHECK(builder.view() == "x=10;y=20");
}

TEST_CASE("the initial-value and capacity constructors work") {
    StringBuilder seeded{std::string_view{"seed"}};
    CHECK(seeded.view() == "seed");
    seeded.append("ed");
    CHECK(seeded.view() == "seeded");

    StringBuilder reserved{128};
    CHECK(reserved.capacity() >= 128);
    CHECK(reserved.empty());
}

TEST_CASE("reserve raises capacity and clear empties without shrinking meaning") {
    StringBuilder builder;
    builder.reserve(64);
    CHECK(builder.capacity() >= 64);
    builder.append("data");
    builder.clear();
    CHECK(builder.empty());
    CHECK(builder.view().empty());
}

TEST_CASE("str() copies on an lvalue and moves on an rvalue") {
    StringBuilder builder;
    builder.append("payload");
    const std::string &ref = builder.str(); // lvalue: reference to the buffer
    CHECK(ref == "payload");

    std::string moved = std::move(builder).str(); // rvalue: moves the buffer out
    CHECK(moved == "payload");
    CHECK(builder.empty()); // NOLINT(bugprone-use-after-move) — str()&& guarantees an empty builder
}
