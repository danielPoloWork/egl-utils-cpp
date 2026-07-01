// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// StringBuilder (component #10): a fluent, preallocation-friendly string builder. It owns a
// growable std::string buffer and appends pieces through a chainable interface; integers are
// formatted with std::to_chars (no allocation, locale-independent). Reserve up front to make
// long build sequences allocation-free after the first grow. Header-only.
#ifndef IT_D4NP_UTIL_STRING_BUILDER_HPP
#define IT_D4NP_UTIL_STRING_BUILDER_HPP

#include <array>
#include <charconv>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace it::d4np::util {

namespace detail {

/// Integral types the numeric append accepts — every integer except the character types and
/// bool, which have dedicated (or intentionally omitted) handling.
template <typename T>
concept AppendableInteger =
    std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> && !std::is_same_v<std::remove_cv_t<T>, char> &&
    !std::is_same_v<std::remove_cv_t<T>, wchar_t> && !std::is_same_v<std::remove_cv_t<T>, char8_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char16_t> && !std::is_same_v<std::remove_cv_t<T>, char32_t>;

} // namespace detail

/// Accumulates text into an owned buffer through a fluent, chainable interface.
///
/// Each `append` overload returns `*this`, so calls chain: `builder.append("x=").append(42)`.
/// `operator<<` is a synonym for `append`. Integers are rendered with `std::to_chars`
/// (allocation-free, locale-independent); floating-point formatting is intentionally out of
/// scope here (its `std::to_chars` support is uneven across the toolchain matrix) and belongs to
/// the upcoming `StringFormatter`. Reserve capacity up front to avoid repeated reallocation.
///
/// @note Not thread-safe. `view()` is invalidated by any subsequent append or `clear()`.
class StringBuilder {
  public:
    using size_type = std::string::size_type;

    StringBuilder() = default;
    /// Constructs an empty builder with at least `capacity` bytes reserved.
    explicit StringBuilder(size_type capacity) { buffer_.reserve(capacity); }
    /// Constructs a builder seeded with `initial`.
    explicit StringBuilder(std::string_view initial) : buffer_(initial) {}

    // --- Fluent append ---------------------------------------------------------------------
    StringBuilder &append(std::string_view text) {
        buffer_.append(text);
        return *this;
    }
    StringBuilder &append(char character) {
        buffer_.push_back(character);
        return *this;
    }
    /// Appends the base-10 rendering of an integer.
    template <detail::AppendableInteger T> StringBuilder &append(T value) {
        std::array<char, 24> digits{}; // 20 digits + sign is the widest 64-bit integer
        char *const first = digits.data();
        char *const last = std::next(first, static_cast<std::ptrdiff_t>(digits.size()));
        const std::to_chars_result result = std::to_chars(first, last, value);
        if (result.ec == std::errc{}) {
            buffer_.append(first, static_cast<size_type>(std::distance(first, result.ptr)));
        }
        return *this;
    }

    // --- operator<< synonyms ---------------------------------------------------------------
    StringBuilder &operator<<(std::string_view text) { return append(text); }
    StringBuilder &operator<<(char character) { return append(character); }
    template <detail::AppendableInteger T> StringBuilder &operator<<(T value) { return append(value); }

    // --- Buffer management -----------------------------------------------------------------
    void reserve(size_type capacity) { buffer_.reserve(capacity); }
    void clear() noexcept { buffer_.clear(); }
    [[nodiscard]] size_type size() const noexcept { return buffer_.size(); }
    [[nodiscard]] bool empty() const noexcept { return buffer_.empty(); }
    [[nodiscard]] size_type capacity() const noexcept { return buffer_.capacity(); }

    // --- Extraction ------------------------------------------------------------------------
    /// A view of the current contents. Invalidated by any later append/clear.
    [[nodiscard]] std::string_view view() const noexcept { return buffer_; }
    /// A copy of the current contents.
    [[nodiscard]] const std::string &str() const & noexcept { return buffer_; }
    /// Moves the accumulated string out of the builder, leaving the builder empty and reusable.
    [[nodiscard]] std::string str() && noexcept {
        std::string result = std::move(buffer_);
        buffer_.clear(); // std::move alone leaves buffer_ unspecified; make "empty" a guarantee
        return result;
    }

  private:
    std::string buffer_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_STRING_BUILDER_HPP
