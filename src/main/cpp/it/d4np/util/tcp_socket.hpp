// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// TcpSocket / TcpServer (component #17): move-only RAII wrappers over an OS TCP socket, with a
// portable poll-based readiness model for non-blocking use. Declarations only — every member is
// defined in tcp_socket.cpp, compiled into the optional STATIC tier (egl-util::egl-util-static,
// ADR-0004), so the socket headers (<sys/socket.h>/<netdb.h>, <winsock2.h>) never leak into
// consumer translation units. The value-or-error boundary follows the library convention: a
// connection refused, a peer that closes, or a socket that would block are ordinary runtime
// outcomes, reported (never thrown); only using a closed socket is a graceful no-op failure.
// See ADR-0025.
#ifndef IT_D4NP_UTIL_TCP_SOCKET_HPP
#define IT_D4NP_UTIL_TCP_SOCKET_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace it::d4np::util {

/// Outcome of a non-blocking readiness wait (`wait_readable` / `wait_writable` /
/// `wait_connected`). Distinguishes "the event happened" from a benign timeout and a real error.
enum class WaitResult : std::uint8_t {
    ready,     ///< the socket became readable / writable / connected.
    timed_out, ///< the timeout elapsed with no event (not an error).
    error      ///< the wait itself failed, or a non-blocking connect failed; see `error()`.
};

/// Status of a non-blocking `send` / `recv`. Bytes transferred live in `IoResult::bytes` and are
/// meaningful only when the status is `ok`.
enum class IoStatus : std::uint8_t {
    ok,          ///< bytes were transferred (see `IoResult::bytes`).
    closed,      ///< the peer performed an orderly shutdown (`recv` only); `bytes == 0`.
    would_block, ///< the socket is non-blocking and the operation cannot proceed right now.
    error        ///< a real I/O error occurred; see `error()`.
};

/// The result of a `send` / `recv`: a status plus the byte count (valid when `status == ok`).
struct IoResult {
    IoStatus status = IoStatus::error; ///< what happened.
    std::size_t bytes = 0;             ///< bytes transferred (only meaningful when `ok`).

    /// True iff bytes were transferred without error or block.
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return status == IoStatus::ok; }
};

/// A move-only RAII wrapper around a single connected TCP socket (component #17).
///
/// Create one with the `connect` factory (client side) or receive one from `TcpServer::accept`.
/// By default the socket is non-blocking: `send`/`recv` never block and report `would_block`
/// instead, and `wait_readable`/`wait_writable`/`wait_connected` (a portable `poll`) let a
/// caller drive readiness explicitly. The descriptor is closed on destruction.
///
/// ```cpp
/// auto sock = it::d4np::util::TcpSocket::connect("127.0.0.1", 8080);
/// if (sock.is_open() && sock.wait_connected(1000) == it::d4np::util::WaitResult::ready) {
///     (void)sock.send(std::string_view{"ping"});
/// }
/// ```
///
/// **Error model.** Ordinary network outcomes are reported, never thrown: `connect` failure
/// yields a closed socket with `error()` set (native `errno`/`WSAGetLastError()`); `send`/`recv`
/// return an `IoResult` whose status separates `ok`/`closed`/`would_block`/`error`. Using a
/// closed socket fails gracefully. Move-only; copying is deleted (a descriptor has one owner).
///
/// @note Requires linking `egl-util::egl-util-static`. Not thread-safe: drive one socket from one
///       thread (a reader and a writer thread on the same socket is allowed, as for any fd).
class TcpSocket {
  public:
    /// Constructs a closed socket (`is_open() == false`). Useful as a movable placeholder.
    TcpSocket() noexcept = default;

    TcpSocket(const TcpSocket &) = delete;
    TcpSocket &operator=(const TcpSocket &) = delete;

    /// Move transfers the descriptor; the source becomes a closed socket.
    TcpSocket(TcpSocket &&other) noexcept;
    TcpSocket &operator=(TcpSocket &&other) noexcept;

    /// Closes the descriptor.
    ~TcpSocket();

    /// Connects to `host`:`port` (a numeric literal or a resolvable name; resolution is
    /// synchronous). With `non_blocking == true` (default) the call returns as soon as the
    /// connect is *initiated* — `is_open()` is true but the connection may still be completing,
    /// so call `wait_connected()` before sending. On immediate failure returns a closed socket
    /// whose `error()` carries the OS code. Never throws.
    [[nodiscard]] static TcpSocket connect(std::string_view host, std::uint16_t port, bool non_blocking = true);

    /// Whether the socket holds an open descriptor.
    [[nodiscard]] bool is_open() const noexcept { return handle_ != invalid_handle; }

    /// The last OS error code (`errno` / `WSAGetLastError()`), or 0 if none has occurred.
    [[nodiscard]] int error() const noexcept { return error_; }

    /// Whether the socket is in non-blocking mode.
    [[nodiscard]] bool non_blocking() const noexcept { return non_blocking_; }

    /// Switches non-blocking mode on or off. Returns `false` on an OS error (see `error()`).
    [[nodiscard]] bool set_non_blocking(bool enabled) noexcept;

    /// Waits up to `timeout_ms` (negative = block indefinitely) for a non-blocking `connect` to
    /// complete. `ready` means the socket is usable; `error` means the connect failed (see
    /// `error()`); `timed_out` means keep waiting.
    [[nodiscard]] WaitResult wait_connected(int timeout_ms) noexcept;

    /// Waits up to `timeout_ms` (negative = block) for the socket to become readable.
    [[nodiscard]] WaitResult wait_readable(int timeout_ms) noexcept;

    /// Waits up to `timeout_ms` (negative = block) for the socket to become writable.
    [[nodiscard]] WaitResult wait_writable(int timeout_ms) noexcept;

    /// Sends up to `src.size()` bytes in a single OS call. A partial send (fewer than requested)
    /// is normal when the kernel buffer is nearly full — resend the remainder. See `IoResult`.
    [[nodiscard]] IoResult send(std::span<const std::byte> src) noexcept;

    /// Sends the bytes of `text` (verbatim). See `IoResult`.
    [[nodiscard]] IoResult send(std::string_view text) noexcept;

    /// Receives up to `dst.size()` bytes. `status == closed` (with `bytes == 0`) signals an
    /// orderly peer shutdown; `would_block` means no data is available right now. See `IoResult`.
    [[nodiscard]] IoResult recv(std::span<std::byte> dst) noexcept;

    /// Half-closes the write direction (sends FIN); the peer sees end-of-stream while this side
    /// can still read. Returns `false` on an OS error.
    bool shutdown_write() noexcept;

    /// Closes the descriptor; idempotent. Returns `false` if the OS close reported an error.
    bool close() noexcept;

  private:
    friend class TcpServer;

    /// Adopts an already-open descriptor (used by `TcpServer::accept`).
    TcpSocket(std::intptr_t handle, bool non_blocking) noexcept : handle_(handle), non_blocking_(non_blocking) {}

    /// Sentinel for "no descriptor" (POSIX -1; Windows `INVALID_SOCKET` casts to -1).
    static constexpr std::intptr_t invalid_handle = -1;

    std::intptr_t handle_ = invalid_handle;
    int error_ = 0;
    bool non_blocking_ = false;
};

/// A move-only RAII wrapper around a listening TCP socket that hands out `TcpSocket`s
/// (component #17).
///
/// `listen` binds to a port on all interfaces and starts accepting. By default the listener is
/// non-blocking: `accept` returns `std::nullopt` immediately when no connection is pending. Use
/// `wait_readable` to block until one is (or drive it from a poll loop of your own).
///
/// ```cpp
/// auto server = it::d4np::util::TcpServer::listen(0); // 0 => an OS-assigned ephemeral port
/// const std::uint16_t port = server.local_port();
/// if (server.wait_readable(1000) == it::d4np::util::WaitResult::ready) {
///     std::optional<it::d4np::util::TcpSocket> peer = server.accept();
/// }
/// ```
///
/// **Error model.** As with `TcpSocket`: `listen` failure yields a closed server with `error()`
/// set; `accept` returns `std::nullopt` both when nothing is pending (a non-blocking would-block,
/// with `error() == 0`) and on a real error (`error() != 0`) — check `error()` to distinguish.
///
/// @note Requires linking `egl-util::egl-util-static`. Not thread-safe.
class TcpServer {
  public:
    /// Constructs a closed server (`is_open() == false`).
    TcpServer() noexcept = default;

    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    /// Move transfers the descriptor; the source becomes a closed server.
    TcpServer(TcpServer &&other) noexcept;
    TcpServer &operator=(TcpServer &&other) noexcept;

    /// Closes the descriptor.
    ~TcpServer();

    /// Binds to `port` on all interfaces (IPv4) and listens with the given `backlog`. Passing
    /// `port == 0` asks the OS for an ephemeral port — read it back with `local_port()`. The
    /// address is reusable (`SO_REUSEADDR`) so a restart need not wait out `TIME_WAIT`. On
    /// failure returns a closed server whose `error()` carries the OS code. Never throws.
    [[nodiscard]] static TcpServer listen(std::uint16_t port, int backlog = 128, bool non_blocking = true);

    /// Whether the server holds an open listening descriptor.
    [[nodiscard]] bool is_open() const noexcept { return handle_ != invalid_handle; }

    /// The last OS error code (`errno` / `WSAGetLastError()`), or 0 if none has occurred.
    [[nodiscard]] int error() const noexcept { return error_; }

    /// Whether the listener is in non-blocking mode.
    [[nodiscard]] bool non_blocking() const noexcept { return non_blocking_; }

    /// The port actually bound (host byte order) — the argument to `listen`, or the OS-assigned
    /// port when `0` was requested. 0 if the server is closed.
    [[nodiscard]] std::uint16_t local_port() const noexcept { return local_port_; }

    /// Waits up to `timeout_ms` (negative = block) for a connection to be pending on `accept`.
    [[nodiscard]] WaitResult wait_readable(int timeout_ms) noexcept;

    /// Accepts one pending connection, returning it as a `TcpSocket` (inheriting the listener's
    /// blocking mode). Returns `std::nullopt` when none is pending (non-blocking would-block,
    /// `error() == 0`) or on a real error (`error() != 0`).
    [[nodiscard]] std::optional<TcpSocket> accept() noexcept;

    /// Closes the descriptor; idempotent. Returns `false` if the OS close reported an error.
    bool close() noexcept;

  private:
    static constexpr std::intptr_t invalid_handle = -1;

    std::intptr_t handle_ = invalid_handle;
    int error_ = 0;
    std::uint16_t local_port_ = 0;
    bool non_blocking_ = false;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_TCP_SOCKET_HPP
