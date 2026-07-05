// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Compiled tier of TcpSocket / TcpServer (component #17, ADR-0025). This translation unit owns
// the OS socket surface — Winsock (winsock2.h/ws2tcpip.h) on Windows, BSD sockets
// (sys/socket.h/netdb.h/poll.h) on POSIX — so those headers never reach consumers. The readiness
// model is a portable one-descriptor poll (::poll / ::WSAPoll); the value-or-error boundary and
// the non-blocking state machine live here. The header exposes only declarations (requires
// egl-util::egl-util-static, ADR-0004).
#include <it/d4np/util/tcp_socket.hpp>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
// winsock2.h must precede windows.h so the legacy winsock.h can never be dragged in;
// ws2tcpip.h (getaddrinfo) requires winsock2.h first — keep this order.
#include <winsock2.h>

#include <ws2tcpip.h>

#include <windows.h>

#include <mutex>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include <memory>
#endif

namespace it::d4np::util {

namespace {

// Sentinel matching TcpSocket::invalid_handle (POSIX -1; Windows INVALID_SOCKET casts to -1).
constexpr std::intptr_t k_invalid = -1;

#ifdef _WIN32

using socklen_type = int;
using poll_fd = WSAPOLLFD;

SOCKET native_fd(std::intptr_t handle) noexcept { return static_cast<SOCKET>(handle); }

int last_error() noexcept { return ::WSAGetLastError(); }
bool is_would_block(int err) noexcept { return err == WSAEWOULDBLOCK; }
bool is_in_progress(int err) noexcept { return err == WSAEWOULDBLOCK; } // non-blocking connect
bool is_interrupted(int /*err*/) noexcept { return false; }             // Winsock retries internally

void close_native(std::intptr_t handle) noexcept { static_cast<void>(::closesocket(native_fd(handle))); }

int poll_native(poll_fd *fds, int timeout_ms) noexcept { return ::WSAPoll(fds, 1, timeout_ms); }

std::int64_t native_send(std::intptr_t handle, const void *buf, std::size_t len, int &err) noexcept {
    const auto capped = static_cast<int>(std::min<std::size_t>(len, INT_MAX));
    const int sent = ::send(native_fd(handle), static_cast<const char *>(buf), capped, 0);
    if (sent == SOCKET_ERROR) {
        err = last_error();
        return -1;
    }
    return sent;
}

std::int64_t native_recv(std::intptr_t handle, void *buf, std::size_t len, int &err) noexcept {
    const auto capped = static_cast<int>(std::min<std::size_t>(len, INT_MAX));
    const int got = ::recv(native_fd(handle), static_cast<char *>(buf), capped, 0);
    if (got == SOCKET_ERROR) {
        err = last_error();
        return -1;
    }
    return got;
}

bool set_nonblocking_native(std::intptr_t handle, bool enabled, int &err) noexcept {
    u_long mode = enabled ? 1UL : 0UL;
    if (::ioctlsocket(native_fd(handle), static_cast<long>(FIONBIO), &mode) != 0) {
        err = last_error();
        return false;
    }
    return true;
}

int get_so_error(std::intptr_t handle) noexcept {
    int value = 0;
    socklen_type len = sizeof(value);
    // getsockopt takes char* on Winsock; viewing the int as bytes is the sanctioned idiom.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    static_cast<void>(::getsockopt(native_fd(handle), SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&value), &len));
    return value;
}

void set_reuse_addr(std::intptr_t handle) noexcept {
    int reuse = 1;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    static_cast<void>(::setsockopt(native_fd(handle), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse),
                                   sizeof(reuse)));
}

void configure_new_socket(std::intptr_t /*handle*/) noexcept {} // no SIGPIPE on Windows

void ensure_started() noexcept {
    static std::once_flag once;
    // Process-lifetime Winsock: WSAStartup is refcounted and its state is released at process
    // exit, so we never WSACleanup — this sidesteps refcount races across socket moves. Windows
    // only; the sanitizers run on the POSIX no-op path.
    std::call_once(once, []() noexcept {
        WSADATA data{};
        static_cast<void>(::WSAStartup(MAKEWORD(2, 2), &data));
    });
}

#else // POSIX

using socklen_type = socklen_t;
using poll_fd = pollfd;

#ifdef MSG_NOSIGNAL
constexpr int k_send_flags = MSG_NOSIGNAL; // Linux: suppress SIGPIPE per-call
#else
constexpr int k_send_flags = 0;
#endif

int native_fd(std::intptr_t handle) noexcept { return static_cast<int>(handle); }

int last_error() noexcept { return errno; }
bool is_would_block(int err) noexcept { return err == EWOULDBLOCK || err == EAGAIN; }
bool is_in_progress(int err) noexcept { return err == EINPROGRESS; }
bool is_interrupted(int err) noexcept { return err == EINTR; }

void close_native(std::intptr_t handle) noexcept { static_cast<void>(::close(native_fd(handle))); }

int poll_native(poll_fd *fds, int timeout_ms) noexcept { return ::poll(fds, 1, timeout_ms); }

std::int64_t native_send(std::intptr_t handle, const void *buf, std::size_t len, int &err) noexcept {
    for (;;) {
        const ssize_t sent = ::send(native_fd(handle), buf, len, k_send_flags);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = errno;
            return -1;
        }
        return sent;
    }
}

std::int64_t native_recv(std::intptr_t handle, void *buf, std::size_t len, int &err) noexcept {
    for (;;) {
        const ssize_t got = ::recv(native_fd(handle), buf, len, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = errno;
            return -1;
        }
        return got;
    }
}

bool set_nonblocking_native(std::intptr_t handle, bool enabled, int &err) noexcept {
    const int flags = ::fcntl(native_fd(handle), F_GETFL, 0); // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (flags < 0) {
        err = errno;
        return false;
    }
    const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(native_fd(handle), F_SETFL, updated) < 0) { // NOLINT(cppcoreguidelines-pro-type-vararg)
        err = errno;
        return false;
    }
    return true;
}

int get_so_error(std::intptr_t handle) noexcept {
    int value = 0;
    socklen_type len = sizeof(value);
    static_cast<void>(::getsockopt(native_fd(handle), SOL_SOCKET, SO_ERROR, &value, &len));
    return value;
}

void set_reuse_addr(std::intptr_t handle) noexcept {
    int reuse = 1;
    static_cast<void>(::setsockopt(native_fd(handle), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
}

void configure_new_socket(std::intptr_t handle) noexcept {
#ifdef SO_NOSIGPIPE // macOS/BSD: no MSG_NOSIGNAL, use the socket option instead
    int on = 1;
    static_cast<void>(::setsockopt(native_fd(handle), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)));
#else
    static_cast<void>(handle);
#endif
}

void ensure_started() noexcept {} // no per-process init on POSIX

#endif

// Polls one descriptor for `events`. Returns ready/timed_out/error purely from the poll call; on
// ready, `revents_out` holds the returned mask for the caller to interpret. `err` is set on error.
WaitResult poll_one(std::intptr_t handle, short events, int timeout_ms, short &revents_out, int &err) noexcept {
    for (;;) {
        poll_fd pfd{};
        pfd.fd = native_fd(handle);
        pfd.events = events;
        const int ready = poll_native(&pfd, timeout_ms);
        if (ready < 0) {
            const int code = last_error();
            if (is_interrupted(code)) {
                continue;
            }
            err = code;
            return WaitResult::error;
        }
        if (ready == 0) {
            return WaitResult::timed_out;
        }
        revents_out = static_cast<short>(pfd.revents);
        return WaitResult::ready;
    }
}

// Waits for a non-blocking connect to *resolve* (succeed or fail). Uses select() rather than
// poll(): WSAPoll on Windows famously does not report a failed connection, but select's
// exceptfds does, and on POSIX a resolved connect shows up in writefds either way. The caller
// then reads SO_ERROR to tell success from failure. `err` is set only on a select() error.
WaitResult wait_connect_resolved(std::intptr_t handle, int timeout_ms, int &err) noexcept {
    timeval span{};
    timeval *timeout = nullptr;
    if (timeout_ms >= 0) {
        span.tv_sec = timeout_ms / 1000;
        span.tv_usec = (timeout_ms % 1000) * 1000;
        timeout = &span;
    }
#ifdef _WIN32
    const int nfds = 0; // ignored by Winsock
#else
    const int nfds = native_fd(handle) + 1;
#endif
    for (;;) {
        fd_set writefds;
        fd_set exceptfds;
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
        FD_SET(native_fd(handle), &writefds);
        FD_SET(native_fd(handle), &exceptfds);
        const int ready = ::select(nfds, nullptr, &writefds, &exceptfds, timeout);
        if (ready < 0) {
            const int code = last_error();
            if (is_interrupted(code)) {
                continue; // select() consumed the sets; the loop rebuilds them
            }
            err = code;
            return WaitResult::error;
        }
        if (ready == 0) {
            return WaitResult::timed_out;
        }
        return WaitResult::ready;
    }
}

} // namespace

// --- TcpSocket ------------------------------------------------------------------------------

TcpSocket::TcpSocket(TcpSocket &&other) noexcept
    : handle_(other.handle_), error_(other.error_), non_blocking_(other.non_blocking_) {
    other.handle_ = invalid_handle;
}

TcpSocket &TcpSocket::operator=(TcpSocket &&other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        handle_ = other.handle_;
        error_ = other.error_;
        non_blocking_ = other.non_blocking_;
        other.handle_ = invalid_handle;
    }
    return *this;
}

TcpSocket::~TcpSocket() { static_cast<void>(close()); }

TcpSocket TcpSocket::connect(std::string_view host, std::uint16_t port, bool non_blocking) {
    ensure_started();
    TcpSocket sock;
    sock.non_blocking_ = non_blocking;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6, whichever resolves
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string host_text{host};
    const std::string port_text = std::to_string(port);
    addrinfo *raw = nullptr;
    if (::getaddrinfo(host_text.c_str(), port_text.c_str(), &hints, &raw) != 0) {
        sock.error_ = last_error();
        return sock; // closed; caller inspects is_open()/error()
    }

    int last = 0;
    for (const addrinfo *ai = raw; ai != nullptr; ai = ai->ai_next) {
        const auto fd = static_cast<std::intptr_t>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (fd == k_invalid) {
            last = last_error();
            continue;
        }
        configure_new_socket(fd);
        int err = 0;
        if (non_blocking && !set_nonblocking_native(fd, true, err)) {
            close_native(fd);
            last = err;
            continue;
        }
        const int rc = ::connect(fd, ai->ai_addr, static_cast<socklen_type>(ai->ai_addrlen));
        if (rc == 0 || is_in_progress(last_error())) {
            sock.handle_ = fd; // connected, or non-blocking connect in progress
            ::freeaddrinfo(raw);
            return sock;
        }
        last = last_error();
        close_native(fd);
    }
    ::freeaddrinfo(raw);
    sock.error_ = last;
    return sock;
}

bool TcpSocket::set_non_blocking(bool enabled) noexcept {
    if (!is_open()) {
        return false;
    }
    int err = 0;
    if (!set_nonblocking_native(handle_, enabled, err)) {
        error_ = err;
        return false;
    }
    non_blocking_ = enabled;
    return true;
}

WaitResult TcpSocket::wait_connected(int timeout_ms) noexcept {
    if (!is_open()) {
        return WaitResult::error;
    }
    int err = 0;
    const WaitResult result = wait_connect_resolved(handle_, timeout_ms, err);
    if (result != WaitResult::ready) {
        if (result == WaitResult::error) {
            error_ = err;
        }
        return result;
    }
    if (const int so = get_so_error(handle_); so != 0) {
        error_ = so;
        return WaitResult::error;
    }
    return WaitResult::ready;
}

WaitResult TcpSocket::wait_readable(int timeout_ms) noexcept {
    if (!is_open()) {
        return WaitResult::error;
    }
    short revents = 0;
    int err = 0;
    // POLLHUP/POLLERR are left for recv to surface (EOF or the concrete error), so a readable
    // event — even a half-closed peer — is reported as ready.
    const WaitResult result = poll_one(handle_, POLLIN, timeout_ms, revents, err);
    if (result == WaitResult::error) {
        error_ = err;
    }
    return result;
}

WaitResult TcpSocket::wait_writable(int timeout_ms) noexcept {
    if (!is_open()) {
        return WaitResult::error;
    }
    short revents = 0;
    int err = 0;
    const WaitResult result = poll_one(handle_, POLLOUT, timeout_ms, revents, err);
    if (result != WaitResult::ready) {
        if (result == WaitResult::error) {
            error_ = err;
        }
        return result;
    }
    if ((revents & (POLLERR | POLLNVAL)) != 0) {
        const int so = get_so_error(handle_);
        error_ = (so != 0) ? so : error_;
        return WaitResult::error;
    }
    return WaitResult::ready;
}

IoResult TcpSocket::send(std::span<const std::byte> src) noexcept {
    if (!is_open()) {
        return IoResult{.status = IoStatus::error, .bytes = 0};
    }
    int err = 0;
    const std::int64_t n = native_send(handle_, src.data(), src.size(), err);
    if (n < 0) {
        if (is_would_block(err)) {
            return IoResult{.status = IoStatus::would_block, .bytes = 0};
        }
        error_ = err;
        return IoResult{.status = IoStatus::error, .bytes = 0};
    }
    return IoResult{.status = IoStatus::ok, .bytes = static_cast<std::size_t>(n)};
}

IoResult TcpSocket::send(std::string_view text) noexcept {
    return send(std::as_bytes(std::span<const char>(text.data(), text.size())));
}

IoResult TcpSocket::recv(std::span<std::byte> dst) noexcept {
    if (!is_open()) {
        return IoResult{.status = IoStatus::error, .bytes = 0};
    }
    int err = 0;
    const std::int64_t n = native_recv(handle_, dst.data(), dst.size(), err);
    if (n < 0) {
        if (is_would_block(err)) {
            return IoResult{.status = IoStatus::would_block, .bytes = 0};
        }
        error_ = err;
        return IoResult{.status = IoStatus::error, .bytes = 0};
    }
    if (n == 0) {
        return IoResult{.status = IoStatus::closed, .bytes = 0};
    }
    return IoResult{.status = IoStatus::ok, .bytes = static_cast<std::size_t>(n)};
}

bool TcpSocket::shutdown_write() noexcept {
    if (!is_open()) {
        return false;
    }
#ifdef _WIN32
    constexpr int how = SD_SEND;
#else
    constexpr int how = SHUT_WR;
#endif
    if (::shutdown(native_fd(handle_), how) != 0) {
        error_ = last_error();
        return false;
    }
    return true;
}

bool TcpSocket::close() noexcept {
    if (!is_open()) {
        return true;
    }
    close_native(handle_);
    handle_ = invalid_handle;
    return true;
}

// --- TcpServer ------------------------------------------------------------------------------

TcpServer::TcpServer(TcpServer &&other) noexcept
    : handle_(other.handle_), error_(other.error_), local_port_(other.local_port_), non_blocking_(other.non_blocking_) {
    other.handle_ = invalid_handle;
    other.local_port_ = 0;
}

TcpServer &TcpServer::operator=(TcpServer &&other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        handle_ = other.handle_;
        error_ = other.error_;
        local_port_ = other.local_port_;
        non_blocking_ = other.non_blocking_;
        other.handle_ = invalid_handle;
        other.local_port_ = 0;
    }
    return *this;
}

TcpServer::~TcpServer() { static_cast<void>(close()); }

TcpServer TcpServer::listen(std::uint16_t port, int backlog, bool non_blocking) {
    ensure_started();
    TcpServer server;
    server.non_blocking_ = non_blocking;

    const auto fd = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (fd == k_invalid) {
        server.error_ = last_error();
        return server;
    }
    set_reuse_addr(fd);
    int err = 0;
    if (non_blocking && !set_nonblocking_native(fd, true, err)) {
        server.error_ = err;
        close_native(fd);
        return server;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    // The bytes at &addr are a live sockaddr_in; viewing them as the base sockaddr is the
    // sanctioned sockets idiom (cf. logger's UDP sink).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (::bind(native_fd(fd), reinterpret_cast<const sockaddr *>(&addr), static_cast<socklen_type>(sizeof(addr))) !=
        0) {
        server.error_ = last_error();
        close_native(fd);
        return server;
    }
    if (::listen(native_fd(fd), backlog) != 0) {
        server.error_ = last_error();
        close_native(fd);
        return server;
    }

    // Read back the bound port (an ephemeral :0 request is now a concrete port).
    sockaddr_in bound{};
    socklen_type len = sizeof(bound);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (::getsockname(native_fd(fd), reinterpret_cast<sockaddr *>(&bound), &len) == 0) {
        server.local_port_ = ntohs(bound.sin_port);
    } else {
        server.local_port_ = port;
    }

    server.handle_ = fd;
    return server;
}

WaitResult TcpServer::wait_readable(int timeout_ms) noexcept {
    if (!is_open()) {
        return WaitResult::error;
    }
    short revents = 0;
    int err = 0;
    const WaitResult result = poll_one(handle_, POLLIN, timeout_ms, revents, err);
    if (result == WaitResult::error) {
        error_ = err;
    }
    return result;
}

std::optional<TcpSocket> TcpServer::accept() noexcept {
    if (!is_open()) {
        return std::nullopt;
    }
    error_ = 0;
    for (;;) {
        const auto fd = static_cast<std::intptr_t>(::accept(native_fd(handle_), nullptr, nullptr));
        if (fd == k_invalid) {
            const int code = last_error();
            if (is_interrupted(code)) {
                continue;
            }
            if (!is_would_block(code)) {
                error_ = code; // a real failure; would-block leaves error_ == 0 (transient)
            }
            return std::nullopt;
        }
        // A POSIX accept()ed socket does not inherit the listener's non-blocking flag; set it
        // explicitly so the returned socket matches the listener's mode.
        int err = 0;
        if (!set_nonblocking_native(fd, non_blocking_, err)) {
            close_native(fd);
            error_ = err;
            return std::nullopt;
        }
        return TcpSocket{fd, non_blocking_};
    }
}

bool TcpServer::close() noexcept {
    if (!is_open()) {
        return true;
    }
    close_native(handle_);
    handle_ = invalid_handle;
    local_port_ = 0;
    return true;
}

} // namespace it::d4np::util
