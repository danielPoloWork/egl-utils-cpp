// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for StringSplitter (component #11, roadmap 5.1): the zero-allocation string_view
// splitter. Covers basic splitting, preserved empty fields, leading/trailing delimiters, the
// no-delimiter and empty-input cases, multi-character delimiters, and compile-time use.
#include <doctest/doctest.h>

#include <it/d4np/util/string_splitter.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using it::d4np::util::StringSplitter;

[[nodiscard]] std::vector<std::string> collect(std::string_view text, std::string_view delimiter) {
    std::vector<std::string> out;
    for (const std::string_view token : StringSplitter{text, delimiter}) {
        out.emplace_back(token);
    }
    return out;
}

// --- Compile-time use: the whole splitter is constexpr -------------------------------------
constexpr std::size_t count_tokens(std::string_view text, std::string_view delimiter) {
    std::size_t count = 0;
    for (const std::string_view token : StringSplitter{text, delimiter}) {
        (void)token;
        ++count;
    }
    return count;
}
static_assert(count_tokens("a,b,c", ",") == 3, "three fields");
static_assert(count_tokens("", ",") == 1, "empty input yields one empty token");
static_assert(count_tokens("a,,b", ",") == 3, "empty fields are preserved");
static_assert(*StringSplitter{"hello world", " "}.begin() == "hello", "first token");

} // namespace

TEST_CASE("splits a simple delimited string") {
    CHECK(collect("a,b,c", ",") == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("preserves empty fields between delimiters") {
    CHECK(collect("a,,b", ",") == std::vector<std::string>{"a", "", "b"});
}

TEST_CASE("preserves leading and trailing empty fields") {
    CHECK(collect(",a,", ",") == std::vector<std::string>{"", "a", ""});
}

TEST_CASE("a string with no delimiter yields one token") {
    CHECK(collect("abc", ",") == std::vector<std::string>{"abc"});
}

TEST_CASE("an empty string yields a single empty token") { CHECK(collect("", ",") == std::vector<std::string>{""}); }

TEST_CASE("supports multi-character delimiters") {
    CHECK(collect("a::b::c", "::") == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("tokens are views into the original text (zero copy)") {
    const std::string_view text = "one two three";
    const auto first = *StringSplitter{text, " "}.begin();
    CHECK(first == "one");
    CHECK(first.data() == text.data()); // same underlying storage, not a copy
}

TEST_CASE("range-for visits every token in order") {
    std::vector<std::string_view> tokens;
    for (const std::string_view token : StringSplitter{"x-y-z", "-"}) {
        tokens.push_back(token);
    }
    REQUIRE(tokens.size() == 3);
    CHECK(tokens.front() == "x");
    CHECK(tokens.back() == "z");
}
