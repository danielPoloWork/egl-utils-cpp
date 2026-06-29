// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for it::d4np::util hash algorithms (component #24). All algorithms are constexpr, so
// the primary proof is a static_assert battery against published/reference vectors (a
// regression fails the build); a TEST_CASE mirrors it at run time.
#include <doctest/doctest.h>

#include <it/d4np/util/hash.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace {

namespace u = it::d4np::util;

// --- FNV-1a 64 -----------------------------------------------------------------------------
static_assert(u::fnv1a_64("") == 0xcbf29ce484222325ULL);
static_assert(u::fnv1a_64("a") == 0xaf63dc4c8601ec8cULL);
static_assert(u::fnv1a_64("abc") == 0xe71fa2190541574bULL);
static_assert(u::fnv1a_64("foobar") == 0x85944171f73967e8ULL);

// --- FNV-1a 32 -----------------------------------------------------------------------------
static_assert(u::fnv1a_32("") == 0x811c9dc5u);
static_assert(u::fnv1a_32("a") == 0xe40c292cu);
static_assert(u::fnv1a_32("foobar") == 0xbf9cf968u);

// --- MurmurHash3 x86_32 --------------------------------------------------------------------
static_assert(u::murmur3_x86_32("") == 0x00000000u);
static_assert(u::murmur3_x86_32("", 1) == 0x514e28b7u);
static_assert(u::murmur3_x86_32("a") == 0x3c2569b2u);
static_assert(u::murmur3_x86_32("abc") == 0xb3dd93fau);
static_assert(u::murmur3_x86_32("hello") == 0x248bfa47u);

// --- SHA-256 -------------------------------------------------------------------------------
constexpr bool hex_is(std::array<char, 64> got, std::string_view want) {
    if (want.size() != got.size()) {
        return false;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (got[i] != want[i]) {
            return false;
        }
    }
    return true;
}

static_assert(hex_is(u::sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
static_assert(hex_is(u::sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

} // namespace

TEST_CASE("non-cryptographic hashes match reference vectors") {
    CHECK(u::fnv1a_64("abc") == 0xe71fa2190541574bULL);
    CHECK(u::fnv1a_32("a") == 0xe40c292cu);
    CHECK(u::murmur3_x86_32("hello") == 0x248bfa47u);
}

TEST_CASE("SHA-256 matches the canonical 'abc' digest") {
    CHECK(hex_is(u::sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}
