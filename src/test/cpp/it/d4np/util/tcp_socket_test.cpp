// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for TcpSocket / TcpServer (component #17, roadmap 9.3). Exercise a loopback round-trip
// over an OS-assigned ephemeral port, the non-blocking boundary (would_block on an empty recv),
// orderly shutdown (recv sees closed), a refused connect (wait_connected reports error), the
// move/RAII lifecycle, and closed-socket graceful failure. Single-threaded: the kernel completes
// the loopback handshake while the listener polls, so no worker thread is needed. Requires the
// compiled STATIC tier.
#include <doctest/doctest.h>

#include <it/d4np/util/tcp_socket.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using it::d4np::util::IoStatus;
using it::d4np::util::TcpServer;
using it::d4np::util::TcpSocket;
using it::d4np::util::WaitResult;

constexpr int timeout_ms = 2000; // generous: loopback is fast, CI is not

// Accepts the next pending connection, failing the test if none arrives in time. The guarded
// deref (`!maybe ? ... : *std::move(maybe)`) keeps clang-tidy's optional-access analysis happy.
TcpSocket accept_one(TcpServer &server) {
    REQUIRE(server.wait_readable(timeout_ms) == WaitResult::ready);
    std::optional<TcpSocket> maybe = server.accept();
    REQUIRE(maybe.has_value());
    return (!maybe) ? TcpSocket{} : *std::move(maybe);
}

// Receives exactly `n` bytes (looping over would_block / partial reads) and returns them as text.
std::string recv_text(TcpSocket &sock, std::size_t n) {
    std::string out;
    std::array<std::byte, 256> buf{};
    while (out.size() < n) {
        if (sock.wait_readable(timeout_ms) != WaitResult::ready) {
            break;
        }
        const std::size_t want = std::min(n - out.size(), buf.size());
        const auto result = sock.recv(std::span<std::byte>(buf).first(want));
        if (result.status == IoStatus::would_block) {
            continue;
        }
        if (result.status != IoStatus::ok) {
            break; // closed or error
        }
        for (std::size_t i = 0; i < result.bytes; ++i) {
            out.push_back(static_cast<char>(buf[i]));
        }
    }
    return out;
}

// Sends all of `text`, looping over partial sends / would_block. Returns whether it all left.
bool send_all(TcpSocket &sock, std::string_view text) {
    std::size_t sent = 0;
    while (sent < text.size()) {
        const auto result = sock.send(text.substr(sent));
        if (result.status == IoStatus::would_block) {
            if (sock.wait_writable(timeout_ms) != WaitResult::ready) {
                return false;
            }
            continue;
        }
        if (result.status != IoStatus::ok) {
            return false;
        }
        sent += result.bytes;
    }
    return true;
}

} // namespace

TEST_CASE("listen assigns an ephemeral port and reports it") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());
    CHECK(server.error() == 0);
    CHECK(server.local_port() != 0);
    CHECK(server.non_blocking());
}

TEST_CASE("connected pair exchanges bytes over loopback in both directions") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());

    TcpSocket client = TcpSocket::connect("127.0.0.1", server.local_port());
    REQUIRE(client.is_open());

    TcpSocket peer = accept_one(server);
    REQUIRE(peer.is_open());
    REQUIRE(client.wait_connected(timeout_ms) == WaitResult::ready);
    CHECK(client.error() == 0);

    // client -> peer
    REQUIRE(send_all(client, "hello"));
    CHECK(recv_text(peer, 5) == "hello");

    // peer -> client
    REQUIRE(send_all(peer, "world!"));
    CHECK(recv_text(client, 6) == "world!");
}

TEST_CASE("recv on a non-blocking socket with no data reports would_block") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());
    TcpSocket client = TcpSocket::connect("127.0.0.1", server.local_port());
    REQUIRE(client.is_open());
    TcpSocket peer = accept_one(server);
    REQUIRE(client.wait_connected(timeout_ms) == WaitResult::ready);

    std::array<std::byte, 16> buf{};
    const auto result = peer.recv(buf);
    CHECK(result.status == IoStatus::would_block);
    CHECK(result.bytes == 0);
    CHECK(peer.error() == 0); // would_block is not an error
}

TEST_CASE("an orderly peer shutdown surfaces as recv status closed") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());
    TcpSocket client = TcpSocket::connect("127.0.0.1", server.local_port());
    REQUIRE(client.is_open());
    TcpSocket peer = accept_one(server);
    REQUIRE(client.wait_connected(timeout_ms) == WaitResult::ready);

    CHECK(client.shutdown_write()); // send FIN

    REQUIRE(peer.wait_readable(timeout_ms) == WaitResult::ready);
    std::array<std::byte, 16> buf{};
    const auto result = peer.recv(buf);
    CHECK(result.status == IoStatus::closed);
    CHECK(result.bytes == 0);
}

TEST_CASE("connecting to a port with no listener never reports a successful connection") {
    // Bind then close to obtain a port nobody is listening on.
    std::uint16_t dead_port = 0;
    {
        TcpServer server = TcpServer::listen(0);
        REQUIRE(server.is_open());
        dead_port = server.local_port();
    } // server closed here

    TcpSocket client = TcpSocket::connect("127.0.0.1", dead_port);
    if (!client.is_open()) {
        CHECK(client.error() != 0); // some stacks fail the connect immediately
    } else {
        // A non-blocking connect was initiated. It must never resolve as ready: on stacks that
        // surface loopback refusal it reports error (with a code); on stacks that don't (Windows
        // loopback), it simply never becomes writable. Either way, never a false success.
        const WaitResult result = client.wait_connected(1000);
        CHECK(result != WaitResult::ready);
        if (result == WaitResult::error) {
            CHECK(client.error() != 0);
        }
    }
}

TEST_CASE("move transfers ownership and leaves the source closed") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());
    const std::uint16_t port = server.local_port();

    TcpServer moved_server = std::move(server);
    CHECK_FALSE(server.is_open()); // NOLINT(bugprone-use-after-move) — checking the moved-from state
    CHECK(moved_server.is_open());
    CHECK(moved_server.local_port() == port);

    TcpSocket client = TcpSocket::connect("127.0.0.1", port);
    REQUIRE(client.is_open());
    TcpSocket moved_client = std::move(client);
    CHECK_FALSE(client.is_open()); // NOLINT(bugprone-use-after-move)
    CHECK(moved_client.is_open());

    TcpSocket peer = accept_one(moved_server);
    CHECK(peer.is_open());
}

TEST_CASE("a closed socket fails gracefully without throwing") {
    TcpSocket sock; // default: closed
    CHECK_FALSE(sock.is_open());

    std::array<std::byte, 8> buf{};
    CHECK(sock.recv(buf).status == IoStatus::error);
    CHECK(sock.send(std::string_view{"x"}).status == IoStatus::error);
    CHECK(sock.wait_readable(0) == WaitResult::error);
    CHECK(sock.wait_writable(0) == WaitResult::error);
    CHECK(sock.wait_connected(0) == WaitResult::error);
    CHECK_FALSE(sock.set_non_blocking(true));
    CHECK_FALSE(sock.shutdown_write());
    CHECK(sock.close()); // idempotent no-op
}

TEST_CASE("a closed server fails gracefully without throwing") {
    TcpServer server; // default: closed
    CHECK_FALSE(server.is_open());
    CHECK(server.local_port() == 0);
    CHECK(server.wait_readable(0) == WaitResult::error);
    const std::optional<TcpSocket> none = server.accept();
    CHECK_FALSE(none.has_value());
    CHECK(server.close()); // idempotent no-op
}

TEST_CASE("a connect that fails immediately reports a closed socket") {
    // Port 0 is not connectable, so ::connect fails at once (not "in progress") for every
    // resolved address — exercising the connect loop's hard-failure path.
    TcpSocket client = TcpSocket::connect("127.0.0.1", 0);
    CHECK_FALSE(client.is_open());
    CHECK(client.error() != 0);
}

TEST_CASE("send reports would_block once the kernel send buffer fills") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());
    TcpSocket client = TcpSocket::connect("127.0.0.1", server.local_port());
    REQUIRE(client.is_open());
    TcpSocket peer = accept_one(server);
    REQUIRE(client.wait_connected(timeout_ms) == WaitResult::ready);

    // The peer never reads, so a non-blocking client eventually cannot enqueue more.
    const std::vector<std::byte> block(std::size_t{64} * 1024);
    IoStatus last = IoStatus::ok;
    for (int i = 0; i < 4096 && last == IoStatus::ok; ++i) {
        last = client.send(block).status;
    }
    CHECK(last == IoStatus::would_block);
    CHECK(client.error() == 0); // would_block is not an error
}

TEST_CASE("a connected socket is writable, toggles blocking mode, and move-assigns") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());
    TcpSocket client = TcpSocket::connect("127.0.0.1", server.local_port());
    REQUIRE(client.is_open());
    TcpSocket peer = accept_one(server);
    REQUIRE(client.wait_connected(timeout_ms) == WaitResult::ready);

    CHECK(client.wait_writable(timeout_ms) == WaitResult::ready);

    CHECK(client.set_non_blocking(false));
    CHECK_FALSE(client.non_blocking());
    CHECK(client.set_non_blocking(true));
    CHECK(client.non_blocking());

    TcpSocket adopted;
    adopted = std::move(client); // move-assignment adopts the connected descriptor
    CHECK(adopted.is_open());
    CHECK_FALSE(client.is_open()); // NOLINT(bugprone-use-after-move)
    CHECK(adopted.wait_writable(timeout_ms) == WaitResult::ready);
}

TEST_CASE("an idle listener times out, accepts nothing, and move-assigns") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());

    // No client has connected: wait_readable times out and accept would-block.
    CHECK(server.wait_readable(50) == WaitResult::timed_out);
    const std::optional<TcpSocket> none = server.accept();
    CHECK_FALSE(none.has_value());
    CHECK(server.error() == 0); // a would-block is not an error

    const std::uint16_t port = server.local_port();
    TcpServer adopted;
    adopted = std::move(server); // move-assignment adopts the listening descriptor
    CHECK(adopted.is_open());
    CHECK_FALSE(server.is_open()); // NOLINT(bugprone-use-after-move)
    CHECK(adopted.local_port() == port);
}

TEST_CASE("connecting to an unresolvable host fails cleanly") {
    // The .invalid TLD (RFC 6761) never resolves, so getaddrinfo fails.
    TcpSocket client = TcpSocket::connect("no-such-host.invalid", 80);
    CHECK_FALSE(client.is_open());
    CHECK(client.error() != 0);
}

TEST_CASE("a blocking-mode socket completes a round trip") {
    TcpServer server = TcpServer::listen(0);
    REQUIRE(server.is_open());
    TcpSocket client = TcpSocket::connect("127.0.0.1", server.local_port(), /*non_blocking=*/false);
    REQUIRE(client.is_open());
    TcpSocket peer = accept_one(server); // inherits the listener's (non-blocking) mode

    REQUIRE(send_all(peer, "blocking"));
    std::array<std::byte, 16> buf{};
    const auto got = client.recv(buf); // blocking: returns once the bytes above arrive
    REQUIRE(got.status == IoStatus::ok);
    CHECK(got.bytes >= 1);
}
