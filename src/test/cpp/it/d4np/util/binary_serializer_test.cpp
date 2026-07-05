// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for BinarySerializer / BinaryDeserializer (component #18, roadmap 9.2). Exercise
// scalar round-trips in both byte orders, the explicit wire layout (a known value produces a
// known byte sequence), float/double fidelity, write_bytes/read_bytes (zero-copy views),
// mixed-type streams, and the value-or-error boundary — writer overflow latches and reader
// truncation yields nullopt without moving the cursor. A constexpr round-trip is checked with
// static_assert to prove the codec works at compile time.
#include <doctest/doctest.h>

#include <it/d4np/util/binary_serializer.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace {

using it::d4np::util::BinaryDeserializer;
using it::d4np::util::BinarySerializer;

constexpr std::byte operator""_b(unsigned long long v) { return static_cast<std::byte>(v); }

// A full-buffer round-trip performed entirely at compile time, proving every operation is
// constexpr and order-correct. Returns true iff every write fit and every read decoded the
// value that was written (whole-optional comparison — no unchecked dereference).
constexpr bool constexpr_roundtrip_ok() {
    std::array<std::byte, 8> buf{};
    BinarySerializer out{buf, std::endian::big};
    if (!out.write<std::uint32_t>(0x01020304) || !out.write<std::uint16_t>(0x0506)) {
        return false;
    }
    BinaryDeserializer in{buf, std::endian::big};
    return in.read<std::uint32_t>() == std::optional<std::uint32_t>{0x01020304} &&
           in.read<std::uint16_t>() == std::optional<std::uint16_t>{0x0506};
}
static_assert(constexpr_roundtrip_ok());

} // namespace

TEST_CASE("scalar round-trip preserves value in both byte orders") {
    for (const std::endian order : {std::endian::little, std::endian::big}) {
        std::array<std::byte, 32> buf{};
        BinarySerializer out{buf, order};
        CHECK(out.write<std::uint8_t>(0x7F));
        CHECK(out.write<std::int16_t>(-1234));
        CHECK(out.write<std::uint32_t>(0xDEADBEEF));
        CHECK(out.write<std::uint64_t>(0x0102030405060708ULL));
        CHECK(out.write<bool>(true));

        BinaryDeserializer in{buf, order};
        CHECK(in.read<std::uint8_t>() == std::optional<std::uint8_t>{0x7F});
        CHECK(in.read<std::int16_t>() == std::optional<std::int16_t>{-1234});
        CHECK(in.read<std::uint32_t>() == std::optional<std::uint32_t>{0xDEADBEEF});
        CHECK(in.read<std::uint64_t>() == std::optional<std::uint64_t>{0x0102030405060708ULL});
        CHECK(in.read<bool>() == std::optional<bool>{true});
        CHECK(in.exhausted() == false); // buffer larger than payload
    }
}

TEST_CASE("wire layout matches the requested endianness byte-for-byte") {
    SUBCASE("big-endian is most-significant-byte first") {
        std::array<std::byte, 4> buf{};
        BinarySerializer out{buf, std::endian::big};
        REQUIRE(out.write<std::uint32_t>(0x01020304));
        CHECK(out.data()[0] == 0x01_b);
        CHECK(out.data()[1] == 0x02_b);
        CHECK(out.data()[2] == 0x03_b);
        CHECK(out.data()[3] == 0x04_b);
    }
    SUBCASE("little-endian is least-significant-byte first") {
        std::array<std::byte, 4> buf{};
        BinarySerializer out{buf, std::endian::little};
        REQUIRE(out.write<std::uint32_t>(0x01020304));
        CHECK(out.data()[0] == 0x04_b);
        CHECK(out.data()[1] == 0x03_b);
        CHECK(out.data()[2] == 0x02_b);
        CHECK(out.data()[3] == 0x01_b);
    }
}

TEST_CASE("floating-point values survive a round-trip") {
    std::array<std::byte, 16> buf{};
    BinarySerializer out{buf, std::endian::little};
    CHECK(out.write<float>(3.14159F));
    CHECK(out.write<double>(-2.718281828459045));

    BinaryDeserializer in{buf, std::endian::little};
    CHECK(in.read<float>() == std::optional<float>{3.14159F});
    CHECK(in.read<double>() == std::optional<double>{-2.718281828459045});
}

TEST_CASE("write_bytes and read_bytes carry a raw block verbatim as a zero-copy view") {
    const std::array<std::byte, 5> payload{0xAA_b, 0xBB_b, 0xCC_b, 0xDD_b, 0xEE_b};
    std::array<std::byte, 8> buf{};
    BinarySerializer out{buf};
    CHECK(out.write<std::uint8_t>(0x01));
    CHECK(out.write_bytes(payload));
    CHECK(out.bytes_written() == 6);

    BinaryDeserializer in{buf};
    CHECK(in.read<std::uint8_t>() == std::optional<std::uint8_t>{0x01});
    const std::optional<std::span<const std::byte>> view = in.read_bytes(payload.size());
    CHECK((view && std::ranges::equal(*view, payload)));
    // Zero-copy: the view aliases the source buffer (points at buf[1]), not a copy.
    CHECK((view && view->data() == &buf[1]));
}

TEST_CASE("writer reports overflow without partial writes and latches the flag") {
    std::array<std::byte, 4> buf{};
    BinarySerializer out{buf};
    CHECK(out.write<std::uint16_t>(0x1122)); // 2 of 4 bytes
    CHECK(out.overflowed() == false);

    CHECK_FALSE(out.write<std::uint32_t>(0x33445566)); // needs 4, only 2 left
    CHECK(out.overflowed() == true);
    CHECK(out.bytes_written() == 2); // nothing partial was written

    // A subsequent write that *would* fit is still refused? No — overflow is sticky as a flag,
    // but a fitting write still succeeds; the flag records that a rejection happened.
    CHECK(out.write<std::uint16_t>(0x7788)); // fills the last 2 bytes
    CHECK(out.bytes_written() == 4);
    CHECK(out.overflowed() == true); // flag stays set
    CHECK(out.remaining() == 0);
}

TEST_CASE("reader returns nullopt on truncation and leaves the cursor unmoved") {
    std::array<std::byte, 3> buf{0x01_b, 0x02_b, 0x03_b};
    BinaryDeserializer in{buf};
    CHECK(in.read<std::uint16_t>() == std::optional<std::uint16_t>{std::endian::native == std::endian::little
                                                                       ? std::uint16_t{0x0201}
                                                                       : std::uint16_t{0x0102}});
    CHECK(in.bytes_read() == 2);

    CHECK(in.read<std::uint32_t>() == std::nullopt); // only 1 byte left
    CHECK(in.bytes_read() == 2);                     // cursor did not move
    CHECK(in.read_bytes(2) == std::nullopt);         // still only 1 byte
    CHECK(in.bytes_read() == 2);

    CHECK(in.read<std::uint8_t>() == std::optional<std::uint8_t>{0x03});
    CHECK(in.exhausted());
}
