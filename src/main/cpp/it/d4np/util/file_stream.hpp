// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// FileStream (component #16): a RAII wrapper over an OS file descriptor with a configurable,
// owned read/write buffer. Declarations only — every member is defined in file_stream.cpp,
// compiled into the optional STATIC tier (egl-util::egl-util-static, ADR-0004), so the OS
// headers (<fcntl.h>/<unistd.h>, <windows.h>) never leak into consumer translation units.
// A stream is opened read-only or write-only (see FileMode) so the buffer has one coherent
// direction. The value-or-error boundary follows the library convention: normal I/O failure is
// reported (nullopt / false + error()), never thrown; only wrong-direction use — a programmer
// error — throws std::logic_error. See ADR-0023.
#ifndef IT_D4NP_UTIL_FILE_STREAM_HPP
#define IT_D4NP_UTIL_FILE_STREAM_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace it::d4np::util {

/// How a `FileStream` is opened. Each mode is single-direction, so the internal buffer never
/// has to reconcile reads against writes.
enum class FileMode : std::uint8_t {
    read,  ///< Open an existing file for reading ("rb"). Missing file → open fails.
    write, ///< Create or truncate a file for writing ("wb").
    append ///< Create or open a file for writing at the end ("ab").
};

/// A RAII, move-only buffered wrapper around a single OS file descriptor (component #16).
///
/// Open with the `open` factory, then read *or* write depending on the `FileMode`; the owned
/// buffer (sized at open time) coalesces small operations into few syscalls. The descriptor is
/// closed on destruction — but the destructor cannot report a failed final flush, so call
/// `close()` (or `flush()`) explicitly when you need to observe write errors.
///
/// ```cpp
/// auto out = it::d4np::util::FileStream::open("log.txt", it::d4np::util::FileMode::write);
/// if (!out.is_open()) { /* out.error() holds the OS code */ }
/// out.write(std::string_view{"hello\n"});
/// if (!out.close()) { /* a buffered write failed to reach disk */ }
/// ```
///
/// **Error model.** A normal I/O failure (file missing, disk full, permission denied) is
/// reported, never thrown: `open` yields a stream with `is_open() == false` and `error()` set;
/// `read` returns `std::nullopt`, `write`/`flush`/`close` return `false`, with `error()` giving
/// the platform-native code (`errno` on POSIX, `GetLastError()` on Windows). Reading a
/// write-mode stream, or writing a read-mode stream, is a programmer error and throws
/// `std::logic_error`. Operating on a closed stream is a graceful no-op failure, not a throw.
///
/// @note Requires linking `egl-util::egl-util-static`; header-only consumers see these
///       declarations but calls are an unresolved-symbol error at link time (same contract as
///       `library_version()`). Not thread-safe: drive one stream from one thread. Paths are
///       passed to the narrow-character OS open call.
class FileStream {
  public:
    /// Default owned-buffer size in bytes when the caller does not specify one.
    static constexpr std::size_t default_buffer_size = 8192;

    /// Constructs a closed stream (`is_open() == false`). Useful as a movable placeholder.
    FileStream() noexcept = default;

    FileStream(const FileStream &) = delete;
    FileStream &operator=(const FileStream &) = delete;

    /// Move transfers the descriptor and buffer; the source becomes a closed stream.
    FileStream(FileStream &&other) noexcept;
    FileStream &operator=(FileStream &&other) noexcept;

    /// Closes the descriptor (flushing a write buffer, ignoring any error).
    ~FileStream();

    /// Opens `path` in `mode` with an owned buffer of `buffer_size` bytes (clamped to at least
    /// 1). On failure returns a closed stream whose `error()` carries the OS code — inspect
    /// `is_open()`. Never throws.
    [[nodiscard]] static FileStream open(std::string_view path, FileMode mode,
                                         std::size_t buffer_size = default_buffer_size);

    /// Whether the stream holds an open descriptor.
    [[nodiscard]] bool is_open() const noexcept { return handle_ != invalid_handle; }

    /// The last OS error code (`errno` / `GetLastError()`), or 0 if none has occurred.
    [[nodiscard]] int error() const noexcept { return error_; }

    /// Whether a read reached end of file (read mode only).
    [[nodiscard]] bool eof() const noexcept { return eof_; }

    /// The mode the stream was opened in.
    [[nodiscard]] FileMode mode() const noexcept { return mode_; }

    /// The size of the owned buffer in bytes.
    [[nodiscard]] std::size_t buffer_size() const noexcept { return buffer_.size(); }

    /// Reads up to `dst.size()` bytes into `dst`, returning the count read (0 at end of file) or
    /// `std::nullopt` on an OS error. A short read (fewer than requested) is normal near EOF.
    /// @throws std::logic_error if the stream was not opened for reading.
    [[nodiscard]] std::optional<std::size_t> read(std::span<std::byte> dst);

    /// Reads the next line, without the terminating `\n` (a single trailing `\r` is also
    /// stripped, so CRLF files work). Returns `std::nullopt` at end of file or on error —
    /// distinguish with `eof()` / `error()`.
    /// @throws std::logic_error if the stream was not opened for reading.
    [[nodiscard]] std::optional<std::string> read_line();

    /// Writes all of `src`, buffering it; returns `false` on an OS error (see `error()`).
    /// @throws std::logic_error if the stream was not opened for writing.
    [[nodiscard]] bool write(std::span<const std::byte> src);

    /// Writes all of `text` (its bytes verbatim); returns `false` on an OS error.
    /// @throws std::logic_error if the stream was not opened for writing.
    [[nodiscard]] bool write(std::string_view text);

    /// Pushes any buffered writes to the OS. No-op in read mode. Returns `false` on error.
    [[nodiscard]] bool flush();

    /// Flushes (in write mode) and closes the descriptor; idempotent. Returns `false` if the
    /// final flush or the close reported an OS error.
    bool close() noexcept;

  private:
    /// Sentinel for "no descriptor". Matches POSIX's -1 and Windows' INVALID_HANDLE_VALUE.
    static constexpr std::intptr_t invalid_handle = -1;

    /// Refills the read buffer from the OS. Returns true iff at least one byte is now available;
    /// false at EOF (sets `eof_`) or on error (sets `error_`).
    bool fill_buffer() noexcept;

    /// Writes the buffered bytes to the OS and empties the buffer. Returns false on error.
    bool flush_buffer() noexcept;

    std::intptr_t handle_ = invalid_handle;
    std::vector<std::byte> buffer_;
    std::size_t buffer_pos_ = 0; ///< read: next byte to serve; write: current fill level
    std::size_t buffer_len_ = 0; ///< read: valid bytes in the buffer (unused while writing)
    FileMode mode_ = FileMode::read;
    int error_ = 0;
    bool eof_ = false;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_FILE_STREAM_HPP
