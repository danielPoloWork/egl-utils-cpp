// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Compiled tier of Logger (component #20, ADR-0020). This translation unit owns the pump
// thread, the flush-marker queue protocol, the UTC line rendering, and the whole OS-API
// surface — Winsock/BSD sockets for the UDP sink and the per-platform thread-id calls —
// so those headers never reach consumers. Producers and the pump synchronize through one
// monitor (mutex + three condition variables); sink calls run outside the lock.
#include <it/d4np/util/logger.hpp>

#include <it/d4np/util/string_builder.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#ifdef _WIN32
// Keep windows.h from defining the min/max macros (they break std::min) and from dragging
// in the rarely-needed subsystem headers. This is a .cpp, so the defines leak to no one.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// winsock2.h must come before windows.h so the legacy winsock.h can never be dragged in.
#include <winsock2.h>
// ws2tcpip.h (getaddrinfo, AI_NUMERICHOST) requires winsock2.h first; keep this order.
#include <ws2tcpip.h>

#include <windows.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif
#endif

namespace it::d4np::util {

static_assert(std::is_nothrow_move_constructible_v<LogRecord>,
              "Logger: records must be nothrow-move-constructible so queue operations stay exception-safe");

namespace {

constexpr std::intptr_t invalid_socket_handle = -1;

/// Appends `value` zero-padded to `width` digits (tm fields: always non-negative here).
void append_padded(StringBuilder &out, int value, std::size_t width) {
    std::array<char, 16> digits{};
    char *const first = digits.data();
    char *const last = std::next(first, static_cast<std::ptrdiff_t>(digits.size()));
    const std::to_chars_result result = std::to_chars(first, last, value);
    if (result.ec != std::errc{}) {
        return; // unreachable: 16 chars fit any int
    }
    const auto length = static_cast<std::size_t>(std::distance(first, result.ptr));
    for (std::size_t i = length; i < width; ++i) {
        out.append('0');
    }
    out.append(std::string_view(first, length));
}

/// Renders the standard line: "[YYYY-MM-DD HH:MM:SS.mmm] [level] [thread] message" (UTC).
std::string render_line(const LogRecord &record) {
    // One floor for both fields, so the second and its milliseconds never disagree (and
    // the millisecond stays in [0, 999] even for pre-epoch times).
    const auto second_point = std::chrono::floor<std::chrono::seconds>(record.time);
    const auto millisecond = std::chrono::duration_cast<std::chrono::milliseconds>(record.time - second_point).count();
    const std::time_t epoch_seconds = std::chrono::system_clock::to_time_t(second_point);

    std::tm utc{};
#ifdef _WIN32
    // MSVC signature — errno_t gmtime_s(tm*, const time_t*) — not the C11 Annex K form.
    const bool decomposed = gmtime_s(&utc, &epoch_seconds) == 0;
#else
    const bool decomposed = gmtime_r(&epoch_seconds, &utc) != nullptr;
#endif

    constexpr std::size_t header_estimate = 48; // timestamp + level + thread id + separators
    StringBuilder out{header_estimate + record.message.size()};
    out.append('[');
    if (decomposed) {
        append_padded(out, utc.tm_year + 1900, 4);
        out.append('-');
        append_padded(out, utc.tm_mon + 1, 2);
        out.append('-');
        append_padded(out, utc.tm_mday, 2);
        out.append(' ');
        append_padded(out, utc.tm_hour, 2);
        out.append(':');
        append_padded(out, utc.tm_min, 2);
        out.append(':');
        append_padded(out, utc.tm_sec, 2);
        out.append('.');
        append_padded(out, static_cast<int>(millisecond), 3);
    } else {
        // MSVC's gmtime_s rejects pre-1970 times: fall back to raw epoch seconds rather
        // than read a tm it never filled in.
        out.append(static_cast<std::int64_t>(epoch_seconds)).append('s');
    }
    out.append("] [").append(level_name(record.level)).append("] [").append(record.thread_id).append("] ");
    out.append(std::string_view{record.message});
    return std::move(out).str();
}

#ifdef _WIN32

void close_socket(std::intptr_t handle) noexcept { static_cast<void>(::closesocket(static_cast<SOCKET>(handle))); }

/// True when the whole payload left as one datagram.
bool send_datagram(std::intptr_t handle, std::string_view payload, const unsigned char *address,
                   int address_length) noexcept {
    // The bytes at `address` were memcpy'd from a live sockaddr, so viewing them as one is
    // the sanctioned sockets idiom — the banned-cast rule meets its legitimate exception
    // at the OS boundary (the stack_trace.cpp precedent).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *destination = reinterpret_cast<const sockaddr *>(address);
    return ::sendto(static_cast<SOCKET>(handle), payload.data(), static_cast<int>(payload.size()), 0, destination,
                    address_length) != SOCKET_ERROR;
}

std::uint64_t compute_thread_id() noexcept { return static_cast<std::uint64_t>(GetCurrentThreadId()); }

#else

void close_socket(std::intptr_t handle) noexcept { static_cast<void>(::close(static_cast<int>(handle))); }

/// True when the whole payload left as one datagram.
bool send_datagram(std::intptr_t handle, std::string_view payload, const unsigned char *address,
                   int address_length) noexcept {
    // The bytes at `address` were memcpy'd from a live sockaddr, so viewing them as one is
    // the sanctioned sockets idiom — the banned-cast rule meets its legitimate exception
    // at the OS boundary (the stack_trace.cpp precedent).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *destination = reinterpret_cast<const sockaddr *>(address);
    return ::sendto(static_cast<int>(handle), payload.data(), payload.size(), 0, destination,
                    static_cast<socklen_t>(address_length)) >= 0;
}

std::uint64_t compute_thread_id() noexcept {
#ifdef __linux__
    // The raw syscall avoids the glibc >= 2.30 floor of the gettid(2) wrapper. syscall(2)
    // is a C vararg function by contract — the banned-vararg rule meets its legitimate
    // exception at the OS boundary.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    return static_cast<std::uint64_t>(::syscall(SYS_gettid));
#elif defined(__APPLE__)
    std::uint64_t tid = 0;
    if (::pthread_threadid_np(nullptr, &tid) != 0) {
        tid = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
    return tid;
#else
    // Unknown platform: a stable per-thread value, though uncorrelated with any OS id.
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

#endif

} // namespace

namespace detail {

std::uint64_t current_thread_id() noexcept {
    // The OS call is a syscall on Linux: pay it once per thread, not once per record.
    thread_local const std::uint64_t id = compute_thread_id();
    return id;
}

} // namespace detail

// Out-of-line on purpose: the vtable anchor that ties every sink to the compiled tier.
Sink::~Sink() = default;

// --- ConsoleSink ----------------------------------------------------------------------------

ConsoleSink::ConsoleSink(ConsoleStream stream) : target_(stream == ConsoleStream::standard_error ? stderr : stdout) {}

void ConsoleSink::write(const LogRecord & /*record*/, std::string_view line) {
    if (std::fwrite(line.data(), 1, line.size(), target_) != line.size() || std::fputc('\n', target_) == EOF) {
        throw std::runtime_error("ConsoleSink: write failed");
    }
}

void ConsoleSink::flush() {
    if (std::fflush(target_) != 0) {
        throw std::runtime_error("ConsoleSink: flush failed");
    }
}

// --- FileSink -------------------------------------------------------------------------------

FileSink::FileSink(const std::string &path, bool append)
    // Binary on purpose: log bytes stay identical on every platform (no CRLF translation).
    : stream_(path, std::ios::out | std::ios::binary | (append ? std::ios::app : std::ios::trunc)) {
    if (!stream_.is_open()) {
        throw std::runtime_error(StringFormatter::format("FileSink: cannot open '{}'", path));
    }
}

void FileSink::write(const LogRecord & /*record*/, std::string_view line) {
    if (!stream_.write(line.data(), static_cast<std::streamsize>(line.size())).put('\n')) {
        throw std::runtime_error("FileSink: write failed");
    }
}

void FileSink::flush() {
    if (!stream_.flush()) {
        throw std::runtime_error("FileSink: flush failed");
    }
}

// --- UdpSink --------------------------------------------------------------------------------

#ifdef _WIN32

UdpSink::WsaSession::WsaSession() {
    WSADATA data{};
    if (const int status = ::WSAStartup(MAKEWORD(2, 2), &data); status != 0) {
        throw std::runtime_error(StringFormatter::format("UdpSink: WSAStartup failed (error {})", status));
    }
}

UdpSink::WsaSession::~WsaSession() { ::WSACleanup(); }

#else

UdpSink::WsaSession::WsaSession() = default;

// Out-of-line on purpose: the Windows build does real work (WSACleanup) in this destructor,
// and the class must keep one ABI across platforms.
// NOLINTNEXTLINE(performance-trivially-destructible)
UdpSink::WsaSession::~WsaSession() = default;

#endif

UdpSink::UdpSink(std::string_view host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;                      // IPv4 and IPv6 literals both resolve
    hints.ai_socktype = SOCK_DGRAM;                   // one addrinfo entry per address,
    hints.ai_protocol = IPPROTO_UDP;                  // not one per socket type
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV; // literals only: never DNS, never blocks

    const std::string host_text{host};
    const std::string port_text = StringFormatter::format("{}", port);
    addrinfo *raw = nullptr;
    const int status = ::getaddrinfo(host_text.c_str(), port_text.c_str(), &hints, &raw);
    if (status != 0) {
        // The numeric code is embedded instead of gai_strerror text: Windows' gai_strerror
        // renders into a static buffer and is not thread-safe.
        throw std::invalid_argument(StringFormatter::format(
            "UdpSink: '{}' is not a numeric host literal (getaddrinfo error {})", host_text, status));
    }
    // Adopt the list under RAII immediately (the __cxa_demangle idiom of stack_trace.cpp).
    const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> result{raw, &::freeaddrinfo};
    if (static_cast<std::size_t>(result->ai_addrlen) > address_.size()) {
        throw std::runtime_error("UdpSink: resolved address exceeds sockaddr_storage");
    }
    std::memcpy(address_.data(), result->ai_addr, static_cast<std::size_t>(result->ai_addrlen));
    address_length_ = static_cast<int>(result->ai_addrlen);

    // The socket is created last — nothing after it can throw, so the handle cannot leak
    // (on the throw paths above, completed members are destroyed: the Winsock session is
    // released and there is no socket yet).
    socket_ = static_cast<std::intptr_t>(::socket(result->ai_family, result->ai_socktype, result->ai_protocol));
    if (socket_ == invalid_socket_handle) {
        throw std::runtime_error("UdpSink: cannot create a UDP socket");
    }
}

UdpSink::~UdpSink() {
    if (socket_ != invalid_socket_handle) {
        close_socket(socket_);
    }
}

void UdpSink::write(const LogRecord & /*record*/, std::string_view line) {
    // One record, one datagram; no trailing newline (the datagram is the frame). Loss in
    // transit is invisible by design — only a local failure (oversized datagram, downed
    // stack) surfaces, and the pump absorbs and counts it.
    if (!send_datagram(socket_, line, address_.data(), address_length_)) {
        throw std::runtime_error("UdpSink: sendto failed");
    }
}

void UdpSink::flush() {
    // Nothing buffered: every write already left as a datagram.
}

// --- Logger ---------------------------------------------------------------------------------

Logger::Logger(std::vector<UniqueRef<Sink>> sinks, std::size_t queue_capacity, OverflowPolicy policy)
    : sinks_(std::move(sinks)), capacity_(queue_capacity), policy_(policy) {
    if (sinks_.empty()) {
        throw std::invalid_argument("Logger: at least one sink is required");
    }
    if (capacity_ == 0) {
        throw std::invalid_argument("Logger: queue capacity must be positive");
    }
    // Started last: nothing after this line may throw, so a completed constructor always
    // owns a joinable pump and a throwing constructor never leaks one.
    pump_ = std::thread([this] { pump_loop(); });
    // Cached so the reentrancy guards never touch pump_ itself: get_id() on the thread
    // object would race with the join() in shutdown().
    pump_id_ = pump_.get_id();
}

Logger::~Logger() { shutdown(); }

void Logger::shutdown() {
    {
        const std::scoped_lock lock{mutex_};
        stopping_ = true;
    }
    records_available_.notify_all(); // wake the pump for the final drain
    space_available_.notify_all();   // wake block-policy producers so they refuse loudly
    if (pump_.joinable()) {
        pump_.join();
    }
}

void Logger::flush() {
    if (std::this_thread::get_id() == pump_id_) {
        throw std::logic_error("Logger: flush from inside a sink would deadlock");
    }
    std::unique_lock<std::mutex> lock{mutex_};
    if (stopping_) {
        return; // shutdown already drained the queue and flushed every sink
    }
    const std::uint64_t ticket = ++flush_requested_;
    // The marker rides the queue itself, so the ticket completes exactly when everything
    // enqueued before it has been written — bounded by the queue depth at call time even
    // under sustained logging. Markers bypass the capacity bound: rare, tiny, and a
    // blocked flush() could never be released by the pump it is about to wait for.
    queue_.emplace_back(FlushMarker{ticket});
    records_available_.notify_one(); // notified under the lock: this thread waits on the same mutex next
    flush_done_.wait(lock, [this, ticket] { return flush_completed_ >= ticket; });
}

void Logger::enqueue(LogRecord record) {
    {
        std::unique_lock<std::mutex> lock{mutex_};
        if (stopping_) {
            throw std::logic_error("Logger: log after shutdown");
        }
        if (queue_.size() >= capacity_) {
            if (policy_ == OverflowPolicy::drop) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (std::this_thread::get_id() == pump_id_) {
                throw std::logic_error("Logger: log from inside a sink would deadlock on a full queue");
            }
            space_available_.wait(lock, [this] { return stopping_ || queue_.size() < capacity_; });
            if (stopping_) {
                // Shutdown began while this call was waiting for space; the pump no longer
                // guarantees delivery, so refuse loudly rather than lose the record silently.
                throw std::logic_error("Logger: log after shutdown");
            }
        }
        queue_.emplace_back(std::move(record));
    }
    records_available_.notify_one();
}

void Logger::pump_loop() {
    for (;;) {
        QueueItem item;
        {
            std::unique_lock<std::mutex> lock{mutex_};
            records_available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                break; // stopping and fully drained
            }
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        space_available_.notify_one(); // one slot freed (outside the lock)
        if (const FlushMarker *marker = std::get_if<FlushMarker>(&item)) {
            flush_sinks();
            {
                const std::scoped_lock lock{mutex_};
                flush_completed_ = std::max(flush_completed_, marker->ticket);
            }
            flush_done_.notify_all(); // several tickets may complete at once
            continue;
        }
        deliver(std::get<LogRecord>(item));
    }
    // Drained on shutdown: every accepted record is written. Leave the sinks flushed and
    // complete every outstanding ticket so no flush() caller is left stranded.
    flush_sinks();
    {
        const std::scoped_lock lock{mutex_};
        flush_completed_ = flush_requested_;
    }
    flush_done_.notify_all();
}

void Logger::deliver(const LogRecord &record) {
    std::string line;
    try {
        line = render_line(record);
    } catch (...) {
        // Rendering failed (allocation): count it and keep pumping — a logger must never
        // take the process down.
        sink_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    for (const UniqueRef<Sink> &sink : sinks_) {
        try {
            sink->write(record, line);
        } catch (...) {
            // A failing sink must not kill the pump or starve its siblings; the failure
            // stays observable through sink_failures().
            sink_failures_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void Logger::flush_sinks() noexcept {
    for (const UniqueRef<Sink> &sink : sinks_) {
        try {
            sink->flush();
        } catch (...) {
            sink_failures_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

} // namespace it::d4np::util
