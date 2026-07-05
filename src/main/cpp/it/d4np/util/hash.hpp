// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Constexpr hash algorithms (component #24): FNV-1a (32/64-bit), MurmurHash3 (x86 32-bit),
// and SHA-256. Every function is usable at compile time (e.g. to hash a string literal in a
// switch or a static lookup table) and at run time. None of these depend on anything beyond
// the standard library.
//
// SCOPE: all three are NON-CRYPTOGRAPHIC integrity/checksum utilities (ADR-0031). SHA-256 is a
// standards-conformant digest for content-addressing, deduplication, corruption detection, and
// interop/test vectors — it is NOT for passwords, MACs/HMAC, signatures, or any secret-dependent
// decision. The constexpr, byte-oriented implementation is not constant-time and is not hardened
// against side channels. FNV-1a and MurmurHash3 are additionally non-collision-resistant.
#ifndef IT_D4NP_UTIL_HASH_HPP
#define IT_D4NP_UTIL_HASH_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace it::d4np::util {

/// 64-bit Fowler–Noll–Vo 1a hash of `data`.
[[nodiscard]] constexpr std::uint64_t fnv1a_64(std::string_view data) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char ch : data) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/// 32-bit Fowler–Noll–Vo 1a hash of `data`.
[[nodiscard]] constexpr std::uint32_t fnv1a_32(std::string_view data) noexcept {
    std::uint32_t hash = 0x811c9dc5u;
    for (const char ch : data) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 0x01000193u;
    }
    return hash;
}

/// MurmurHash3 x86 32-bit hash of `data` with an optional `seed`.
[[nodiscard]] constexpr std::uint32_t murmur3_x86_32(std::string_view data, std::uint32_t seed = 0) noexcept {
    constexpr std::uint32_t c1 = 0xcc9e2d51u;
    constexpr std::uint32_t c2 = 0x1b873593u;
    const std::size_t len = data.size();
    const std::size_t blocks = len / 4;
    std::uint32_t h1 = seed;

    const auto byte = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[i])); };

    for (std::size_t i = 0; i < blocks; ++i) {
        const std::size_t j = i * 4;
        std::uint32_t k1 = byte(j) | (byte(j + 1) << 8) | (byte(j + 2) << 16) | (byte(j + 3) << 24);
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> 19);
        h1 = h1 * 5u + 0xe6546b64u;
    }

    std::uint32_t k1 = 0;
    const std::size_t tail = blocks * 4;
    switch (len & 3u) {
    case 3:
        k1 ^= byte(tail + 2) << 16;
        [[fallthrough]];
    case 2:
        k1 ^= byte(tail + 1) << 8;
        [[fallthrough]];
    case 1:
        k1 ^= byte(tail);
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        h1 ^= k1;
        break;
    default:
        break;
    }

    h1 ^= static_cast<std::uint32_t>(len);
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6bu;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35u;
    h1 ^= h1 >> 16;
    return h1;
}

namespace detail {

inline constexpr std::array<std::uint32_t, 64> sha256_k = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept { return (x >> n) | (x << (32u - n)); }

} // namespace detail

/// SHA-256 digest of `data`, returned as 32 raw bytes (big-endian per the standard).
///
/// @warning Non-cryptographic scope (ADR-0031): an integrity/checksum digest, not a security
///          primitive. Not constant-time; do not use for passwords, MACs/HMAC, signatures, or any
///          secret-dependent decision.
[[nodiscard]] constexpr std::array<std::uint8_t, 32> sha256(std::string_view data) noexcept {
    std::array<std::uint32_t, 8> h = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    const std::uint64_t len = data.size();
    const std::uint64_t bitlen = len * 8u;
    std::uint64_t total = len + 1u; // message + the 0x80 terminator byte
    while (total % 64u != 56u) {
        ++total;
    }
    total += 8u; // 64-bit big-endian length
    const std::uint64_t nblocks = total / 64u;

    // Returns the i-th byte of the padded message without materializing it.
    const auto padded = [&](std::uint64_t i) -> std::uint32_t {
        if (i < len) {
            return static_cast<std::uint8_t>(data[static_cast<std::size_t>(i)]);
        }
        if (i == len) {
            return 0x80u;
        }
        if (i >= total - 8u) {
            const std::uint64_t shift = (total - 1u - i) * 8u;
            return static_cast<std::uint32_t>((bitlen >> shift) & 0xffu);
        }
        return 0u;
    };

    for (std::uint64_t blk = 0; blk < nblocks; ++blk) {
        std::array<std::uint32_t, 64> w = {};
        for (std::uint64_t t = 0; t < 16u; ++t) {
            const std::uint64_t base = blk * 64u + t * 4u;
            w[static_cast<std::size_t>(t)] =
                (padded(base) << 24) | (padded(base + 1u) << 16) | (padded(base + 2u) << 8) | padded(base + 3u);
        }
        for (std::size_t t = 16; t < 64; ++t) {
            const std::uint32_t s0 = detail::rotr(w[t - 15], 7) ^ detail::rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
            const std::uint32_t s1 = detail::rotr(w[t - 2], 17) ^ detail::rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }

        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        std::uint32_t f = h[5];
        std::uint32_t g = h[6];
        std::uint32_t hh = h[7];

        for (std::size_t t = 0; t < 64; ++t) {
            const std::uint32_t s1 = detail::rotr(e, 6) ^ detail::rotr(e, 11) ^ detail::rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = hh + s1 + ch + detail::sha256_k[t] + w[t];
            const std::uint32_t s0 = detail::rotr(a, 2) ^ detail::rotr(a, 13) ^ detail::rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::array<std::uint8_t, 32> digest = {};
    for (std::size_t i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return digest;
}

/// SHA-256 digest of `data` as a 64-character lowercase hex string (no terminator).
[[nodiscard]] constexpr std::array<char, 64> sha256_hex(std::string_view data) noexcept {
    constexpr std::string_view digits = "0123456789abcdef";
    const std::array<std::uint8_t, 32> digest = sha256(data);
    std::array<char, 64> out = {};
    for (std::size_t i = 0; i < 32; ++i) {
        out[i * 2] = digits[(digest[i] >> 4) & 0x0fu];
        out[i * 2 + 1] = digits[digest[i] & 0x0fu];
    }
    return out;
}

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_HASH_HPP
