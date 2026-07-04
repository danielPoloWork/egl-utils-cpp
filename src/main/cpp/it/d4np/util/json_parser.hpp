// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// JsonParser (component #23): a non-allocating, pull-style (SAX) JSON parser over a
// std::string_view. next() walks the document once and returns typed events whose payload is
// a std::string_view borrowing the source text — parsing copies nothing and allocates nothing
// (nesting is tracked in a fixed, template-sized stack, which also caps adversarial depth).
// Scalars are borrowed raw and converted on demand: to_number() runs std::from_chars over the
// literal, decode_string() unescapes into a caller string only when asked. The public boundary
// follows the spec's value-or-error model: malformed input yields a sticky `error` event with a
// message and byte offset — parsing never throws. See ADR-0022. Header-only: no OS APIs.
#ifndef IT_D4NP_UTIL_JSON_PARSER_HPP
#define IT_D4NP_UTIL_JSON_PARSER_HPP

#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <version>

#if !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)
#include <ios>
#include <locale>
#include <sstream>
#endif

namespace it::d4np::util {

/// The kind of a JSON event returned by `JsonParser::next`.
///
/// Structural events (`begin_object` … `end_array`) carry no text. `key` precedes each object
/// member's value and carries the raw key slice. Scalar events (`string`, `number`, `boolean`,
/// `null_value`) carry the raw source slice (`null_value` carries `"null"`). `end` marks the
/// document fully consumed; `error` marks a malformed document (sticky — every later `next`
/// returns `error` too).
enum class JsonToken : std::uint8_t {
    begin_object,
    end_object,
    begin_array,
    end_array,
    key,
    string,
    number,
    boolean,
    null_value,
    end,
    error
};

/// One event from the pull parser: its kind plus the raw source slice it refers to.
///
/// `text` borrows the parser's source (it must outlive any retained event). For `key`, `string`,
/// `number`, and `boolean` it is the exact source token — string/key text is the *raw* content
/// between the quotes (escapes intact, quotes excluded); decode it with
/// `JsonParser::decode_string`. Numbers are the literal; convert with `JsonParser::to_number`.
/// For structural events, `null_value`, `end`, and `error` it carries no useful payload.
struct JsonEvent {
    JsonToken type = JsonToken::error;
    std::string_view text;

    [[nodiscard]] friend bool operator==(const JsonEvent &, const JsonEvent &) = default;
};

/// A non-allocating, pull-style JSON parser over a `std::string_view` (component #23).
///
/// Construct it over the document text, then call `next()` repeatedly. Each call returns the next
/// `JsonEvent` in a pre-order walk of the document; the sequence for
/// `{"a":1,"b":[true,null]}` is: `begin_object`, `key("a")`, `number("1")`, `key("b")`,
/// `begin_array`, `boolean("true")`, `null_value`, `end_array`, `end_object`, then `end`.
///
/// ```cpp
/// it::d4np::util::JsonParser parser{R"({"n":42})"};
/// for (auto ev = parser.next(); ev.type != JsonToken::end; ev = parser.next()) {
///     if (ev.type == JsonToken::error) { /* parser.error_message(), parser.error_offset() */ break; }
///     if (ev.type == JsonToken::number) { const auto n = JsonParser::to_number<int>(ev.text); }
/// }
/// ```
///
/// **Non-allocating.** Parsing copies nothing and touches no heap: event payloads are views into
/// the source, and nesting is tracked in a fixed `std::array` of `MaxDepth` frames. Exceeding
/// `MaxDepth` is a (data-driven) `error`, not a throw — a built-in guard against adversarial
/// deep nesting. Conversion is opt-in and separate: `to_number` never allocates; `decode_string`
/// allocates only the destination string, and only when the caller asks.
///
/// **Error model.** Malformed input is normal input, not a programmer error: `next` returns an
/// `error` event (never throws) and latches — every subsequent call returns `error` as well, with
/// `error_message()` and `error_offset()` describing the first failure. `failed()` / `done()`
/// probe the state.
///
/// @tparam MaxDepth maximum object/array nesting depth (default 64). One `bool`-pair frame per
///         level; deeper input is reported as an error.
/// @warning The source is viewed, not owned: it must outlive the parser and every event it yields.
///          Not thread-safe: drive one parser from one thread.
template <std::size_t MaxDepth = 64> class JsonParser {
  public:
    static_assert(MaxDepth > 0, "JsonParser requires a positive MaxDepth");

    /// Builds a parser over `source` (the whole JSON document). No parsing happens until `next`.
    explicit JsonParser(std::string_view source) noexcept : source_(source) {}

    /// Advances and returns the next event. Returns `end` once the document is fully consumed and
    /// `error` (sticky) once anything is malformed.
    [[nodiscard]] JsonEvent next() noexcept {
        if (state_ == State::errored) {
            return JsonEvent{.type = JsonToken::error};
        }
        if (state_ == State::finished) {
            return JsonEvent{.type = JsonToken::end};
        }

        if (depth_ == 0) {
            if (!top_level_started_) {
                top_level_started_ = true;
                return start_value();
            }
            // A top-level value has been fully read: only trailing whitespace may remain.
            skip_whitespace();
            if (pos_ < source_.size()) {
                return make_error("unexpected trailing characters after the top-level value");
            }
            state_ = State::finished;
            return JsonEvent{.type = JsonToken::end};
        }

        Frame &frame = stack_[depth_ - 1];
        return frame.is_object ? next_in_object(frame) : next_in_array(frame);
    }

    /// Whether parsing has stopped (either `end` reached or an `error` latched).
    [[nodiscard]] bool done() const noexcept { return state_ != State::running; }

    /// Whether an error has latched.
    [[nodiscard]] bool failed() const noexcept { return state_ == State::errored; }

    /// The message describing the first error (empty until one occurs).
    [[nodiscard]] std::string_view error_message() const noexcept { return error_message_; }

    /// The byte offset in the source at which the first error was detected.
    [[nodiscard]] std::size_t error_offset() const noexcept { return error_offset_; }

    // --- Conversion helpers (opt-in; separate from the allocation-free walk) ------------------

    /// Converts a `number` token to the target arithmetic type via `std::from_chars`, requiring
    /// the whole literal to be consumed. Returns `std::nullopt` on overflow or a type mismatch
    /// (e.g. a fractional literal into an integer). `bool` is intentionally unsupported here —
    /// use the `boolean` token, not a number.
    template <typename T>
        requires(std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool>) || std::floating_point<T>
    [[nodiscard]] static std::optional<T> to_number(std::string_view literal) noexcept {
        T value{};
        if (parse_arithmetic(literal, value)) {
            return value;
        }
        return std::nullopt;
    }

    /// Interprets a `boolean` token: `true` for the literal `"true"`, `false` otherwise. The token
    /// text comes straight from the parser, so it is always exactly `"true"` or `"false"`.
    [[nodiscard]] static bool to_bool(std::string_view literal) noexcept { return literal == "true"; }

    /// Decodes a raw string/key slice (escapes intact, no surrounding quotes) into `out`, applying
    /// the JSON escape rules (`\" \\ \/ \b \f \n \r \t` and `\uXXXX`, including UTF-16 surrogate
    /// pairs, encoded as UTF-8). Returns `false` on a malformed escape, leaving `out` unspecified.
    /// A slice with no backslash is copied verbatim. `out` is cleared first.
    static bool decode_string(std::string_view raw, std::string &out) {
        out.clear();
        out.reserve(raw.size());
        std::size_t i = 0;
        while (i < raw.size()) {
            const char c = raw[i];
            if (c != '\\') {
                out.push_back(c);
                ++i;
                continue;
            }
            if (++i >= raw.size()) {
                return false; // trailing backslash
            }
            const char esc = raw[i++];
            switch (esc) {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                std::uint32_t code = 0;
                if (!read_hex4(raw, i, code)) {
                    return false;
                }
                if (code >= 0xD800 && code <= 0xDBFF) { // high surrogate: needs a low surrogate
                    if (i + 1 >= raw.size() || raw[i] != '\\' || raw[i + 1] != 'u') {
                        return false;
                    }
                    i += 2;
                    std::uint32_t low = 0;
                    if (!read_hex4(raw, i, low) || low < 0xDC00 || low > 0xDFFF) {
                        return false;
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                } else if (code >= 0xDC00 && code <= 0xDFFF) {
                    return false; // lone low surrogate
                }
                append_utf8(out, code);
                break;
            }
            default:
                return false; // unknown escape
            }
        }
        return true;
    }

    /// Convenience wrapper around `decode_string(raw, out)` returning the decoded string, or
    /// `std::nullopt` on a malformed escape.
    [[nodiscard]] static std::optional<std::string> decode_string(std::string_view raw) {
        std::string out;
        if (decode_string(raw, out)) {
            return out;
        }
        return std::nullopt;
    }

  private:
    enum class State : std::uint8_t { running, finished, errored };

    struct Frame {
        bool is_object = false;    // object vs array
        bool seen_element = false; // at least one member/element emitted (drives comma handling)
        bool expect_value = false; // (objects) a key was just emitted; the value comes next
    };

    // --- Container walking --------------------------------------------------------------------

    JsonEvent next_in_object(Frame &frame) noexcept {
        if (frame.expect_value) {
            frame.expect_value = false;
            return start_value();
        }
        skip_whitespace();
        if (peek() == '}') {
            ++pos_;
            return close_frame(JsonToken::end_object);
        }
        if (frame.seen_element) {
            if (peek() != ',') {
                return make_error("expected ',' or '}' in object");
            }
            ++pos_;
            skip_whitespace();
            if (peek() == '}') {
                return make_error("trailing comma before '}'");
            }
        }
        frame.seen_element = true;
        if (peek() != '"') {
            return make_error("expected a string key");
        }
        const std::optional<std::string_view> raw = scan_string();
        if (!raw) {
            return JsonEvent{.type = JsonToken::error};
        }
        skip_whitespace();
        if (peek() != ':') {
            return make_error("expected ':' after object key");
        }
        ++pos_;
        frame.expect_value = true;
        return JsonEvent{.type = JsonToken::key, .text = *raw};
    }

    JsonEvent next_in_array(Frame &frame) noexcept {
        skip_whitespace();
        if (peek() == ']') {
            ++pos_;
            return close_frame(JsonToken::end_array);
        }
        if (frame.seen_element) {
            if (peek() != ',') {
                return make_error("expected ',' or ']' in array");
            }
            ++pos_;
            skip_whitespace();
            if (peek() == ']') {
                return make_error("trailing comma before ']'");
            }
        }
        frame.seen_element = true;
        return start_value();
    }

    // Pops the current frame; if that returns us to the top level, the document's single value is
    // complete. The caller has already consumed the closing brace/bracket.
    JsonEvent close_frame(JsonToken token) noexcept {
        --depth_;
        return JsonEvent{.type = token};
    }

    // --- Value dispatch -----------------------------------------------------------------------

    // Parses whatever value begins at the current position. Scalars are returned directly;
    // containers push a frame and return the matching begin_* event.
    JsonEvent start_value() noexcept {
        skip_whitespace();
        if (pos_ >= source_.size()) {
            return make_error("expected a value but reached end of input");
        }
        switch (source_[pos_]) {
        case '{':
            return open_frame(true, JsonToken::begin_object);
        case '[':
            return open_frame(false, JsonToken::begin_array);
        case '"': {
            const std::optional<std::string_view> raw = scan_string();
            return raw ? JsonEvent{.type = JsonToken::string, .text = *raw} : JsonEvent{.type = JsonToken::error};
        }
        case 't':
            return scan_literal("true", JsonToken::boolean);
        case 'f':
            return scan_literal("false", JsonToken::boolean);
        case 'n':
            return scan_literal("null", JsonToken::null_value);
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return scan_number();
        default:
            return make_error("unexpected character; expected a value");
        }
    }

    JsonEvent open_frame(bool is_object, JsonToken token) noexcept {
        if (depth_ >= MaxDepth) {
            return make_error("maximum nesting depth exceeded");
        }
        ++pos_; // consume '{' or '['
        stack_[depth_] = Frame{is_object, false, false};
        ++depth_;
        return JsonEvent{.type = token};
    }

    // --- Scalar scanners ----------------------------------------------------------------------

    // Scans a JSON string starting at the opening quote and returns the raw inner slice (escapes
    // intact, quotes excluded), advancing past the closing quote. Validates escapes and rejects
    // unescaped control characters. On failure latches an error and returns nullopt.
    std::optional<std::string_view> scan_string() noexcept {
        const std::size_t open = pos_;
        ++pos_; // opening quote
        const std::size_t content = pos_;
        while (pos_ < source_.size()) {
            const auto c = static_cast<unsigned char>(source_[pos_]);
            if (c == '"') {
                const std::string_view raw = source_.substr(content, pos_ - content);
                ++pos_; // closing quote
                return raw;
            }
            if (c == '\\') {
                if (!validate_escape()) {
                    return std::nullopt;
                }
                continue;
            }
            if (c < 0x20) {
                make_error("unescaped control character in string");
                return std::nullopt;
            }
            ++pos_;
        }
        pos_ = open;
        make_error("unterminated string");
        return std::nullopt;
    }

    // At a backslash inside a string: validate the escape and advance past it.
    bool validate_escape() noexcept {
        const std::size_t start = pos_;
        ++pos_; // backslash
        if (pos_ >= source_.size()) {
            pos_ = start;
            make_error("unterminated escape sequence");
            return false;
        }
        switch (source_[pos_]) {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
            ++pos_;
            return true;
        case 'u':
            ++pos_;
            for (int digit = 0; digit < 4; ++digit) {
                if (pos_ >= source_.size() || !is_hex(source_[pos_])) {
                    make_error("invalid \\u escape: expected four hex digits");
                    return false;
                }
                ++pos_;
            }
            return true;
        default:
            make_error("invalid escape sequence");
            return false;
        }
    }

    // Scans and validates a JSON number per the grammar, returning the literal slice.
    JsonEvent scan_number() noexcept {
        const std::size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        // Integer part: a lone 0, or a nonzero digit followed by more digits (no leading zeros).
        if (peek() == '0') {
            ++pos_;
        } else if (is_digit(peek())) {
            while (is_digit(peek())) {
                ++pos_;
            }
        } else {
            return make_error("invalid number: expected a digit");
        }
        // Fraction.
        if (peek() == '.') {
            ++pos_;
            if (!is_digit(peek())) {
                return make_error("invalid number: expected a digit after '.'");
            }
            while (is_digit(peek())) {
                ++pos_;
            }
        }
        // Exponent.
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            if (!is_digit(peek())) {
                return make_error("invalid number: expected a digit in the exponent");
            }
            while (is_digit(peek())) {
                ++pos_;
            }
        }
        return JsonEvent{.type = JsonToken::number, .text = source_.substr(start, pos_ - start)};
    }

    JsonEvent scan_literal(std::string_view literal, JsonToken token) noexcept {
        if (source_.substr(pos_).starts_with(literal)) {
            pos_ += literal.size();
            return JsonEvent{.type = token, .text = literal};
        }
        return make_error("invalid literal");
    }

    // --- Low-level cursor ---------------------------------------------------------------------

    void skip_whitespace() noexcept {
        while (pos_ < source_.size()) {
            const char c = source_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    // The current character, or '\0' at end of input (never a valid structural byte).
    [[nodiscard]] char peek() const noexcept { return pos_ < source_.size() ? source_[pos_] : '\0'; }

    JsonEvent make_error(std::string_view message) noexcept {
        if (state_ != State::errored) { // keep the first error
            state_ = State::errored;
            error_message_ = message;
            error_offset_ = pos_;
        }
        return JsonEvent{.type = JsonToken::error};
    }

    [[nodiscard]] static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    [[nodiscard]] static bool is_hex(char c) noexcept {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    // Reads four hex digits from `raw` starting at `i` (advancing it) into `code`.
    static bool read_hex4(std::string_view raw, std::size_t &i, std::uint32_t &code) noexcept {
        if (i + 4 > raw.size()) {
            return false;
        }
        std::uint32_t value = 0;
        for (int digit = 0; digit < 4; ++digit) {
            const char c = raw[i++];
            value <<= 4U;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        code = value;
        return true;
    }

    // Appends the UTF-8 encoding of a Unicode code point to `out`.
    static void append_utf8(std::string &out, std::uint32_t code) {
        if (code <= 0x7F) {
            out.push_back(static_cast<char>(code));
        } else if (code <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else if (code <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0U | (code >> 18U)));
            out.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        }
    }

    // --- Arithmetic conversion (shared by to_number) ------------------------------------------

    template <typename I>
        requires std::integral<I> && (!std::same_as<std::remove_cv_t<I>, bool>)
    static bool parse_arithmetic(std::string_view in, I &out) noexcept {
        const char *const first = in.data();
        const char *const last = std::next(first, static_cast<std::ptrdiff_t>(in.size()));
        I value{};
        const std::from_chars_result result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr != last) {
            return false;
        }
        out = value;
        return true;
    }

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    template <std::floating_point F> static bool parse_arithmetic(std::string_view in, F &out) noexcept {
        const char *const first = in.data();
        const char *const last = std::next(first, static_cast<std::ptrdiff_t>(in.size()));
        F value{};
        const std::from_chars_result result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr != last) {
            return false;
        }
        out = value;
        return true;
    }
#else
    // Fallback for standard libraries without floating-point std::from_chars (older libc++): a
    // classic-locale stream that must consume the whole token.
    template <std::floating_point F> static bool parse_arithmetic(std::string_view in, F &out) {
        std::istringstream stream{std::string(in)};
        stream.imbue(std::locale::classic());
        F value{};
        stream >> value;
        if (stream.fail() || !stream.eof()) {
            return false;
        }
        out = value;
        return true;
    }
#endif

    std::string_view source_;
    std::size_t pos_ = 0;
    std::array<Frame, MaxDepth> stack_{};
    std::size_t depth_ = 0;
    bool top_level_started_ = false;
    State state_ = State::running;
    std::string_view error_message_;
    std::size_t error_offset_ = 0;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_JSON_PARSER_HPP
