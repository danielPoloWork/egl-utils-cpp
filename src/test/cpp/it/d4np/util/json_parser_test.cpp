// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for JsonParser (component #23, roadmap 8.2). Exercise the full event stream (scalars,
// objects, arrays, nesting), the opt-in conversion helpers (to_number, to_bool,
// decode_string incl. \u and surrogate pairs), and the value-or-error boundary — every
// malformed-input class yields a sticky `error` event with a byte offset, never a throw.
#include <doctest/doctest.h>

#include <it/d4np/util/json_parser.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using it::d4np::util::JsonEvent;
using it::d4np::util::JsonParser;
using it::d4np::util::JsonToken;

// Drains a parser into a flat list of (type, text) pairs, stopping at end or error (inclusive).
template <std::size_t MaxDepth = 64>
std::vector<std::pair<JsonToken, std::string>> drain(JsonParser<MaxDepth> &parser) {
    std::vector<std::pair<JsonToken, std::string>> events;
    for (;;) {
        const JsonEvent ev = parser.next();
        events.emplace_back(ev.type, std::string{ev.text});
        if (ev.type == JsonToken::end || ev.type == JsonToken::error) {
            return events;
        }
    }
}

// The token types of a successful parse (drops text and the trailing `end`).
std::vector<JsonToken> token_stream(std::string_view json) {
    JsonParser<> parser{json};
    std::vector<JsonToken> types;
    for (JsonEvent ev = parser.next(); ev.type != JsonToken::end; ev = parser.next()) {
        REQUIRE(ev.type != JsonToken::error);
        types.push_back(ev.type);
    }
    return types;
}

} // namespace

TEST_CASE("top-level scalars parse to a single event then end") {
    SUBCASE("number") { CHECK(token_stream("42") == std::vector{JsonToken::number}); }
    SUBCASE("string") { CHECK(token_stream(R"("hi")") == std::vector{JsonToken::string}); }
    SUBCASE("true") { CHECK(token_stream("true") == std::vector{JsonToken::boolean}); }
    SUBCASE("false") { CHECK(token_stream("false") == std::vector{JsonToken::boolean}); }
    SUBCASE("null") { CHECK(token_stream("null") == std::vector{JsonToken::null_value}); }
    SUBCASE("surrounding whitespace is skipped") {
        CHECK(token_stream("  \n\t 42 \r\n") == std::vector{JsonToken::number});
    }
}

TEST_CASE("an object yields key/value events in order") {
    JsonParser<> parser{R"({"a":1,"b":true})"};
    const auto events = drain(parser);
    const std::vector<std::pair<JsonToken, std::string>> expected{
        {JsonToken::begin_object, ""}, {JsonToken::key, "a"},       {JsonToken::number, "1"}, {JsonToken::key, "b"},
        {JsonToken::boolean, "true"},  {JsonToken::end_object, ""}, {JsonToken::end, ""}};
    CHECK(events == expected);
}

TEST_CASE("nested containers walk pre-order") {
    const auto types = token_stream(R"({"b":[true,null]})");
    const std::vector<JsonToken> expected{JsonToken::begin_object, JsonToken::key,        JsonToken::begin_array,
                                          JsonToken::boolean,      JsonToken::null_value, JsonToken::end_array,
                                          JsonToken::end_object};
    CHECK(types == expected);
}

TEST_CASE("empty containers are a begin/end pair") {
    CHECK(token_stream("{}") == std::vector{JsonToken::begin_object, JsonToken::end_object});
    CHECK(token_stream("[]") == std::vector{JsonToken::begin_array, JsonToken::end_array});
    CHECK(token_stream("[{},[]]") == std::vector{JsonToken::begin_array, JsonToken::begin_object, JsonToken::end_object,
                                                 JsonToken::begin_array, JsonToken::end_array, JsonToken::end_array});
}

TEST_CASE("number text is borrowed verbatim and converts on demand") {
    JsonParser<> parser{"[-3, 2.5, 1e3, 42]"};
    CHECK(parser.next().type == JsonToken::begin_array);

    const JsonEvent neg = parser.next();
    CHECK(neg.text == "-3");
    CHECK(JsonParser<>::to_number<int>(neg.text) == -3);

    const JsonEvent frac = parser.next();
    CHECK(frac.text == "2.5");
    CHECK(JsonParser<>::to_number<double>(frac.text) == doctest::Approx(2.5));
    CHECK(JsonParser<>::to_number<int>(frac.text) == std::nullopt); // fractional into int fails

    const JsonEvent exp = parser.next();
    CHECK(JsonParser<>::to_number<double>(exp.text) == doctest::Approx(1000.0));

    const JsonEvent big = parser.next();
    CHECK(big.text == "42");
}

TEST_CASE("to_number reports overflow as nullopt, not a wrong value") {
    CHECK(JsonParser<>::to_number<std::int32_t>("99999999999999999999") == std::nullopt);
    CHECK(JsonParser<>::to_number<std::uint8_t>("256") == std::nullopt);
    CHECK(JsonParser<>::to_number<std::uint8_t>("255") == std::uint8_t{255});
}

TEST_CASE("to_bool maps the boolean literal") {
    CHECK(JsonParser<>::to_bool("true"));
    CHECK_FALSE(JsonParser<>::to_bool("false"));
}

TEST_CASE("string tokens are the raw inner slice; decode_string applies escapes") {
    SUBCASE("no escapes: raw slice, borrowed") {
        JsonParser<> parser{R"("plain")"};
        CHECK(parser.next().text == "plain");
    }
    SUBCASE("escapes are left raw in the token and decoded on demand") {
        JsonParser<> parser{R"("a\tb\n\"c\"")"};
        const JsonEvent ev = parser.next();
        CHECK(ev.text == R"(a\tb\n\"c\")"); // raw, escapes intact
        CHECK(JsonParser<>::decode_string(ev.text) == std::string{"a\tb\n\"c\""});
    }
    SUBCASE("\\u BMP escape becomes UTF-8") {
        // Build the escape text with a '\x5C' backslash char, so the C++ source carries no
        // universal-character-name of its own. U+20AC EURO SIGN -> E2 82 AC.
        std::string euro;
        euro += '\x5C';
        euro += "u20AC";
        CHECK(JsonParser<>::decode_string(euro) == std::string{"\xE2\x82\xAC"});
    }
    SUBCASE("surrogate pair becomes one UTF-8 code point") {
        // U+1F600 GRINNING FACE -> F0 9F 98 80.
        std::string grin;
        grin += '\x5C';
        grin += "uD83D";
        grin += '\x5C';
        grin += "uDE00";
        CHECK(JsonParser<>::decode_string(grin) == std::string{"\xF0\x9F\x98\x80"});
    }
    SUBCASE("malformed escapes fail decoding") {
        CHECK(JsonParser<>::decode_string(R"(\x)") == std::nullopt);      // unknown escape
        CHECK(JsonParser<>::decode_string(R"(\u12)") == std::nullopt);    // short \u
        CHECK(JsonParser<>::decode_string(R"(\uD83D)") == std::nullopt);  // lone high surrogate
        CHECK(JsonParser<>::decode_string(R"(\uDE00)") == std::nullopt);  // lone low surrogate
        CHECK(JsonParser<>::decode_string("trailing\\") == std::nullopt); // trailing backslash
    }
}

TEST_CASE("keys may contain escapes and decode independently") {
    JsonParser<> parser{R"({"a\nb":1})"};
    CHECK(parser.next().type == JsonToken::begin_object);
    const JsonEvent key = parser.next();
    CHECK(key.type == JsonToken::key);
    CHECK(JsonParser<>::decode_string(key.text) == std::string{"a\nb"});
}

TEST_CASE("malformed input latches a sticky error, never throws") {
    auto expect_error = [](std::string_view json) {
        JsonParser<> parser{json};
        JsonEvent ev = parser.next();
        while (ev.type != JsonToken::error && ev.type != JsonToken::end) {
            ev = parser.next();
        }
        CHECK(ev.type == JsonToken::error);
        CHECK(parser.failed());
        CHECK(parser.done());
        CHECK_FALSE(parser.error_message().empty());
        // The error is sticky: further calls keep returning error.
        CHECK(parser.next().type == JsonToken::error);
    };

    SUBCASE("empty input") { expect_error(""); }
    SUBCASE("whitespace only") { expect_error("   "); }
    SUBCASE("trailing characters") { expect_error("42 43"); }
    SUBCASE("trailing comma in object") { expect_error(R"({"a":1,})"); }
    SUBCASE("trailing comma in array") { expect_error("[1,]"); }
    SUBCASE("missing colon") { expect_error(R"({"a" 1})"); }
    SUBCASE("non-string key") { expect_error("{1:2}"); }
    SUBCASE("unterminated string") { expect_error(R"("abc)"); }
    SUBCASE("unterminated object") { expect_error(R"({"a":1)"); }
    SUBCASE("unterminated array") { expect_error("[1,2"); }
    SUBCASE("unescaped control character") { expect_error("\"a\tb\""); }
    SUBCASE("leading zero") { expect_error("01"); }
    SUBCASE("bare minus") { expect_error("-"); }
    SUBCASE("dangling fraction") { expect_error("1."); }
    SUBCASE("dangling exponent") { expect_error("1e"); }
    SUBCASE("leading-dot number") { expect_error(".5"); }
    SUBCASE("unknown literal") { expect_error("nul"); }
    SUBCASE("bare colon") { expect_error(":"); }
}

TEST_CASE("the error offset points at the offending byte") {
    JsonParser<> parser{"[1, 2, x]"};
    JsonEvent ev = parser.next();
    while (ev.type != JsonToken::error && ev.type != JsonToken::end) {
        ev = parser.next();
    }
    REQUIRE(parser.failed());
    CHECK(parser.error_offset() == 7); // index of 'x'
}

TEST_CASE("nesting deeper than MaxDepth is a bounded error, not a crash") {
    JsonParser<2> parser{"[[[1]]]"}; // depth 3 > MaxDepth 2
    CHECK(parser.next().type == JsonToken::begin_array);
    CHECK(parser.next().type == JsonToken::begin_array);
    const JsonEvent third = parser.next();
    CHECK(third.type == JsonToken::error);
    CHECK(parser.error_message() == "maximum nesting depth exceeded");
}

TEST_CASE("exactly MaxDepth nesting is accepted") {
    JsonParser<2> parser{"[[1]]"}; // depth 2 == MaxDepth 2
    CHECK(parser.next().type == JsonToken::begin_array);
    CHECK(parser.next().type == JsonToken::begin_array);
    CHECK(parser.next().type == JsonToken::number);
    CHECK(parser.next().type == JsonToken::end_array);
    CHECK(parser.next().type == JsonToken::end_array);
    CHECK(parser.next().type == JsonToken::end);
    CHECK_FALSE(parser.failed());
}

TEST_CASE("a realistic document round-trips through the event stream") {
    constexpr std::string_view json = R"({
        "name": "egl-util",
        "version": 8,
        "stable": false,
        "tags": ["cpp", "header-only"],
        "meta": {"depth": 2, "note": null}
    })";
    JsonParser<> parser{json};
    std::size_t keys = 0;
    std::size_t values = 0;
    for (JsonEvent ev = parser.next(); ev.type != JsonToken::end; ev = parser.next()) {
        REQUIRE(ev.type != JsonToken::error);
        if (ev.type == JsonToken::key) {
            ++keys;
        }
        if (ev.type == JsonToken::string || ev.type == JsonToken::number || ev.type == JsonToken::boolean ||
            ev.type == JsonToken::null_value) {
            ++values;
        }
    }
    CHECK(keys == 7);   // name, version, stable, tags, meta, depth, note
    CHECK(values == 7); // "egl-util", 8, false, "cpp", "header-only", 2, null
    CHECK_FALSE(parser.failed());
}
