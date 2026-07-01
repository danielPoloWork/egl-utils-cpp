// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// StringSplitter (component #11): a zero-allocation, string_view-based splitter. It is a lazy
// range: iterating it yields each field between occurrences of a delimiter as a std::string_view
// that borrows the original text — no copies, no heap. The whole type is constexpr, so splitting
// can happen at compile time. Header-only.
#ifndef IT_D4NP_UTIL_STRING_SPLITTER_HPP
#define IT_D4NP_UTIL_STRING_SPLITTER_HPP

#include <cstddef>
#include <iterator>
#include <string_view>

namespace it::d4np::util {

/// A lazy, zero-allocation view over the fields of a string separated by a delimiter.
///
/// `StringSplitter` models a range: `begin()`/`end()` yield `std::string_view` tokens that point
/// into the original text. Splitting is faithful — `n` delimiter occurrences produce exactly
/// `n + 1` tokens — so empty fields are preserved: `"a,,b"` → `"a"`, `""`, `"b"`; a trailing
/// delimiter yields a trailing empty token; and an empty input yields a single empty token.
///
/// The delimiter is a `std::string_view` (matched as a whole sequence). Pass a string literal for
/// a single character, e.g. `StringSplitter{text, ","}`; there is deliberately no `char` overload,
/// so the splitter never dangles and stays trivially copyable.
///
/// @warning Both the text and the delimiter are viewed, not owned: they must outlive the splitter
///          and any tokens it yields. The delimiter must be non-empty.
class StringSplitter {
  public:
    /// A forward iterator producing successive tokens as `std::string_view`.
    class iterator {
      public:
        using value_type = std::string_view;
        using reference = std::string_view;
        using pointer = const std::string_view *;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        constexpr iterator() noexcept = default;

        [[nodiscard]] constexpr std::string_view operator*() const noexcept { return current_; }
        [[nodiscard]] constexpr const std::string_view *operator->() const noexcept { return &current_; }

        constexpr iterator &operator++() noexcept {
            advance();
            return *this;
        }
        constexpr iterator operator++(int) noexcept {
            iterator copy = *this;
            advance();
            return copy;
        }

        [[nodiscard]] constexpr bool operator==(const iterator &other) const noexcept {
            if (done_ || other.done_) {
                return done_ && other.done_;
            }
            return current_.data() == other.current_.data() && current_.size() == other.current_.size();
        }
        [[nodiscard]] constexpr bool operator!=(const iterator &other) const noexcept { return !(*this == other); }

      private:
        friend class StringSplitter;

        constexpr iterator(std::string_view text, std::string_view delimiter) noexcept
            : text_(text), delimiter_(delimiter), done_(false) {
            advance();
        }

        // Loads the next token, or transitions to the end state once the last one is consumed.
        constexpr void advance() noexcept {
            if (final_) {
                done_ = true;
                current_ = {};
                return;
            }
            const std::size_t pos = text_.find(delimiter_, cursor_);
            if (pos == std::string_view::npos) {
                current_ = text_.substr(cursor_);
                final_ = true;
            } else {
                current_ = text_.substr(cursor_, pos - cursor_);
                cursor_ = pos + delimiter_.size();
            }
        }

        std::string_view text_;
        std::string_view delimiter_;
        std::string_view current_;
        std::size_t cursor_ = 0; // offset in text_ where the current token begins
        bool final_ = false;     // the current token is the last one
        bool done_ = true;       // past-the-end (default-constructed iterators are end)
    };

    constexpr StringSplitter(std::string_view text, std::string_view delimiter) noexcept
        : text_(text), delimiter_(delimiter) {}

    [[nodiscard]] constexpr iterator begin() const noexcept { return iterator{text_, delimiter_}; }
    [[nodiscard]] static constexpr iterator end() noexcept { return iterator{}; }

  private:
    std::string_view text_;
    std::string_view delimiter_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_STRING_SPLITTER_HPP
