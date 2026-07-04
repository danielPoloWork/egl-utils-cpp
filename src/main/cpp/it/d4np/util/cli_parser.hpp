// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// CliParser (component #22): a typed command-line argument parser with auto-generated help.
// Arguments are declared fluently and each binds to a caller-owned variable; parsing writes
// the typed value straight into that variable, so there is no heterogeneous result bag to
// query. Values are converted through the CliValue concept (std::string, bool, integers, and
// floating-point). The public boundary follows the spec's value-or-error model: parse()
// returns a ParseResult (success / help-requested / error) and never throws on bad input;
// only registration misuse — a programmer error — throws std::logic_error. Header-only: the
// parser touches no OS APIs. See ADR-0021.
#ifndef IT_D4NP_UTIL_CLI_PARSER_HPP
#define IT_D4NP_UTIL_CLI_PARSER_HPP

#include <it/d4np/util/string_builder.hpp>

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <version>

#if !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)
#include <ios>
#include <locale>
#include <sstream>
#endif

namespace it::d4np::util {

namespace detail {

/// Converts `in` into a `std::string` target (identity copy — always succeeds).
inline bool cli_parse(std::string_view in, std::string &out) {
    out.assign(in);
    return true;
}

/// Converts `in` into a `bool` target against a fixed vocabulary — `true/false`, `1/0`,
/// `yes/no`, `on/off`. Anything else fails the conversion. (Boolean *flags* are presence-based
/// and never reach this path; this serves an explicit `--flag=value` option of `bool` type.)
inline bool cli_parse(std::string_view in, bool &out) {
    if (in == "true" || in == "1" || in == "yes" || in == "on") {
        out = true;
        return true;
    }
    if (in == "false" || in == "0" || in == "no" || in == "off") {
        out = false;
        return true;
    }
    return false;
}

/// Converts `in` into an integer target via base-10 `std::from_chars`. The whole token must be
/// consumed, so trailing garbage (e.g. "12x") is rejected. `bool` is handled by its own
/// overload and excluded here.
template <typename I>
    requires std::integral<I> && (!std::same_as<std::remove_cv_t<I>, bool>)
bool cli_parse(std::string_view in, I &out) {
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
/// Converts `in` into a floating-point target via `std::from_chars`, whole-token consumption
/// required (locale-independent, the standard round-trip path).
template <std::floating_point F> bool cli_parse(std::string_view in, F &out) {
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
/// Fallback for standard libraries without floating-point `std::from_chars` (older libc++): a
/// classic-locale stream that must consume the entire token, so partial parses are rejected.
template <std::floating_point F> bool cli_parse(std::string_view in, F &out) {
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

} // namespace detail

/// A type usable as a CliParser option/positional target: one for which a `detail::cli_parse`
/// overload turns a `std::string_view` into a value. Out of the box: `std::string`, `bool`,
/// every standard integer type, and `float`/`double`. Binding an unsupported type is a
/// compile error at the `add_option`/`add_positional` call site.
template <typename T>
concept CliValue = requires(std::string_view text, T &target) {
    { detail::cli_parse(text, target) } -> std::same_as<bool>;
};

/// The category of a failed parse, for programmatic handling. `message()` on the `ParseResult`
/// carries the human-readable detail; this is the machine-readable discriminant.
enum class CliError : std::uint8_t {
    none,                 ///< No error (success or help-requested).
    unknown_option,       ///< An option token matched no registered name.
    missing_value,        ///< A value option was given with no value to consume.
    unexpected_value,     ///< A flag was given an inline value (e.g. `--verbose=1`).
    invalid_value,        ///< A value failed typed conversion (bad number, bad bool, ...).
    missing_required,     ///< A required option or positional was absent.
    unexpected_positional ///< More positional arguments than were registered.
};

/// The outcome of `CliParser::parse`: success, a request to show help, or an error with a
/// machine-readable `CliError` code and a human-readable message.
///
/// Contextually convertible to `bool` (true iff success), so call sites can write
/// `if (const auto result = parser.parse(argc, argv); !result) { ... }`. When
/// `help_requested()` is true the parse stopped early at `--help`/`-h`: no error, and the
/// caller should print `help()` and exit successfully.
class ParseResult {
  public:
    /// A successful parse.
    [[nodiscard]] static ParseResult success() { return ParseResult{Status::success, CliError::none, {}}; }

    /// A parse short-circuited by `--help`/`-h`.
    [[nodiscard]] static ParseResult help() { return ParseResult{Status::help_requested, CliError::none, {}}; }

    /// A failed parse carrying a category and a message.
    [[nodiscard]] static ParseResult failure(CliError code, std::string message) {
        return ParseResult{Status::error, code, std::move(message)};
    }

    /// Whether the parse succeeded (no error, no help request).
    [[nodiscard]] bool ok() const noexcept { return status_ == Status::success; }

    /// Contextual `bool`: true iff `ok()`.
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }

    /// Whether the parse stopped at `--help`/`-h`. Not an error.
    [[nodiscard]] bool help_requested() const noexcept { return status_ == Status::help_requested; }

    /// The error category (`CliError::none` unless the parse failed).
    [[nodiscard]] CliError error() const noexcept { return error_; }

    /// The human-readable error detail (empty unless the parse failed).
    [[nodiscard]] const std::string &message() const noexcept { return message_; }

  private:
    enum class Status : std::uint8_t { success, help_requested, error };

    ParseResult(Status status, CliError error, std::string message)
        : status_(status), error_(error), message_(std::move(message)) {}

    Status status_;
    CliError error_;
    std::string message_;
};

/// Typed command-line argument parser with auto-generated help (component #22).
///
/// Declare arguments fluently, each bound to a caller-owned variable, then call `parse`:
///
/// ```cpp
/// bool verbose = false;
/// int count = 1;
/// std::string input;
///
/// it::d4np::util::CliParser parser{"mytool", "Process an input file."};
/// parser.add_flag("verbose", 'v', "Enable verbose output.", verbose)
///       .add_option("count", 'c', "Repeat count.", count, false, "N")
///       .add_positional("input", "Path to the input file.", input);
///
/// const auto result = parser.parse(argc, argv);
/// if (result.help_requested()) { std::fputs(parser.help().c_str(), stdout); return 0; }
/// if (!result) { std::fputs(result.message().c_str(), stderr); return 2; }
/// // verbose / count / input now hold the parsed values.
/// ```
///
/// **Grammar.** Long options are `--name`, `--name value`, or `--name=value`. Short options
/// are `-n`, `-n value`, `-n=value`, or attached `-nvalue`; consecutive short *flags* combine
/// (`-ab` == `-a -b`). A lone `--` ends option processing — every later token is positional.
/// Positionals are consumed in registration order. A value option repeated keeps the last
/// value.
///
/// **Error model.** `parse` never throws on user input; it returns a `ParseResult`. Bound
/// variables keep their pre-parse values (the defaults) unless overwritten by a successful
/// match, so set defaults before calling. Registration methods throw `std::logic_error` on a
/// programmer error (empty name, duplicate long/short name, a name colliding with the
/// auto-registered `--help`/`-h`). Every bound reference must outlive the `parse` call.
///
/// @note Not thread-safe: register and parse from one thread. `--help`/`-h` is registered
/// automatically unless the names are already taken.
class CliParser {
  public:
    /// Builds a parser labelled with a program name and an optional description (both shown in
    /// `help()`). Registers `--help`/`-h` automatically.
    explicit CliParser(std::string program_name, std::string description = {})
        : program_name_(std::move(program_name)), description_(std::move(description)) {
        OptionSpec help_spec;
        help_spec.long_name = "help";
        help_spec.short_name = 'h';
        help_spec.help = "Show this help message and exit.";
        help_spec.is_help = true;
        options_.push_back(std::move(help_spec));
    }

    /// Registers a boolean flag: present on the command line sets `bound` to `true`, absent
    /// leaves it untouched. Pass `'\0'` for `short_name` to register a long name only.
    /// @throws std::logic_error on an empty or duplicate name.
    CliParser &add_flag(std::string_view long_name, char short_name, std::string_view help, bool &bound) {
        OptionSpec spec = make_named_spec(long_name, short_name, help);
        spec.takes_value = false;
        spec.set_flag = [&bound] { bound = true; };
        options_.push_back(std::move(spec));
        return *this;
    }

    /// Registers a value option bound to `target`. `value_name` labels the value in the usage
    /// line; `required` makes its absence a `missing_required` error. Pass `'\0'` for
    /// `short_name` to register a long name only.
    /// @throws std::logic_error on an empty or duplicate name.
    template <CliValue T>
    CliParser &add_option(std::string_view long_name, char short_name, std::string_view help, T &target,
                          bool required = false, std::string_view value_name = "VALUE") {
        OptionSpec spec = make_named_spec(long_name, short_name, help);
        spec.takes_value = true;
        spec.required = required;
        spec.value_name = std::string{value_name};
        spec.assign = [&target](std::string_view text) { return detail::cli_parse(text, target); };
        options_.push_back(std::move(spec));
        return *this;
    }

    /// Registers a positional argument bound to `target`, consumed in registration order.
    /// Required by default; an absent required positional is a `missing_required` error.
    /// @throws std::logic_error on an empty or duplicate name.
    template <CliValue T>
    CliParser &add_positional(std::string_view name, std::string_view help, T &target, bool required = true) {
        if (name.empty()) {
            throw std::logic_error("CliParser: positional name must not be empty");
        }
        for (const PositionalSpec &existing : positionals_) {
            if (existing.name == name) {
                throw std::logic_error("CliParser: duplicate positional name '" + std::string{name} + "'");
            }
        }
        PositionalSpec spec;
        spec.name = std::string{name};
        spec.help = std::string{help};
        spec.required = required;
        spec.assign = [&target](std::string_view text) { return detail::cli_parse(text, target); };
        positionals_.push_back(std::move(spec));
        return *this;
    }

    /// Parses `args` (the arguments *without* the program name). Writes matched values into the
    /// bound variables and returns the outcome. Re-parsing is supported: each call starts from
    /// a clean seen-state.
    [[nodiscard]] ParseResult parse(std::span<const std::string_view> args) {
        for (OptionSpec &option : options_) {
            option.seen = false;
        }
        for (PositionalSpec &positional : positionals_) {
            positional.seen = false;
        }

        std::size_t positional_index = 0;
        bool options_ended = false;

        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string_view token = args[i];

            if (!options_ended && token == "--") {
                options_ended = true;
                continue;
            }

            if (!options_ended && token.starts_with("--")) {
                ParseResult result = parse_long(token.substr(2), args, i);
                if (!result.ok()) {
                    return result;
                }
                continue;
            }

            if (!options_ended && token.size() >= 2 && token.front() == '-') {
                ParseResult result = parse_short(token.substr(1), args, i);
                if (!result.ok()) {
                    return result;
                }
                continue;
            }

            ParseResult result = consume_positional(token, positional_index);
            if (!result.ok()) {
                return result;
            }
        }

        return check_required();
    }

    /// Parses a C `main` argument vector, skipping `argv[0]` (the program name).
    [[nodiscard]] ParseResult parse(int argc, const char *const *argv) {
        const std::span<const char *const> raw{argv, static_cast<std::size_t>(argc)};
        std::vector<std::string_view> args;
        bool first = true;
        for (const char *const entry : raw) {
            if (first) { // argv[0] is the program name, not an argument
                first = false;
                continue;
            }
            args.emplace_back(entry);
        }
        return parse(std::span<const std::string_view>{args});
    }

    /// The program name passed at construction.
    [[nodiscard]] const std::string &program_name() const noexcept { return program_name_; }

    /// Renders the auto-generated help text: a usage line, the description, and aligned
    /// sections for positional arguments and options.
    [[nodiscard]] std::string help() const {
        StringBuilder out;
        out.append("Usage: ").append(std::string_view{program_name_});
        if (!options_.empty()) {
            out.append(std::string_view{" [OPTIONS]"});
        }
        for (const PositionalSpec &positional : positionals_) {
            out.append(positional.required ? std::string_view{" <"} : std::string_view{" ["});
            out.append(std::string_view{positional.name});
            out.append(positional.required ? std::string_view{">"} : std::string_view{"]"});
        }
        out.append('\n');

        if (!description_.empty()) {
            out.append('\n').append(std::string_view{description_}).append('\n');
        }

        const std::size_t width = label_column_width();

        if (!positionals_.empty()) {
            out.append(std::string_view{"\nPositional arguments:\n"});
            for (const PositionalSpec &positional : positionals_) {
                append_help_row(out, positional.name, positional.help, width);
            }
        }

        out.append(std::string_view{"\nOptions:\n"});
        for (const OptionSpec &option : options_) {
            append_help_row(out, option_label(option), option_help(option), width);
        }

        return std::move(out).str();
    }

  private:
    struct OptionSpec {
        std::string long_name;  // without "--"; may be empty if short-only
        char short_name = '\0'; // '\0' when there is no short form
        std::string help;
        std::string value_name = "VALUE"; // shown in help for value options
        bool takes_value = false;
        bool required = false;
        bool is_help = false; // the auto-registered --help/-h
        bool seen = false;
        std::function<bool(std::string_view)> assign; // value options: convert-and-store
        std::function<void()> set_flag;               // flags: mark presence
    };

    struct PositionalSpec {
        std::string name;
        std::string help;
        bool required = true;
        bool seen = false;
        std::function<bool(std::string_view)> assign;
    };

    /// Validates a name pair and seeds a spec with the common fields. Shared by flags and value
    /// options. @throws std::logic_error on an empty or duplicate name.
    [[nodiscard]] OptionSpec make_named_spec(std::string_view long_name, char short_name, std::string_view help) const {
        if (long_name.empty() && short_name == '\0') {
            throw std::logic_error("CliParser: an option needs a long name or a short name");
        }
        if (!long_name.empty() && find_long(long_name) != nullptr) {
            throw std::logic_error("CliParser: duplicate long name '--" + std::string{long_name} + "'");
        }
        if (short_name != '\0' && find_short(short_name) != nullptr) {
            throw std::logic_error("CliParser: duplicate short name '-" + std::string(1, short_name) + "'");
        }
        OptionSpec spec;
        spec.long_name = std::string{long_name};
        spec.short_name = short_name;
        spec.help = std::string{help};
        return spec;
    }

    [[nodiscard]] const OptionSpec *find_long(std::string_view name) const {
        for (const OptionSpec &option : options_) {
            if (!option.long_name.empty() && option.long_name == name) {
                return &option;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const OptionSpec *find_short(char name) const {
        for (const OptionSpec &option : options_) {
            if (option.short_name != '\0' && option.short_name == name) {
                return &option;
            }
        }
        return nullptr;
    }

    /// Locates the mutable spec behind a const lookup result (the finders return const pointers
    /// so they can serve the const registration guards). The parser owns `options_`, so this is
    /// a safe const-away within the object.
    [[nodiscard]] OptionSpec *mutable_option(const OptionSpec *option) {
        const auto distance = std::distance(static_cast<const OptionSpec *>(options_.data()), option);
        return std::next(options_.data(), distance);
    }

    /// Handles a `--long` / `--long=value` token. `index` may advance to consume a separate
    /// value token.
    ParseResult parse_long(std::string_view body, std::span<const std::string_view> args, std::size_t &index) {
        std::string_view name = body;
        std::string_view inline_value;
        bool has_inline = false;
        if (const std::size_t eq = body.find('='); eq != std::string_view::npos) {
            name = body.substr(0, eq);
            inline_value = body.substr(eq + 1);
            has_inline = true;
        }

        const OptionSpec *found = find_long(name);
        if (found == nullptr) {
            return ParseResult::failure(CliError::unknown_option, "unknown option '--" + std::string{name} + "'");
        }
        OptionSpec *option = mutable_option(found);

        if (option->is_help) {
            return ParseResult::help();
        }

        if (!option->takes_value) {
            if (has_inline) {
                return ParseResult::failure(CliError::unexpected_value,
                                            "flag '--" + option->long_name + "' takes no value");
            }
            option->set_flag();
            option->seen = true;
            return ParseResult::success();
        }

        std::string_view value;
        if (has_inline) {
            value = inline_value;
        } else {
            if (index + 1 >= args.size()) {
                return ParseResult::failure(CliError::missing_value,
                                            "option '--" + option->long_name + "' requires a value");
            }
            value = args[++index];
        }
        return assign_option(*option, "--" + option->long_name, value);
    }

    /// Handles a `-x...` cluster: a run of short flags optionally ending in a value option that
    /// takes the rest (or the next token) as its value. `index` may advance.
    ParseResult parse_short(std::string_view body, std::span<const std::string_view> args, std::size_t &index) {
        for (std::size_t pos = 0; pos < body.size(); ++pos) {
            const char letter = body[pos];
            const OptionSpec *found = find_short(letter);
            if (found == nullptr) {
                return ParseResult::failure(CliError::unknown_option,
                                            "unknown option '-" + std::string(1, letter) + "'");
            }
            OptionSpec *option = mutable_option(found);

            if (option->is_help) {
                return ParseResult::help();
            }

            const std::string label = "-" + std::string(1, letter);

            if (!option->takes_value) {
                option->set_flag();
                option->seen = true;
                continue; // combine with the next short flag in the cluster
            }

            // Value option: the remainder of the cluster is the value ("-ovalue" / "-o=value");
            // if nothing remains, the next token is the value ("-o value").
            std::string_view rest = body.substr(pos + 1);
            if (!rest.empty()) {
                if (rest.front() == '=') {
                    rest.remove_prefix(1);
                }
                return assign_option(*option, label, rest);
            }
            if (index + 1 >= args.size()) {
                return ParseResult::failure(CliError::missing_value, "option '" + label + "' requires a value");
            }
            return assign_option(*option, label, args[++index]);
        }
        return ParseResult::success();
    }

    /// Runs an option's typed conversion, mapping a conversion failure to `invalid_value`.
    static ParseResult assign_option(OptionSpec &option, const std::string &label, std::string_view value) {
        if (!option.assign(value)) {
            return ParseResult::failure(CliError::invalid_value,
                                        "invalid value '" + std::string{value} + "' for option '" + label + "'");
        }
        option.seen = true;
        return ParseResult::success();
    }

    /// Binds `token` to the next unfilled positional, or reports an overflow.
    ParseResult consume_positional(std::string_view token, std::size_t &positional_index) {
        if (positional_index >= positionals_.size()) {
            return ParseResult::failure(CliError::unexpected_positional,
                                        "unexpected positional argument '" + std::string{token} + "'");
        }
        PositionalSpec &positional = positionals_[positional_index];
        ++positional_index;
        if (!positional.assign(token)) {
            return ParseResult::failure(CliError::invalid_value, "invalid value '" + std::string{token} +
                                                                     "' for argument '" + positional.name + "'");
        }
        positional.seen = true;
        return ParseResult::success();
    }

    /// Verifies every required option and positional was supplied.
    [[nodiscard]] ParseResult check_required() const {
        for (const OptionSpec &option : options_) {
            if (option.required && !option.seen) {
                return ParseResult::failure(CliError::missing_required,
                                            "missing required option '--" + option.long_name + "'");
            }
        }
        for (const PositionalSpec &positional : positionals_) {
            if (positional.required && !positional.seen) {
                return ParseResult::failure(CliError::missing_required,
                                            "missing required argument '" + positional.name + "'");
            }
        }
        return ParseResult::success();
    }

    /// The left-column label for an option in help, e.g. "-c, --count N" or "    --long".
    [[nodiscard]] static std::string option_label(const OptionSpec &option) {
        std::string label;
        if (option.short_name != '\0') {
            label += '-';
            label += option.short_name;
            if (!option.long_name.empty()) {
                label += ", ";
            }
        } else {
            label += "    "; // align long-only rows under the short-flag column
        }
        if (!option.long_name.empty()) {
            label += "--";
            label += option.long_name;
        }
        if (option.takes_value) {
            label += ' ';
            label += option.value_name;
        }
        return label;
    }

    /// The right-column help text for an option, annotating required value options.
    [[nodiscard]] static std::string option_help(const OptionSpec &option) {
        if (option.required) {
            return option.help + " (required)";
        }
        return option.help;
    }

    /// The width of the label column: the longest label across positionals and options, so the
    /// help descriptions line up.
    [[nodiscard]] std::size_t label_column_width() const {
        std::size_t width = 0;
        for (const PositionalSpec &positional : positionals_) {
            width = std::max(width, positional.name.size());
        }
        for (const OptionSpec &option : options_) {
            width = std::max(width, option_label(option).size());
        }
        return width;
    }

    /// Appends one aligned "  <label><padding>  <help>" line to `out`.
    static void append_help_row(StringBuilder &out, std::string_view label, std::string_view help, std::size_t width) {
        out.append(std::string_view{"  "});
        out.append(label);
        for (std::size_t pad = label.size(); pad < width; ++pad) {
            out.append(' ');
        }
        out.append(std::string_view{"  "});
        out.append(help);
        out.append('\n');
    }

    std::string program_name_;
    std::string description_;
    std::vector<OptionSpec> options_;
    std::vector<PositionalSpec> positionals_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_CLI_PARSER_HPP
