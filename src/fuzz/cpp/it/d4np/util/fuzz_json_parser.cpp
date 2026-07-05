// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// libFuzzer harness for JsonParser (component #23), the JSON untrusted-input boundary
// (ADR-0031). It walks next() to completion and decode_string()s every string/key slice — the
// escape / \uXXXX / UTF-16 surrogate decoder is the sharpest edge. The asserted invariant is
// that no input crashes, invokes UB (ASan/UBSan), or throws: malformed input must latch a sticky
// `error` event, never unwind (ADR-0022, ADR-0029). Builds either as a libFuzzer target or, by
// default, as a standalone replay tool over input files (see fuzz_standalone.hpp).
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <it/d4np/util/json_parser.hpp>

#include "fuzz_standalone.hpp"

namespace {

namespace u = it::d4np::util;

void fuzz_one(std::string_view input) {
    u::JsonParser parser{input};
    std::string decoded;
    for (auto event = parser.next(); event.type != u::JsonToken::end; event = parser.next()) {
        if (event.type == u::JsonToken::error) {
            break;
        }
        if (event.type == u::JsonToken::key || event.type == u::JsonToken::string) {
            decoded.clear();
            static_cast<void>(u::JsonParser<>::decode_string(event.text, decoded));
        }
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const std::span<const std::uint8_t> bytes{data, size};
    const std::string text(bytes.begin(), bytes.end());
    fuzz_one(text);
    return 0;
}
