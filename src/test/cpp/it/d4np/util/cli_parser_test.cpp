// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for CliParser (component #22, roadmap 8.1). Exercise the whole grammar (long/short
// options, attached/clustered short forms, the `--` terminator, positionals), the typed
// value-or-error boundary (every CliError category), the auto-generated help, and the
// registration-misuse guards that throw std::logic_error.
#include <doctest/doctest.h>

#include <it/d4np/util/cli_parser.hpp>

#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using it::d4np::util::CliError;
using it::d4np::util::CliParser;
using it::d4np::util::ParseResult;

// Helper: parse a fixed list of argument tokens (without the program name).
template <std::size_t N> ParseResult run(CliParser &parser, const std::array<std::string_view, N> &args) {
    return parser.parse(std::span<const std::string_view>{args});
}

} // namespace

TEST_CASE("an empty argument list on a parser with no requirements succeeds") {
    CliParser parser{"tool"};
    const std::array<std::string_view, 0> args{};
    const ParseResult result = run(parser, args);
    CHECK(result.ok());
    CHECK(static_cast<bool>(result));
    CHECK(result.error() == CliError::none);
    CHECK_FALSE(result.help_requested());
}

TEST_CASE("a long flag toggles its bound variable only when present") {
    bool verbose = false;

    CliParser present{"tool"};
    present.add_flag("verbose", 'v', "Be loud.", verbose);
    const std::array<std::string_view, 1> args{"--verbose"};
    CHECK(run(present, args).ok());
    CHECK(verbose);

    verbose = false;
    CliParser absent{"tool"};
    absent.add_flag("verbose", 'v', "Be loud.", verbose);
    const std::array<std::string_view, 0> none{};
    CHECK(run(absent, none).ok());
    CHECK_FALSE(verbose); // absent: default preserved
}

TEST_CASE("a value option accepts both --name value and --name=value") {
    SUBCASE("separate token") {
        int count = 0;
        CliParser parser{"tool"};
        parser.add_option("count", 'c', "Repeat count.", count);
        const std::array<std::string_view, 2> args{"--count", "42"};
        CHECK(run(parser, args).ok());
        CHECK(count == 42);
    }
    SUBCASE("inline value") {
        int count = 0;
        CliParser parser{"tool"};
        parser.add_option("count", 'c', "Repeat count.", count);
        const std::array<std::string_view, 1> args{"--count=42"};
        CHECK(run(parser, args).ok());
        CHECK(count == 42);
    }
}

TEST_CASE("short options: separate, attached, inline, and clustered flags") {
    SUBCASE("attached value -ovalue") {
        std::string out;
        CliParser parser{"tool"};
        parser.add_option("output", 'o', "Output path.", out);
        const std::array<std::string_view, 1> args{"-oresult.txt"};
        CHECK(run(parser, args).ok());
        CHECK(out == "result.txt");
    }
    SUBCASE("inline value -o=value") {
        std::string out;
        CliParser parser{"tool"};
        parser.add_option("output", 'o', "Output path.", out);
        const std::array<std::string_view, 1> args{"-o=result.txt"};
        CHECK(run(parser, args).ok());
        CHECK(out == "result.txt");
    }
    SUBCASE("separate value -o value") {
        std::string out;
        CliParser parser{"tool"};
        parser.add_option("output", 'o', "Output path.", out);
        const std::array<std::string_view, 2> args{"-o", "result.txt"};
        CHECK(run(parser, args).ok());
        CHECK(out == "result.txt");
    }
    SUBCASE("clustered flags -ab expand to -a -b") {
        bool a = false;
        bool b = false;
        CliParser parser{"tool"};
        parser.add_flag("all", 'a', "All.", a).add_flag("brief", 'b', "Brief.", b);
        const std::array<std::string_view, 1> args{"-ab"};
        CHECK(run(parser, args).ok());
        CHECK(a);
        CHECK(b);
    }
    SUBCASE("a cluster of flags ending in a value option") {
        bool a = false;
        std::string out;
        CliParser parser{"tool"};
        parser.add_flag("all", 'a', "All.", a).add_option("output", 'o', "Out.", out);
        const std::array<std::string_view, 1> args{"-aoresult.txt"};
        CHECK(run(parser, args).ok());
        CHECK(a);
        CHECK(out == "result.txt");
    }
}

TEST_CASE("positionals are consumed in registration order") {
    std::string source;
    std::string dest;
    CliParser parser{"cp"};
    parser.add_positional("source", "Source path.", source).add_positional("dest", "Destination path.", dest);
    const std::array<std::string_view, 2> args{"a.txt", "b.txt"};
    CHECK(run(parser, args).ok());
    CHECK(source == "a.txt");
    CHECK(dest == "b.txt");
}

TEST_CASE("a lone -- ends option processing; later dashed tokens are positional") {
    bool verbose = false;
    std::string file;
    CliParser parser{"tool"};
    parser.add_flag("verbose", 'v', "Loud.", verbose).add_positional("file", "A file.", file);
    const std::array<std::string_view, 3> args{"--", "--verbose", "not-a-flag"};
    // "--verbose" after "--" fills the (only) positional; "not-a-flag" then overflows.
    const ParseResult result = run(parser, args);
    CHECK_FALSE(result.ok());
    CHECK(result.error() == CliError::unexpected_positional);
    CHECK_FALSE(verbose);       // never parsed as a flag
    CHECK(file == "--verbose"); // taken literally as the positional
}

TEST_CASE("typed conversion covers strings, integers, floating point, and explicit bool") {
    SUBCASE("integer") {
        int value = 0;
        CliParser parser{"tool"};
        parser.add_option("n", '\0', "A number.", value);
        const std::array<std::string_view, 1> args{"--n=-17"};
        CHECK(run(parser, args).ok());
        CHECK(value == -17);
    }
    SUBCASE("double") {
        double value = 0.0;
        CliParser parser{"tool"};
        parser.add_option("ratio", '\0', "A ratio.", value);
        const std::array<std::string_view, 1> args{"--ratio=1.5"};
        CHECK(run(parser, args).ok());
        CHECK(value == doctest::Approx(1.5));
    }
    SUBCASE("explicit bool value option") {
        bool flag = false;
        CliParser parser{"tool"};
        parser.add_option("enabled", '\0', "On/off.", flag);
        const std::array<std::string_view, 1> args{"--enabled=yes"};
        CHECK(run(parser, args).ok());
        CHECK(flag);
    }
}

TEST_CASE("a value that fails typed conversion is an invalid_value error") {
    int count = 7; // sentinel: must be untouched on failure
    CliParser parser{"tool"};
    parser.add_option("count", 'c', "Count.", count);
    const std::array<std::string_view, 1> args{"--count=12x"};
    const ParseResult result = run(parser, args);
    CHECK_FALSE(result.ok());
    CHECK(result.error() == CliError::invalid_value);
    CHECK(count == 7); // conversion failed → bound variable keeps its default
}

TEST_CASE("unknown options report the right label for long and short forms") {
    CliParser parser{"tool"};
    SUBCASE("long") {
        const std::array<std::string_view, 1> args{"--nope"};
        const ParseResult result = run(parser, args);
        CHECK(result.error() == CliError::unknown_option);
        CHECK(result.message().find("--nope") != std::string::npos);
    }
    SUBCASE("short label is exactly -x (regression: no stray control byte)") {
        const std::array<std::string_view, 1> args{"-x"};
        const ParseResult result = run(parser, args);
        CHECK(result.error() == CliError::unknown_option);
        CHECK(result.message() == "unknown option '-x'");
    }
}

TEST_CASE("a value option with no value to consume is a missing_value error") {
    std::string out;
    CliParser parser{"tool"};
    parser.add_option("output", 'o', "Out.", out);
    const std::array<std::string_view, 1> args{"--output"};
    const ParseResult result = run(parser, args);
    CHECK(result.error() == CliError::missing_value);
}

TEST_CASE("a flag given an inline value is an unexpected_value error") {
    bool verbose = false;
    CliParser parser{"tool"};
    parser.add_flag("verbose", 'v', "Loud.", verbose);
    const std::array<std::string_view, 1> args{"--verbose=1"};
    const ParseResult result = run(parser, args);
    CHECK(result.error() == CliError::unexpected_value);
    CHECK_FALSE(verbose);
}

TEST_CASE("missing required option and positional are reported") {
    SUBCASE("option") {
        std::string out;
        CliParser parser{"tool"};
        parser.add_option("output", 'o', "Out.", out, /*required=*/true);
        const std::array<std::string_view, 0> args{};
        CHECK(run(parser, args).error() == CliError::missing_required);
    }
    SUBCASE("positional") {
        std::string file;
        CliParser parser{"tool"};
        parser.add_positional("file", "A file.", file); // required by default
        const std::array<std::string_view, 0> args{};
        CHECK(run(parser, args).error() == CliError::missing_required);
    }
    SUBCASE("an optional positional may be omitted") {
        std::string file = "default";
        CliParser parser{"tool"};
        parser.add_positional("file", "A file.", file, /*required=*/false);
        const std::array<std::string_view, 0> args{};
        CHECK(run(parser, args).ok());
        CHECK(file == "default");
    }
}

TEST_CASE("more positionals than registered is an unexpected_positional error") {
    std::string only;
    CliParser parser{"tool"};
    parser.add_positional("only", "The one arg.", only);
    const std::array<std::string_view, 2> args{"first", "second"};
    const ParseResult result = run(parser, args);
    CHECK(result.error() == CliError::unexpected_positional);
    CHECK(result.message().find("second") != std::string::npos);
}

TEST_CASE("--help and -h short-circuit to a help request, not an error") {
    SUBCASE("long") {
        CliParser parser{"tool"};
        const std::array<std::string_view, 1> args{"--help"};
        const ParseResult result = run(parser, args);
        CHECK(result.help_requested());
        CHECK_FALSE(result.ok());
        CHECK(result.error() == CliError::none);
    }
    SUBCASE("short, even mid-cluster") {
        bool verbose = false;
        CliParser parser{"tool"};
        parser.add_flag("verbose", 'v', "Loud.", verbose);
        const std::array<std::string_view, 1> args{"-vh"};
        const ParseResult result = run(parser, args);
        CHECK(result.help_requested());
    }
    SUBCASE("required arguments do not defeat a help request") {
        std::string file;
        CliParser parser{"tool"};
        parser.add_positional("file", "A file.", file); // required, but absent
        const std::array<std::string_view, 1> args{"--help"};
        CHECK(run(parser, args).help_requested());
    }
}

TEST_CASE("help text carries the usage line, description, and every registered name") {
    bool verbose = false;
    int count = 0;
    std::string input;
    CliParser parser{"mytool", "Process an input file."};
    parser.add_flag("verbose", 'v', "Enable verbose output.", verbose)
        .add_option("count", 'c', "Repeat count.", count, false, "N")
        .add_positional("input", "Path to the input file.", input);

    const std::string help = parser.help();
    CHECK(help.find("Usage: mytool") != std::string::npos);
    CHECK(help.find("[OPTIONS]") != std::string::npos);
    CHECK(help.find("<input>") != std::string::npos); // required positional
    CHECK(help.find("Process an input file.") != std::string::npos);
    CHECK(help.find("--verbose") != std::string::npos);
    CHECK(help.find("-c, --count N") != std::string::npos); // value name shown
    CHECK(help.find("--help") != std::string::npos);        // auto-registered
    CHECK(help.find("Positional arguments:") != std::string::npos);
}

TEST_CASE("a required value option is annotated in help") {
    std::string out;
    CliParser parser{"tool"};
    parser.add_option("output", 'o', "Where to write.", out, /*required=*/true);
    CHECK(parser.help().find("(required)") != std::string::npos);
}

TEST_CASE("registration misuse throws std::logic_error") {
    SUBCASE("an option with neither a long nor a short name") {
        CliParser parser{"tool"};
        bool flag = false;
        CHECK_THROWS_AS(parser.add_flag("", '\0', "help", flag), std::logic_error);
    }
    SUBCASE("a duplicate long name") {
        CliParser parser{"tool"};
        bool a = false;
        bool b = false;
        parser.add_flag("dup", 'a', "first", a);
        CHECK_THROWS_AS(parser.add_flag("dup", 'b', "second", b), std::logic_error);
    }
    SUBCASE("a duplicate short name") {
        CliParser parser{"tool"};
        bool a = false;
        bool b = false;
        parser.add_flag("first", 'x', "first", a);
        CHECK_THROWS_AS(parser.add_flag("second", 'x', "second", b), std::logic_error);
    }
    SUBCASE("a name colliding with the auto-registered --help/-h") {
        CliParser parser{"tool"};
        bool flag = false;
        CHECK_THROWS_AS(parser.add_flag("help", '\0', "clash", flag), std::logic_error);
        CHECK_THROWS_AS(parser.add_flag("other", 'h', "clash", flag), std::logic_error);
    }
    SUBCASE("a duplicate positional name") {
        CliParser parser{"tool"};
        std::string a;
        std::string b;
        parser.add_positional("path", "first", a);
        CHECK_THROWS_AS(parser.add_positional("path", "second", b), std::logic_error);
    }
    SUBCASE("an empty positional name") {
        CliParser parser{"tool"};
        std::string a;
        CHECK_THROWS_AS(parser.add_positional("", "help", a), std::logic_error);
    }
}

TEST_CASE("re-parsing starts from a clean seen-state") {
    bool verbose = false;
    CliParser parser{"tool"};
    parser.add_flag("verbose", 'v', "Loud.", verbose);

    const std::array<std::string_view, 1> first{"--verbose"};
    CHECK(run(parser, first).ok());
    CHECK(verbose);

    // A second parse without the flag: the parser's own seen-state resets (the bound
    // variable is the caller's to reset between runs).
    verbose = false;
    const std::array<std::string_view, 0> second{};
    CHECK(run(parser, second).ok());
    CHECK_FALSE(verbose);
}

TEST_CASE("the argc/argv overload skips the program name") {
    int count = 0;
    CliParser parser{"tool"};
    parser.add_option("count", 'c', "Count.", count);

    std::array<const char *, 3> argv{"tool", "--count", "5"};
    const ParseResult result = parser.parse(static_cast<int>(argv.size()), argv.data());
    CHECK(result.ok());
    CHECK(count == 5);
    CHECK(parser.program_name() == "tool");
}

TEST_CASE("the last value wins when a value option repeats") {
    int count = 0;
    CliParser parser{"tool"};
    parser.add_option("count", 'c', "Count.", count);
    const std::array<std::string_view, 4> args{"--count", "1", "-c", "2"};
    CHECK(run(parser, args).ok());
    CHECK(count == 2);
}
