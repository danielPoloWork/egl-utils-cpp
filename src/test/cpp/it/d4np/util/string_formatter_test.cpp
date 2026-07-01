// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for StringFormatter (component #12, roadmap 5.3). The compile-time validator is
// exercised with static_assert (its failure mode — a build error — cannot appear in a runtime
// suite, so the underlying constexpr checker is asserted directly); rendering is covered per
// Formattable category, plus escapes, placement, and the format_to appending path.
#include <doctest/doctest.h>

#include <it/d4np/util/string_formatter.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

using it::d4np::util::FormatString;
using it::d4np::util::StringBuilder;
using it::d4np::util::StringFormatter;

namespace detail = it::d4np::util::detail;
using detail::FormatStringError;

// --- Compile-time validation -----------------------------------------------------------------

static_assert(detail::check_format_string("", 0) == FormatStringError::none);
static_assert(detail::check_format_string("plain text", 0) == FormatStringError::none);
static_assert(detail::check_format_string("{}", 1) == FormatStringError::none);
static_assert(detail::check_format_string("a{}b{}c", 2) == FormatStringError::none);
static_assert(detail::check_format_string("{{}}", 0) == FormatStringError::none);
static_assert(detail::check_format_string("{{{}}}", 1) == FormatStringError::none);
static_assert(detail::check_format_string("{", 0) == FormatStringError::unterminated_open_brace);
static_assert(detail::check_format_string("{0}", 1) == FormatStringError::unterminated_open_brace);
static_assert(detail::check_format_string("{:>8}", 1) == FormatStringError::unterminated_open_brace);
static_assert(detail::check_format_string("}", 0) == FormatStringError::stray_close_brace);
static_assert(detail::check_format_string("a}b", 0) == FormatStringError::stray_close_brace);
static_assert(detail::check_format_string("{}", 0) == FormatStringError::argument_count_mismatch);
static_assert(detail::check_format_string("{}", 2) == FormatStringError::argument_count_mismatch);

// The public type runs that validation in its consteval constructor.
static_assert(FormatString<int>{"value={}"}.view() == "value={}");

} // namespace

TEST_CASE("formats string-like arguments") {
    const std::string owned = "owned";
    const std::string_view viewed = "viewed";
    CHECK(StringFormatter::format("{} {} {}", "literal", owned, viewed) == "literal owned viewed");
}

TEST_CASE("formats chars and bools") {
    CHECK(StringFormatter::format("{}{}{}", 'a', 'b', 'c') == "abc");
    CHECK(StringFormatter::format("yes={} no={}", true, false) == "yes=true no=false");
}

TEST_CASE("formats integers across widths and signs") {
    CHECK(StringFormatter::format("{} {} {}", 0, -7, 42U) == "0 -7 42");
    CHECK(StringFormatter::format("{}", INT64_C(9223372036854775807)) == "9223372036854775807");
    CHECK(StringFormatter::format("{}", UINT64_C(18446744073709551615)) == "18446744073709551615");
}

TEST_CASE("formats floating-point values") {
    // Only values whose rendering agrees between std::to_chars and the max_digits10 fallback
    // (exactly representable, trailing zeros trimmed by both paths) — see ADR-0012.
    CHECK(StringFormatter::format("{}", 1.5) == "1.5");
    CHECK(StringFormatter::format("{}", -2.25) == "-2.25");
    CHECK(StringFormatter::format("{}", 0.5F) == "0.5");
    CHECK(StringFormatter::format("{}", 0.0) == "0");
}

TEST_CASE("a format string without placeholders passes through") {
    CHECK(StringFormatter::format("plain text").empty() == false);
    CHECK(StringFormatter::format("plain text") == "plain text");
    CHECK(StringFormatter::format("").empty());
}

TEST_CASE("brace escapes render literal braces") {
    CHECK(StringFormatter::format("{{}}") == "{}");
    CHECK(StringFormatter::format("a{{b}}c") == "a{b}c");
    CHECK(StringFormatter::format("{{{}}}", 5) == "{5}");
}

TEST_CASE("placeholders work leading, trailing, and adjacent") {
    CHECK(StringFormatter::format("{} end", 1) == "1 end");
    CHECK(StringFormatter::format("start {}", 2) == "start 2");
    CHECK(StringFormatter::format("{}{}", 3, 4) == "34");
    CHECK(StringFormatter::format("x={}, y={}, z={}", 1, 2, 3) == "x=1, y=2, z=3");
}

TEST_CASE("format_to appends into an existing builder and chains") {
    StringBuilder builder;
    builder.append("log: ");
    StringBuilder &same = StringFormatter::format_to(builder, "code={} msg={}", 404, "missing");
    CHECK(&same == &builder); // returns the caller's builder
    CHECK(builder.view() == "log: code=404 msg=missing");

    StringFormatter::format_to(builder, " (attempt {})", 2);
    CHECK(builder.view() == "log: code=404 msg=missing (attempt 2)");
}
