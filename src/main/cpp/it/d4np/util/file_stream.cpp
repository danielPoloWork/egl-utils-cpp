// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Compiled tier of FileStream (component #16, ADR-0023). This translation unit owns the OS
// file-descriptor surface — CreateFileA/ReadFile/WriteFile/CloseHandle on Windows,
// open/read/write/close on POSIX — so those headers never reach consumers. The buffering,
// single-direction contract, and value-or-error boundary live here; the header exposes only
// declarations (requires egl-util::egl-util-static, ADR-0004).
#include <it/d4np/util/file_stream.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <limits>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace it::d4np::util {

namespace {

// Sentinel matching FileStream::invalid_handle (POSIX -1, Windows INVALID_HANDLE_VALUE).
constexpr std::intptr_t k_invalid = -1;

#ifdef _WIN32

std::intptr_t os_open(const std::string &path, FileMode mode, int &err) noexcept {
    DWORD access = 0;
    DWORD disposition = 0;
    switch (mode) {
    case FileMode::read:
        access = GENERIC_READ;
        disposition = OPEN_EXISTING;
        break;
    case FileMode::write:
        access = GENERIC_WRITE;
        disposition = CREATE_ALWAYS;
        break;
    case FileMode::append:
        access = FILE_APPEND_DATA;
        disposition = OPEN_ALWAYS;
        break;
    }
    // Share read/write/delete so behaviour matches POSIX, which applies no mandatory lock: the
    // wrapper never locks a file, and concurrent access is the caller's contract to arbitrate.
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const HANDLE handle =
        ::CreateFileA(path.c_str(), access, share, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        err = static_cast<int>(::GetLastError());
        return k_invalid;
    }
    return reinterpret_cast<std::intptr_t>(handle); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

std::int64_t os_read(std::intptr_t handle, std::span<std::byte> dst, int &err) noexcept {
    const auto capped = static_cast<DWORD>(std::min<std::size_t>(dst.size(), std::numeric_limits<DWORD>::max()));
    DWORD read = 0;
    // The OS handle is type-erased behind an intptr_t field to keep windows.h out of the
    // header; casting it back is the boundary exception (cf. StackTrace).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    if (::ReadFile(reinterpret_cast<HANDLE>(handle), dst.data(), capped, &read, nullptr) == 0) {
        err = static_cast<int>(::GetLastError());
        return -1;
    }
    return static_cast<std::int64_t>(read); // 0 == end of file
}

bool os_write_all(std::intptr_t handle, std::span<const std::byte> src, int &err) noexcept {
    std::size_t written = 0;
    while (written < src.size()) {
        const std::span<const std::byte> chunk = src.subspan(written);
        const auto capped = static_cast<DWORD>(std::min<std::size_t>(chunk.size(), std::numeric_limits<DWORD>::max()));
        DWORD wrote = 0;
        // The OS handle is type-erased behind an intptr_t field to keep windows.h out of the
        // header; casting it back is the boundary exception (cf. StackTrace).
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        if (::WriteFile(reinterpret_cast<HANDLE>(handle), chunk.data(), capped, &wrote, nullptr) == 0) {
            err = static_cast<int>(::GetLastError());
            return false;
        }
        written += wrote;
    }
    return true;
}

bool os_close(std::intptr_t handle, int &err) noexcept {
    // The OS handle is type-erased behind an intptr_t field to keep windows.h out of the
    // header; casting it back is the boundary exception (cf. StackTrace).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    if (::CloseHandle(reinterpret_cast<HANDLE>(handle)) == 0) {
        err = static_cast<int>(::GetLastError());
        return false;
    }
    return true;
}

#else // POSIX

std::intptr_t os_open(const std::string &path, FileMode mode, int &err) noexcept {
    int flags = 0;
    switch (mode) {
    case FileMode::read:
        flags = O_RDONLY;
        break;
    case FileMode::write:
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    case FileMode::append:
        flags = O_WRONLY | O_CREAT | O_APPEND;
        break;
    }
    constexpr int create_perms = 0644; // rw-r--r--; umask still applies
    int fd = -1;
    do {
        fd = ::open(path.c_str(), flags, create_perms); // NOLINT(cppcoreguidelines-pro-type-vararg)
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        err = errno;
        return k_invalid;
    }
    return static_cast<std::intptr_t>(fd);
}

std::int64_t os_read(std::intptr_t handle, std::span<std::byte> dst, int &err) noexcept {
    for (;;) {
        const ssize_t n = ::read(static_cast<int>(handle), dst.data(), dst.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = errno;
            return -1;
        }
        return static_cast<std::int64_t>(n); // 0 == end of file
    }
}

bool os_write_all(std::intptr_t handle, std::span<const std::byte> src, int &err) noexcept {
    std::size_t written = 0;
    while (written < src.size()) {
        const std::span<const std::byte> chunk = src.subspan(written);
        const ssize_t n = ::write(static_cast<int>(handle), chunk.data(), chunk.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = errno;
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

bool os_close(std::intptr_t handle, int &err) noexcept {
    // POSIX.1-2008: after close() returns, the descriptor is gone even on EINTR, so retrying
    // could close a reused fd. Treat EINTR as success.
    if (::close(static_cast<int>(handle)) == 0 || errno == EINTR) {
        return true;
    }
    err = errno;
    return false;
}

#endif

} // namespace

FileStream::FileStream(FileStream &&other) noexcept
    : handle_(other.handle_), buffer_(std::move(other.buffer_)), buffer_pos_(other.buffer_pos_),
      buffer_len_(other.buffer_len_), mode_(other.mode_), error_(other.error_), eof_(other.eof_) {
    other.handle_ = invalid_handle;
    other.buffer_pos_ = 0;
    other.buffer_len_ = 0;
}

FileStream &FileStream::operator=(FileStream &&other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        buffer_ = std::move(other.buffer_);
        buffer_pos_ = other.buffer_pos_;
        buffer_len_ = other.buffer_len_;
        mode_ = other.mode_;
        error_ = other.error_;
        eof_ = other.eof_;
        other.handle_ = invalid_handle;
        other.buffer_pos_ = 0;
        other.buffer_len_ = 0;
    }
    return *this;
}

FileStream::~FileStream() { static_cast<void>(close()); }

FileStream FileStream::open(std::string_view path, FileMode mode, std::size_t buffer_size) {
    FileStream stream;
    stream.mode_ = mode;
    stream.buffer_.resize(std::max<std::size_t>(buffer_size, 1));
    int err = 0;
    const std::intptr_t handle = os_open(std::string(path), mode, err);
    if (handle == invalid_handle) {
        stream.error_ = err;
        return stream; // closed; caller inspects is_open()/error()
    }
    stream.handle_ = handle;
    return stream;
}

bool FileStream::fill_buffer() noexcept {
    buffer_pos_ = 0;
    buffer_len_ = 0;
    int err = 0;
    const std::int64_t n = os_read(handle_, std::span<std::byte>(buffer_), err);
    if (n < 0) {
        error_ = err;
        return false;
    }
    if (n == 0) {
        eof_ = true;
        return false;
    }
    buffer_len_ = static_cast<std::size_t>(n);
    return true;
}

bool FileStream::flush_buffer() noexcept {
    if (buffer_pos_ == 0) {
        return true;
    }
    int err = 0;
    const std::span<const std::byte> pending = std::span<const std::byte>(buffer_).subspan(0, buffer_pos_);
    if (!os_write_all(handle_, pending, err)) {
        error_ = err;
        return false;
    }
    buffer_pos_ = 0;
    return true;
}

std::optional<std::size_t> FileStream::read(std::span<std::byte> dst) {
    if (!is_open()) {
        return std::nullopt;
    }
    if (mode_ != FileMode::read) {
        throw std::logic_error("FileStream::read on a stream not opened for reading");
    }
    std::size_t total = 0;
    while (total < dst.size()) {
        if (buffer_pos_ >= buffer_len_ && !fill_buffer()) {
            break; // end of file or error
        }
        const std::size_t take = std::min(buffer_len_ - buffer_pos_, dst.size() - total);
        const std::span<const std::byte> src = std::span<const std::byte>(buffer_).subspan(buffer_pos_, take);
        std::ranges::copy(src, std::next(dst.begin(), static_cast<std::ptrdiff_t>(total)));
        buffer_pos_ += take;
        total += take;
    }
    if (total == 0 && error_ != 0) {
        return std::nullopt;
    }
    return total; // 0 signals end of file
}

std::optional<std::string> FileStream::read_line() {
    if (!is_open()) {
        return std::nullopt;
    }
    if (mode_ != FileMode::read) {
        throw std::logic_error("FileStream::read_line on a stream not opened for reading");
    }
    std::string line;
    for (;;) {
        if (buffer_pos_ >= buffer_len_ && !fill_buffer()) {
            break; // end of file or error
        }
        const std::span<const std::byte> window =
            std::span<const std::byte>(buffer_).subspan(buffer_pos_, buffer_len_ - buffer_pos_);
        const auto newline = std::ranges::find(window, std::byte{'\n'});
        const auto count = static_cast<std::size_t>(std::ranges::distance(window.begin(), newline));
        for (std::size_t i = 0; i < count; ++i) {
            line.push_back(static_cast<char>(window[i]));
        }
        if (newline != window.end()) {
            buffer_pos_ += count + 1; // consume the newline too
            if (!line.empty() && line.back() == '\r') {
                line.pop_back(); // fold CRLF into LF
            }
            return line;
        }
        buffer_pos_ += count; // whole window consumed, no newline yet
    }
    if (error_ != 0) {
        return std::nullopt;
    }
    if (line.empty()) {
        return std::nullopt; // clean end of file, nothing pending
    }
    return line; // last line, unterminated
}

bool FileStream::write(std::span<const std::byte> src) {
    if (!is_open()) {
        return false;
    }
    if (mode_ != FileMode::write && mode_ != FileMode::append) {
        throw std::logic_error("FileStream::write on a stream not opened for writing");
    }
    if (src.size() >= buffer_.size()) {
        // Large write: flush what is pending, then hand the block straight to the OS.
        if (!flush_buffer()) {
            return false;
        }
        int err = 0;
        if (!os_write_all(handle_, src, err)) {
            error_ = err;
            return false;
        }
        return true;
    }
    if (buffer_pos_ + src.size() > buffer_.size()) {
        if (!flush_buffer()) {
            return false;
        }
    }
    std::ranges::copy(src, std::next(buffer_.begin(), static_cast<std::ptrdiff_t>(buffer_pos_)));
    buffer_pos_ += src.size();
    return true;
}

bool FileStream::write(std::string_view text) { return write(std::as_bytes(std::span<const char>(text))); }

bool FileStream::flush() {
    if (!is_open()) {
        return false;
    }
    if (mode_ == FileMode::read) {
        return true;
    }
    return flush_buffer();
}

bool FileStream::close() noexcept {
    if (!is_open()) {
        return true;
    }
    bool ok = true;
    if (mode_ == FileMode::write || mode_ == FileMode::append) {
        ok = flush_buffer();
    }
    int err = 0;
    if (!os_close(handle_, err)) {
        error_ = err;
        ok = false;
    }
    handle_ = invalid_handle;
    buffer_pos_ = 0;
    buffer_len_ = 0;
    return ok;
}

} // namespace it::d4np::util
