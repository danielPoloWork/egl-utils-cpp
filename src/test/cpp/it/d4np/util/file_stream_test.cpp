// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for FileStream (component #16, roadmap 9.1). Exercise the RAII/move lifecycle, the
// three open modes, buffered read/write round-trips (including buffers smaller than the data,
// to force multiple syscalls and the large-write bypass), read_line/CRLF/last-line handling,
// and the value-or-error boundary — open failure and closed-stream ops report gracefully,
// wrong-direction use throws std::logic_error. Requires the compiled STATIC tier.
#include <doctest/doctest.h>

#include <it/d4np/util/file_stream.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using it::d4np::util::FileMode;
using it::d4np::util::FileStream;

// A unique temp path removed on scope exit (no Date/random: a per-run counter).
struct TempFile {
    std::filesystem::path path;

    TempFile() {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() / ("egl_fs_" + std::to_string(counter++) + ".tmp");
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;
    TempFile(TempFile &&) = delete;
    TempFile &operator=(TempFile &&) = delete;
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    [[nodiscard]] std::string str() const { return path.string(); }
};

// Reads the whole stream through an intentionally tiny buffer (forces several fills).
std::string read_all(FileStream &stream) {
    std::string out;
    std::array<std::byte, 8> chunk{};
    for (;;) {
        const std::optional<std::size_t> n = stream.read(chunk);
        if (!n || *n == 0) {
            break;
        }
        for (std::size_t i = 0; i < *n; ++i) {
            out.push_back(static_cast<char>(chunk[i]));
        }
    }
    return out;
}

// Writes `text` to `path` in `mode`, returning whether every step succeeded.
bool write_file(const std::string &path, std::string_view text, FileMode mode = FileMode::write) {
    FileStream out = FileStream::open(path, mode);
    if (!out.is_open()) {
        return false;
    }
    return out.write(text) && out.close();
}

} // namespace

TEST_CASE("a default-constructed stream is closed") {
    const FileStream stream;
    CHECK_FALSE(stream.is_open());
    CHECK(stream.error() == 0);
}

TEST_CASE("write then read round-trips the bytes") {
    const TempFile file;
    CHECK(write_file(file.str(), "hello, file\n"));

    FileStream in = FileStream::open(file.str(), FileMode::read);
    REQUIRE(in.is_open());
    CHECK(in.mode() == FileMode::read);
    CHECK(read_all(in) == "hello, file\n");
    CHECK(in.eof());
}

TEST_CASE("opening a missing file for reading fails gracefully, without throwing") {
    const TempFile file; // never created
    FileStream in = FileStream::open(file.str(), FileMode::read);
    CHECK_FALSE(in.is_open());
    CHECK(in.error() != 0);
}

TEST_CASE("write truncates and append extends") {
    const TempFile file;
    CHECK(write_file(file.str(), "first\n"));
    CHECK(write_file(file.str(), "second\n", FileMode::append));

    FileStream in = FileStream::open(file.str(), FileMode::read);
    REQUIRE(in.is_open());
    CHECK(read_all(in) == "first\nsecond\n");
    CHECK(in.close());

    // write mode truncates what was there.
    CHECK(write_file(file.str(), "fresh\n"));
    FileStream in2 = FileStream::open(file.str(), FileMode::read);
    REQUIRE(in2.is_open());
    CHECK(read_all(in2) == "fresh\n");
}

TEST_CASE("data larger than the buffer round-trips (bypass + multi-fill)") {
    const TempFile file;
    const std::string big(50000, 'x'); // >> the tiny buffers below

    FileStream out = FileStream::open(file.str(), FileMode::write, /*buffer_size=*/64);
    REQUIRE(out.is_open());
    CHECK(out.buffer_size() == 64);
    CHECK(out.write(big));
    CHECK(out.close());

    FileStream in = FileStream::open(file.str(), FileMode::read, /*buffer_size=*/64);
    REQUIRE(in.is_open());
    const std::string got = read_all(in);
    CHECK(got.size() == big.size());
    CHECK(got == big);
}

TEST_CASE("read fills exactly what is asked, short only at EOF") {
    const TempFile file;
    CHECK(write_file(file.str(), "abcdef"));

    FileStream in = FileStream::open(file.str(), FileMode::read, /*buffer_size=*/4);
    REQUIRE(in.is_open());

    std::array<std::byte, 3> three{};
    const std::optional<std::size_t> first = in.read(three); // spans a buffer refill
    REQUIRE(first.has_value());
    CHECK(*first == 3);

    std::array<std::byte, 10> rest{};
    const std::optional<std::size_t> second = in.read(rest); // only 3 left
    REQUIRE(second.has_value());
    CHECK(*second == 3);

    const std::optional<std::size_t> third = in.read(rest); // EOF
    REQUIRE(third.has_value());
    CHECK(*third == 0);
    CHECK(in.eof());
}

TEST_CASE("read_line splits on newlines, folds CRLF, and yields the last unterminated line") {
    const TempFile file;
    CHECK(write_file(file.str(), "alpha\nbeta\r\ngamma"));

    FileStream in = FileStream::open(file.str(), FileMode::read, /*buffer_size=*/4);
    REQUIRE(in.is_open());
    CHECK(in.read_line() == std::optional<std::string>{"alpha"});
    CHECK(in.read_line() == std::optional<std::string>{"beta"});  // \r stripped
    CHECK(in.read_line() == std::optional<std::string>{"gamma"}); // no trailing newline
    CHECK(in.read_line() == std::nullopt);                        // EOF
    CHECK(in.eof());
}

TEST_CASE("wrong-direction use is a programmer error and throws") {
    const TempFile file;
    CHECK(write_file(file.str(), "data\n"));

    FileStream in = FileStream::open(file.str(), FileMode::read);
    REQUIRE(in.is_open());
    CHECK_THROWS_AS(static_cast<void>(in.write(std::string_view{"nope"})), std::logic_error);

    FileStream out = FileStream::open(file.str(), FileMode::append);
    REQUIRE(out.is_open());
    std::array<std::byte, 4> buf{};
    CHECK_THROWS_AS(static_cast<void>(out.read(buf)), std::logic_error);
}

TEST_CASE("operations on a closed stream report failure, not a throw") {
    FileStream closed;
    std::array<std::byte, 4> buf{};
    CHECK(closed.read(buf) == std::nullopt);
    CHECK_FALSE(closed.write(std::string_view{"x"})); // closed: no direction check reached
    CHECK_FALSE(closed.flush());
    CHECK(closed.close()); // idempotent no-op
}

TEST_CASE("move transfers ownership and leaves the source closed") {
    const TempFile file;
    CHECK(write_file(file.str(), "movable\n"));

    FileStream original = FileStream::open(file.str(), FileMode::read);
    REQUIRE(original.is_open());

    FileStream moved = std::move(original);
    CHECK(moved.is_open());
    CHECK_FALSE(original.is_open()); // NOLINT(bugprone-use-after-move) — asserting the moved-from state
    CHECK(read_all(moved) == "movable\n");

    // Move-assignment closes the current target first.
    FileStream target;
    target = std::move(moved);
    CHECK(target.is_open());
}

TEST_CASE("close is idempotent and flushes buffered writes") {
    const TempFile file;
    FileStream out = FileStream::open(file.str(), FileMode::write, /*buffer_size=*/1024);
    REQUIRE(out.is_open());
    CHECK(out.write(std::string_view{"buffered but not yet flushed"}));
    CHECK(out.close()); // flushes
    CHECK(out.close()); // idempotent
    CHECK_FALSE(out.is_open());

    FileStream in = FileStream::open(file.str(), FileMode::read);
    REQUIRE(in.is_open());
    CHECK(read_all(in) == "buffered but not yet flushed");
}
