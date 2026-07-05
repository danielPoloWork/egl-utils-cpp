// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// libFuzzer harness for CliParser (component #22), the argv untrusted-input boundary (ADR-0031).
// It splits the input buffer into NUL-separated argv tokens, registers a representative mix of
// flags / value options / positionals bound to locals, and runs parse(). The asserted invariant
// is that no argv crashes, invokes UB (ASan/UBSan), or throws: parse() must always return a
// ParseResult (ADR-0021, ADR-0029). Builds as a libFuzzer target or a standalone replay tool.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <it/d4np/util/cli_parser.hpp>

#include "fuzz_standalone.hpp"

namespace {

namespace u = it::d4np::util;

void fuzz_one(std::string_view input) {
    // Split on NUL bytes into argv-like tokens; keep the storage alive for the string_views.
    std::vector<std::string> storage;
    std::string current;
    for (const char ch : input) {
        if (ch == '\0') {
            storage.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    storage.push_back(current);

    std::vector<std::string_view> args;
    args.reserve(storage.size());
    for (const std::string &token : storage) {
        args.emplace_back(token);
    }

    bool verbose = false;
    int count = 1;
    double ratio = 0.0;
    std::string name;
    std::string input_path;

    u::CliParser parser{"fuzz", "CliParser fuzz harness."};
    parser.add_flag("verbose", 'v', "verbose flag", verbose)
        .add_option("count", 'c', "count option", count, false, "N")
        .add_option("ratio", 'r', "ratio option", ratio, false, "R")
        .add_option("name", 'n', "name option", name, false, "NAME")
        .add_positional("input", "input path", input_path);

    static_cast<void>(parser.parse(std::span<const std::string_view>{args}));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const std::span<const std::uint8_t> bytes{data, size};
    const std::string text(bytes.begin(), bytes.end());
    fuzz_one(text);
    return 0;
}
