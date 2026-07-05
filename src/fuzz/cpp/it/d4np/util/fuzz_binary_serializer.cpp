// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// libFuzzer harness for BinaryDeserializer (component #18), the binary untrusted-input boundary
// (ADR-0031). Over arbitrary bytes it reads a mixed sequence of scalars and length-prefixed
// read_bytes, driven by a selector byte, until a read runs off the end. The asserted invariant is
// that no buffer crashes, invokes UB (ASan/UBSan), or throws: a short/oversized read must return
// std::nullopt and never read out of bounds (ADR-0024, ADR-0029). Builds as a libFuzzer target or
// a standalone replay tool.
#include <cstddef>
#include <cstdint>
#include <span>

#include <it/d4np/util/binary_serializer.hpp>

#include "fuzz_standalone.hpp"

namespace {

namespace u = it::d4np::util;

void fuzz_one(std::span<const std::byte> bytes) {
    u::BinaryDeserializer in{bytes};
    bool progressing = true;
    while (progressing && in.remaining() > 0) {
        const auto selector = in.read<std::uint8_t>();
        if (!selector) {
            break;
        }
        switch (*selector % 6u) {
        case 0:
            progressing = in.read<std::uint16_t>().has_value();
            break;
        case 1:
            progressing = in.read<std::uint32_t>().has_value();
            break;
        case 2:
            progressing = in.read<std::uint64_t>().has_value();
            break;
        case 3:
            progressing = in.read<float>().has_value();
            break;
        case 4:
            progressing = in.read<double>().has_value();
            break;
        default: {
            const auto length = in.read<std::uint8_t>();
            progressing = length.has_value() && in.read_bytes(*length).has_value();
            break;
        }
        }
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const std::span<const std::uint8_t> bytes{data, size};
    fuzz_one(std::as_bytes(bytes));
    return 0;
}
