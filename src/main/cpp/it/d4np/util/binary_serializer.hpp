// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// BinarySerializer / BinaryDeserializer (component #18): an endianness-aware, zero-allocation
// binary codec over caller-owned byte buffers. Header-only — every operation is pure byte
// manipulation (std::bit_cast + a conditional byte reverse), so no OS API and no compiled tier
// are involved (unlike FileStream). The wire byte order is chosen at construction (little by
// default); when it matches the host order the bytes are copied verbatim, otherwise they are
// reversed. The value-or-error boundary follows the library convention: running past the buffer
// is ordinary runtime input, never a throw — the writer returns false and latches overflowed(),
// the reader returns std::nullopt. See ADR-0024.
#ifndef IT_D4NP_UTIL_BINARY_SERIALIZER_HPP
#define IT_D4NP_UTIL_BINARY_SERIALIZER_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>

namespace it::d4np::util {

// The little/big byte-reverse used for cross-order I/O assumes the host is one or the other;
// mixed-endian (PDP) platforms are out of scope and would need per-field permutation.
static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big,
              "BinarySerializer supports only little- or big-endian hosts (no mixed-endian).");

/// The scalar types the codec serializes directly: the fundamental integrals (including `bool`
/// and the character types) and the floating-point types. Aggregates are serialized field by
/// field by the caller; raw spans go through `write_bytes` / `read_bytes`.
template <class T>
concept TriviallySerializable = std::integral<T> || std::floating_point<T>;

/// Endianness-aware writer over a caller-owned, fixed-size byte buffer (component #18).
///
/// Construct over a `std::span<std::byte>` and a target wire order (little-endian by default);
/// each `write` lays a scalar down in that order, `write_bytes` copies a raw block. The buffer
/// is never grown or owned — the caller sizes it. Writing more than fits leaves the buffer
/// untouched from that point, returns `false`, and latches `overflowed()`.
///
/// ```cpp
/// std::array<std::byte, 16> buf{};
/// it::d4np::util::BinarySerializer out{buf};
/// out.write<std::uint32_t>(0xDEADBEEF);
/// out.write<double>(3.5);
/// const std::span<const std::byte> wire = out.data(); // the bytes actually written
/// ```
///
/// **Error model.** Overflow is reported, never thrown: a `write` that would exceed the buffer
/// writes nothing, returns `false`, and sets `overflowed()` (which stays set). There is no
/// programmer-error throw path — the type has no misuse mode analogous to a wrong-direction
/// stream.
///
/// @note Not thread-safe: drive one serializer from one thread. All operations are `constexpr`,
///       so a buffer backed by a `std::array` can be filled at compile time.
class BinarySerializer {
  public:
    /// Constructs a writer over `buffer`, emitting scalars in `order` (default little-endian).
    constexpr explicit BinarySerializer(std::span<std::byte> buffer, std::endian order = std::endian::little) noexcept
        : buffer_(buffer), order_(order) {}

    /// Writes `value` as `sizeof(T)` bytes in the configured order. Returns `false` without
    /// writing anything if it would not fit (and latches `overflowed()`).
    template <TriviallySerializable T> [[nodiscard]] constexpr bool write(T value) noexcept {
        if constexpr (std::floating_point<T>) {
            static_assert(std::numeric_limits<T>::is_iec559,
                          "portable float serialization requires an IEC 559 / IEEE 754 layout");
        }
        return write_raw(std::bit_cast<std::array<std::byte, sizeof(T)>>(value));
    }

    /// Writes `src` verbatim (no byte-order transform — raw bytes are order-agnostic). Returns
    /// `false` without writing anything if it would not fit (and latches `overflowed()`).
    [[nodiscard]] constexpr bool write_bytes(std::span<const std::byte> src) noexcept {
        if (src.size() > remaining()) {
            overflowed_ = true;
            return false;
        }
        std::ranges::copy(src, buffer_.subspan(pos_).begin());
        pos_ += src.size();
        return true;
    }

    /// The bytes written so far, in order.
    [[nodiscard]] constexpr std::span<const std::byte> data() const noexcept { return buffer_.first(pos_); }

    /// Number of bytes written so far.
    [[nodiscard]] constexpr std::size_t bytes_written() const noexcept { return pos_; }

    /// Total size of the backing buffer.
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return buffer_.size(); }

    /// Bytes still available to write.
    [[nodiscard]] constexpr std::size_t remaining() const noexcept { return buffer_.size() - pos_; }

    /// Whether any write has been rejected for lack of room (sticky once set).
    [[nodiscard]] constexpr bool overflowed() const noexcept { return overflowed_; }

    /// The wire byte order this serializer emits.
    [[nodiscard]] constexpr std::endian order() const noexcept { return order_; }

  private:
    template <std::size_t N> constexpr bool write_raw(std::array<std::byte, N> raw) noexcept {
        if (N > remaining()) {
            overflowed_ = true;
            return false;
        }
        if (order_ != std::endian::native) {
            std::ranges::reverse(raw);
        }
        std::ranges::copy(raw, buffer_.subspan(pos_).begin());
        pos_ += N;
        return true;
    }

    std::span<std::byte> buffer_;
    std::size_t pos_ = 0;
    std::endian order_;
    bool overflowed_ = false;
};

/// Endianness-aware reader that mirrors `BinarySerializer` over a read-only byte buffer.
///
/// Construct over the bytes and the same order they were written in; each `read<T>()` consumes
/// `sizeof(T)` bytes and returns the decoded value, or `std::nullopt` if fewer than that remain.
/// `read_bytes(n)` returns a zero-copy view into the source buffer (its lifetime is the caller's
/// buffer, not the reader).
///
/// ```cpp
/// it::d4np::util::BinaryDeserializer in{wire};
/// const std::optional<std::uint32_t> magic = in.read<std::uint32_t>();
/// const std::optional<double> ratio = in.read<double>();
/// ```
///
/// **Error model.** A read past the end is reported, never thrown: it returns `std::nullopt` and
/// leaves the cursor unmoved, so the caller can branch on a truncated buffer.
///
/// @note Not thread-safe. All operations are `constexpr`. The `std::span` returned by
///       `read_bytes` aliases the source buffer and must not outlive it.
class BinaryDeserializer {
  public:
    /// Constructs a reader over `buffer`, decoding scalars from `order` (default little-endian).
    constexpr explicit BinaryDeserializer(std::span<const std::byte> buffer,
                                          std::endian order = std::endian::little) noexcept
        : buffer_(buffer), order_(order) {}

    /// Reads `sizeof(T)` bytes as a `T` in the configured order, advancing the cursor. Returns
    /// `std::nullopt` (cursor unmoved) if fewer than `sizeof(T)` bytes remain.
    template <TriviallySerializable T> [[nodiscard]] constexpr std::optional<T> read() noexcept {
        if constexpr (std::floating_point<T>) {
            static_assert(std::numeric_limits<T>::is_iec559,
                          "portable float serialization requires an IEC 559 / IEEE 754 layout");
        }
        if (sizeof(T) > remaining()) {
            return std::nullopt;
        }
        std::array<std::byte, sizeof(T)> raw{};
        std::ranges::copy(buffer_.subspan(pos_, sizeof(T)), raw.begin());
        if (order_ != std::endian::native) {
            std::ranges::reverse(raw);
        }
        pos_ += sizeof(T);
        return std::bit_cast<T>(raw);
    }

    /// Returns a zero-copy view of the next `n` bytes and advances the cursor, or `std::nullopt`
    /// (cursor unmoved) if fewer than `n` bytes remain. The view aliases the source buffer.
    [[nodiscard]] constexpr std::optional<std::span<const std::byte>> read_bytes(std::size_t n) noexcept {
        if (n > remaining()) {
            return std::nullopt;
        }
        const std::span<const std::byte> view = buffer_.subspan(pos_, n);
        pos_ += n;
        return view;
    }

    /// Number of bytes consumed so far.
    [[nodiscard]] constexpr std::size_t bytes_read() const noexcept { return pos_; }

    /// Bytes still available to read.
    [[nodiscard]] constexpr std::size_t remaining() const noexcept { return buffer_.size() - pos_; }

    /// Whether every byte has been consumed.
    [[nodiscard]] constexpr bool exhausted() const noexcept { return pos_ == buffer_.size(); }

    /// The wire byte order this reader decodes from.
    [[nodiscard]] constexpr std::endian order() const noexcept { return order_; }

  private:
    std::span<const std::byte> buffer_;
    std::size_t pos_ = 0;
    std::endian order_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_BINARY_SERIALIZER_HPP
