// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// StringFormatter (component #12): a compile-time type-safe, std::format-like formatter.
// The format string is validated during constant evaluation (a malformed string or a
// placeholder/argument count mismatch is a compile error at the call site) and arguments are
// constrained by the Formattable concept. The grammar is positional-only: "{}" placeholders
// plus "{{" / "}}" escapes. Rendering runs on top of StringBuilder. See ADR-0012.
#ifndef IT_D4NP_UTIL_STRING_FORMATTER_HPP
#define IT_D4NP_UTIL_STRING_FORMATTER_HPP

#include <it/d4np/util/string_builder.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <version>

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#include <array>
#include <charconv>
#include <iterator>
#include <system_error>
#else
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#endif

namespace it::d4np::util {

namespace detail {

/// Outcome of scanning a format string against an argument count.
enum class FormatStringError : std::uint8_t {
    none,
    unterminated_open_brace,
    stray_close_brace,
    argument_count_mismatch
};

/// Scans `text` for the "{}" / "{{" / "}}" grammar and checks the placeholder count against
/// `argument_count`. Constexpr so the consteval FormatString constructor can run it — and so
/// its logic is unit-testable with static_assert.
constexpr FormatStringError check_format_string(std::string_view text, std::size_t argument_count) noexcept {
    std::size_t placeholders = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const char ch = text[i];
        if (ch == '{') {
            if (i + 1 == text.size()) {
                return FormatStringError::unterminated_open_brace;
            }
            if (text[i + 1] == '{') { // "{{" escape
                i += 2;
                continue;
            }
            if (text[i + 1] == '}') { // "{}" placeholder
                ++placeholders;
                i += 2;
                continue;
            }
            return FormatStringError::unterminated_open_brace; // indices/specs are not supported
        }
        if (ch == '}') {
            if (i + 1 == text.size() || text[i + 1] != '}') {
                return FormatStringError::stray_close_brace;
            }
            i += 2; // "}}" escape
            continue;
        }
        ++i;
    }
    return placeholders == argument_count ? FormatStringError::none : FormatStringError::argument_count_mismatch;
}

/// Deliberately declared but never defined. It is only ever called during constant evaluation
/// (from the consteval FormatString constructor), where calling a non-constexpr function is
/// ill-formed — failing the build and surfacing `reason` in the compiler diagnostic.
void invalid_format_string(const char *reason);

} // namespace detail

/// Argument types StringFormatter renders: anything convertible to std::string_view, `char`,
/// `bool`, the StringBuilder integer set, `float`, and `double`. Everything else (pointers,
/// wide characters, `long double`, arbitrary classes) is rejected at compile time.
template <typename T>
concept Formattable = std::convertible_to<const T &, std::string_view> || std::same_as<std::remove_cv_t<T>, char> ||
                      std::same_as<std::remove_cv_t<T>, bool> || detail::AppendableInteger<T> ||
                      std::same_as<std::remove_cv_t<T>, float> || std::same_as<std::remove_cv_t<T>, double>;

/// A format string validated at compile time against the argument types `Args...`.
///
/// The constructor is consteval: it only accepts strings whose validity is known during
/// constant evaluation (string literals, `constexpr` views), and a malformed string or a
/// placeholder/argument count mismatch fails the build at the call site. Use through the
/// `FormatString` alias, which places `Args...` in a non-deduced context — exactly the C++23
/// `std::format_string` technique (ADR-0012). Dynamic (runtime) format strings are
/// intentionally not representable.
template <typename... Args> class BasicFormatString {
  public:
    /// Implicit on purpose, mirroring `std::format_string`: call sites pass a plain literal.
    template <typename S>
        requires std::convertible_to<const S &, std::string_view>
    // A string literal (char array) becoming a string_view through decay is this constructor's
    // purpose, exactly as in std::format_string.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    consteval BasicFormatString(const S &text) : text_(static_cast<std::string_view>(text)) {
        const detail::FormatStringError error = detail::check_format_string(text_, sizeof...(Args));
        if (error == detail::FormatStringError::unterminated_open_brace) {
            detail::invalid_format_string(R"(unterminated '{': only "{}" placeholders and "{{" escapes exist)");
        }
        if (error == detail::FormatStringError::stray_close_brace) {
            detail::invalid_format_string(R"(stray '}': write "}}" for a literal '}')");
        }
        if (error == detail::FormatStringError::argument_count_mismatch) {
            detail::invalid_format_string("number of {} placeholders differs from the number of arguments");
        }
    }

    /// The validated format string.
    [[nodiscard]] constexpr std::string_view view() const noexcept { return text_; }

  private:
    std::string_view text_;
};

/// The format-string parameter type of `StringFormatter::format`. `std::type_identity_t`
/// keeps `Args...` out of deduction, so they are deduced from the value arguments alone.
template <typename... Args> using FormatString = BasicFormatString<std::type_identity_t<Args>...>;

namespace detail {

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
/// Appends the shortest round-trip base-10 rendering of a floating-point value.
template <typename T> void append_float(StringBuilder &out, T value) {
    std::array<char, 32> digits{}; // the longest shortest-round-trip double takes 24 chars
    char *const first = digits.data();
    char *const last = std::next(first, static_cast<std::ptrdiff_t>(digits.size()));
    const std::to_chars_result result = std::to_chars(first, last, value);
    if (result.ec == std::errc{}) {
        out.append(std::string_view(first, static_cast<std::size_t>(std::distance(first, result.ptr))));
    }
}
#else
/// Fallback for standard libraries without floating-point std::to_chars (older libc++): a
/// classic-locale stream at max_digits10 — round-trip exact, but not always the shortest form.
template <typename T> void append_float(StringBuilder &out, T value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
    out.append(stream.str());
}
#endif

/// Renders one argument into `out`, dispatching on its Formattable category.
template <Formattable T> void append_value(StringBuilder &out, const T &value) {
    using Plain = std::remove_cv_t<T>;
    if constexpr (std::same_as<Plain, bool>) {
        out.append(value ? std::string_view{"true"} : std::string_view{"false"});
    } else if constexpr (std::same_as<Plain, char> || AppendableInteger<T>) {
        out.append(value); // StringBuilder's char / std::to_chars integer overloads
    } else if constexpr (std::is_floating_point_v<Plain>) {
        append_float(out, value);
    } else {
        // String-literal arguments (char arrays) decay by design; length comes from the null
        // terminator, as callers expect.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        out.append(static_cast<std::string_view>(value));
    }
}

/// Appends the literal prefix of `rest` up to the next "{}" placeholder (unescaping "{{" and
/// "}}" on the way), consuming through the placeholder. Returns true when a placeholder was
/// consumed, false when `rest` was exhausted (all of it appended). `rest` must originate from
/// a validated FormatString, which guarantees every brace is part of "{}", "{{", or "}}".
inline bool advance_to_placeholder(StringBuilder &out, std::string_view &rest) {
    std::size_t run = 0; // start of the pending literal run
    std::size_t i = 0;
    while (i < rest.size()) {
        const char ch = rest[i];
        if (ch != '{' && ch != '}') {
            ++i;
            continue;
        }
        out.append(rest.substr(run, i - run));
        if (rest[i + 1] == ch) { // "{{" or "}}" — one literal brace
            out.append(ch);
            i += 2;
            run = i;
            continue;
        }
        rest.remove_prefix(i + 2); // "{}"
        return true;
    }
    out.append(rest.substr(run));
    rest = {};
    return false;
}

/// Appends the literal text before the next placeholder, then `value` in its place.
template <Formattable T> void append_segment(StringBuilder &out, std::string_view &rest, const T &value) {
    if (advance_to_placeholder(out, rest)) {
        append_value(out, value);
    }
}

} // namespace detail

/// Compile-time type-safe, `std::format`-like formatter (component #12).
///
/// The format string is validated during constant evaluation — `"{"` unmatched, `"}"` stray,
/// or a placeholder/argument count mismatch fails the build — and arguments are constrained
/// by `Formattable`. Placeholders are positional-only `{}`; write `{{` / `}}` for literal
/// braces. Format specifications and argument indices are intentionally out of scope
/// (ADR-0012). Rendering builds on `StringBuilder`: integers via `std::to_chars`,
/// floating-point shortest-round-trip where the standard library provides it.
///
/// ```cpp
/// std::string s = StringFormatter::format("{} + {} = {}", 1, 2, "three"); // "1 + 2 = three"
/// ```
///
/// @note Stateless and thread-safe (pure functions). `format_to` is as thread-safe as the
/// builder passed to it.
class StringFormatter {
  public:
    StringFormatter() = delete; // static-only utility: there is nothing to instantiate

    /// Renders `fmt`, replacing each `{}` with the corresponding argument.
    template <Formattable... Args>
    [[nodiscard]] static std::string format(FormatString<Args...> fmt, const Args &...args) {
        StringBuilder out{fmt.view().size() + sizeof...(Args) * expected_argument_length};
        format_to(out, fmt, args...);
        return std::move(out).str();
    }

    /// Renders `fmt` by appending to a caller-supplied builder (no intermediate string),
    /// and returns that builder for chaining.
    template <Formattable... Args>
    static StringBuilder &format_to(StringBuilder &out, FormatString<Args...> fmt, const Args &...args) {
        std::string_view rest = fmt.view();
        (detail::append_segment(out, rest, args), ...);
        static_cast<void>(detail::advance_to_placeholder(out, rest)); // trailing literal run
        return out;
    }

  private:
    static constexpr std::size_t expected_argument_length = 8; // preallocation heuristic per argument
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_STRING_FORMATTER_HPP
