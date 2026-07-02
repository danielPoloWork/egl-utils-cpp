// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Logger (component #20): an asynchronous multi-sink logger — the Milestone 7 closer.
// Call sites format eagerly through the StringFormatter machinery (compile-time validated
// format strings), records cross a bounded monitor-guarded queue, and one dedicated pump
// thread renders each record once and fans it out to every sink (console, file, UDP — a
// Strategy hierarchy). Declarations and the templated hot path live here; every other
// definition is compiled into the optional STATIC tier (egl-util::egl-util-static,
// ADR-0004), so the socket headers (<winsock2.h>, <netdb.h>) never leak into consumer
// translation units. See ADR-0020.
#ifndef IT_D4NP_UTIL_LOGGER_HPP
#define IT_D4NP_UTIL_LOGGER_HPP

#include <it/d4np/util/string_formatter.hpp>
#include <it/d4np/util/unique_ref.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace it::d4np::util {

namespace detail {

/// The OS thread id of the calling thread, cached per thread (`GetCurrentThreadId`,
/// `gettid`, `pthread_threadid_np`). Records carry it so log lines correlate with
/// debugger and OS tooling ids. Compiled into the static tier (ADR-0004).
[[nodiscard]] std::uint64_t current_thread_id() noexcept;

} // namespace detail

/// Severity levels, ordered: a record is delivered when its level is at least the
/// logger's filter level (see Logger::set_level). `off` is a filter threshold only —
/// logging *at* `off` is diagnosed with std::invalid_argument.
enum class LogLevel : std::uint8_t {
    trace = 0,
    debug = 1,
    info = 2,
    warning = 3,
    error = 4,
    critical = 5,
    off = 6 ///< filter threshold that silences the logger; not a loggable level
};

/// The lowercase name of `level` ("trace" … "critical", "off"); "unknown" for a value
/// outside the enumeration (defensive: levels normally arrive type-checked).
[[nodiscard]] constexpr std::string_view level_name(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::trace:
        return "trace";
    case LogLevel::debug:
        return "debug";
    case LogLevel::info:
        return "info";
    case LogLevel::warning:
        return "warning";
    case LogLevel::error:
        return "error";
    case LogLevel::critical:
        return "critical";
    case LogLevel::off:
        return "off";
    }
    return "unknown";
}

/// One log event, captured at the call site and delivered to every sink by the pump.
struct LogRecord {
    std::chrono::system_clock::time_point time; ///< wall-clock capture time (rendered as UTC)
    LogLevel level = LogLevel::trace;           ///< severity the record was logged at
    std::uint64_t thread_id = 0;                ///< OS id of the logging thread
    std::string message;                        ///< the fully formatted message text
};

/// Where rendered records go — the strategy the logger fans out to (ADR-0020).
///
/// `write` receives both the structured record and the standard rendered line (rendered
/// once per record, shared by every sink), so a custom sink may consume either. Both
/// members are invoked from the owning logger's pump thread only, one call at a time —
/// implementations need no internal locking. A sink that throws is absorbed and counted
/// (Logger::sink_failures()); it never kills the pump.
///
/// @note A sink must not call back into the logger that owns it: `flush()` — and a `log()`
/// that has to wait for queue space — would deadlock, so both are diagnosed with
/// std::logic_error when invoked from the pump thread. Non-copyable and non-movable.
class Sink {
  public:
    Sink() = default;
    Sink(const Sink &) = delete;
    Sink &operator=(const Sink &) = delete;
    Sink(Sink &&) = delete;
    Sink &operator=(Sink &&) = delete;
    virtual ~Sink(); // defined out-of-line: anchors the vtable in the compiled tier

    /// Emits one record. `line` is the standard rendering of `record`.
    virtual void write(const LogRecord &record, std::string_view line) = 0;

    /// Pushes buffered output to its destination.
    virtual void flush() = 0;
};

/// Selects the stream a ConsoleSink writes to.
enum class ConsoleStream : std::uint8_t { standard_output, standard_error };

/// A console sink: one line per record to stdout or stderr.
///
/// @note `write`/`flush` throw std::runtime_error when the C stream reports failure
/// (absorbed and counted by the owning logger).
class ConsoleSink final : public Sink {
  public:
    /// Targets standard output by default.
    explicit ConsoleSink(ConsoleStream stream = ConsoleStream::standard_output);

    void write(const LogRecord &record, std::string_view line) override;
    void flush() override;

  private:
    std::FILE *target_; ///< not owned: stdout/stderr live for the whole process
};

/// A file sink: one line per record through a buffered stream.
///
/// The file opens in binary mode so log bytes are identical on every platform (no CRLF
/// translation on Windows). Buffering is the stream's; `flush()` forces it out.
///
/// @note `write`/`flush` throw std::runtime_error once the stream fails (full disk,
/// removed directory); the owning logger absorbs and counts those failures.
class FileSink final : public Sink {
  public:
    /// Opens `path` for appending (default) or truncating.
    /// @throws std::runtime_error if the file cannot be opened.
    explicit FileSink(const std::string &path, bool append = true);

    void write(const LogRecord &record, std::string_view line) override;
    void flush() override;

  private:
    std::ofstream stream_;
};

/// A UDP sink: each rendered line leaves as one datagram to a fixed endpoint.
///
/// The host must be a numeric IPv4/IPv6 literal ("127.0.0.1", "::1"): resolution runs
/// with `AI_NUMERICHOST | AI_NUMERICSERV`, so construction never consults DNS and cannot
/// block — hostname resolution belongs to the Milestone-9 socket work (ADR-0020).
/// Datagram loss in transit is undetectable by design; a *local* send failure throws
/// std::runtime_error from `write`, absorbed and counted by the owning logger.
class UdpSink final : public Sink {
  public:
    /// Resolves `host:port` and opens the socket.
    /// @throws std::invalid_argument if `host` is not a numeric address literal.
    /// @throws std::runtime_error if the socket (or the Winsock session) cannot be created.
    UdpSink(std::string_view host, std::uint16_t port);

    UdpSink(const UdpSink &) = delete;
    UdpSink &operator=(const UdpSink &) = delete;
    UdpSink(UdpSink &&) = delete;
    UdpSink &operator=(UdpSink &&) = delete;

    /// Closes the socket (and, on Windows, releases the Winsock session).
    ~UdpSink() override;

    void write(const LogRecord &record, std::string_view line) override;

    /// Datagrams are not buffered: nothing to flush.
    void flush() override;

  private:
    /// Winsock session lifetime (a no-op on POSIX). Declared before the socket members:
    /// getaddrinfo/socket in the constructor need a live session, and members completed
    /// before a throwing constructor body are destroyed — so the WSAStartup refcount
    /// stays balanced even on failed construction.
    struct WsaSession {
        WsaSession();
        WsaSession(const WsaSession &) = delete;
        WsaSession &operator=(const WsaSession &) = delete;
        WsaSession(WsaSession &&) = delete;
        WsaSession &operator=(WsaSession &&) = delete;
        ~WsaSession();
    };

    /// sizeof(sockaddr_storage) on every supported platform.
    static constexpr std::size_t address_capacity = 128;

    WsaSession session_;
    std::intptr_t socket_ = -1;                                        ///< SOCKET on Windows, fd on POSIX; -1 = invalid
    alignas(8) std::array<unsigned char, address_capacity> address_{}; ///< resolved sockaddr bytes
    int address_length_ = 0;
};

/// What a producer experiences when the record queue is full.
enum class OverflowPolicy : std::uint8_t {
    block, ///< wait for space: nothing is lost, latency is unbounded (the default)
    drop   ///< refuse the incoming record and count it in Logger::dropped()
};

/// An asynchronous multi-sink logger: call sites format, a dedicated pump thread delivers.
///
/// `log(level, fmt, args...)` validates `fmt` at compile time (the StringFormatter
/// machinery, ADR-0012), filters against the runtime level *before* any formatting work,
/// then enqueues the formatted record into a bounded queue. One pump thread drains it in
/// strict FIFO order, renders each record once ("[YYYY-MM-DD HH:MM:SS.mmm] [level]
/// [thread] message", UTC), and hands it to every sink. A full queue blocks the producer
/// or drops the record, by OverflowPolicy. `shutdown()` stops admissions and **drains**:
/// every record already accepted is written and the sinks are flushed before the pump
/// joins; the destructor calls it. See ADR-0020.
///
/// @note Requires linking `egl-util::egl-util-static`; header-only consumers see these
/// declarations but calls are an unresolved-symbol error at link time (same contract as
/// `library_version()`). Thread-safe: `log`, the per-level helpers, `flush`, `set_level`,
/// `level`, `dropped`, and `sink_failures` may be called from any thread except inside a
/// sink (see Sink); `shutdown` is idempotent but must not be invoked concurrently from
/// multiple threads. Non-copyable and non-movable.
class Logger {
  public:
    /// Default bound of the record queue.
    static constexpr std::size_t default_queue_capacity = 1024;

    /// Takes ownership of `sinks` and starts the pump thread. The initial filter level
    /// is `trace`: everything logged is delivered until set_level says otherwise.
    /// @throws std::invalid_argument if `sinks` is empty or `queue_capacity` is zero.
    explicit Logger(std::vector<UniqueRef<Sink>> sinks, std::size_t queue_capacity = default_queue_capacity,
                    OverflowPolicy policy = OverflowPolicy::block);

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    /// Drains and joins (see shutdown()).
    ~Logger();

    /// Formats and enqueues one record at `level`.
    ///
    /// Records below the filter level return before any formatting work. Acceptance
    /// depends on the overflow policy: `block` waits for queue space, `drop` refuses and
    /// counts (@ref dropped).
    /// @throws std::invalid_argument if `level` is `LogLevel::off`.
    /// @throws std::logic_error once the logger is shut down — a call that passes the
    /// filter throws, including one that was still waiting for queue space when shutdown
    /// began (its record is not delivered).
    /// @throws std::logic_error when a full queue would make the pump thread wait on
    /// itself (a sink calling back into its logger under the `block` policy).
    template <Formattable... Args> void log(LogLevel level, FormatString<Args...> fmt, const Args &...args) {
        if (level == LogLevel::off) {
            throw std::invalid_argument("Logger: off is a filter threshold, not a loggable level");
        }
        if (level < min_level_.load(std::memory_order_relaxed)) {
            return; // filtered before any formatting or allocation happens
        }
        enqueue(LogRecord{.time = std::chrono::system_clock::now(),
                          .level = level,
                          .thread_id = detail::current_thread_id(),
                          .message = StringFormatter::format(fmt, args...)});
    }

    /// Logs at LogLevel::trace. See @ref log.
    template <Formattable... Args> void trace(FormatString<Args...> fmt, const Args &...args) {
        log(LogLevel::trace, fmt, args...);
    }
    /// Logs at LogLevel::debug. See @ref log.
    template <Formattable... Args> void debug(FormatString<Args...> fmt, const Args &...args) {
        log(LogLevel::debug, fmt, args...);
    }
    /// Logs at LogLevel::info. See @ref log.
    template <Formattable... Args> void info(FormatString<Args...> fmt, const Args &...args) {
        log(LogLevel::info, fmt, args...);
    }
    /// Logs at LogLevel::warning. See @ref log.
    template <Formattable... Args> void warning(FormatString<Args...> fmt, const Args &...args) {
        log(LogLevel::warning, fmt, args...);
    }
    /// Logs at LogLevel::error. See @ref log.
    template <Formattable... Args> void error(FormatString<Args...> fmt, const Args &...args) {
        log(LogLevel::error, fmt, args...);
    }
    /// Logs at LogLevel::critical. See @ref log.
    template <Formattable... Args> void critical(FormatString<Args...> fmt, const Args &...args) {
        log(LogLevel::critical, fmt, args...);
    }

    /// Blocks until every record enqueued before this call has been written and every
    /// sink has flushed. Returns immediately after shutdown (the drain already did both).
    /// @throws std::logic_error when called from the pump thread (a sink calling back).
    void flush();

    /// Stops admissions, drains every accepted record to the sinks, flushes them, and
    /// joins the pump. Idempotent; must not be called concurrently from multiple threads.
    void shutdown();

    /// The lowest level that is delivered; records below it are filtered unformatted.
    [[nodiscard]] LogLevel level() const noexcept { return min_level_.load(std::memory_order_relaxed); }

    /// Sets the runtime filter level. `LogLevel::off` silences the logger entirely.
    void set_level(LogLevel level) noexcept { min_level_.store(level, std::memory_order_relaxed); }

    /// Records refused because the queue was full under OverflowPolicy::drop.
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    /// Sink write/flush calls that threw, plus records whose rendering failed (all
    /// absorbed by the pump; see Sink).
    [[nodiscard]] std::uint64_t sink_failures() const noexcept {
        return sink_failures_.load(std::memory_order_relaxed);
    }

  private:
    /// A flush() barrier riding the record queue itself: completing ticket N means every
    /// record enqueued before ticket N was issued has been written and the sinks were
    /// flushed — bounded by queue depth even under sustained logging (ADR-0020).
    struct FlushMarker {
        std::uint64_t ticket;
    };
    using QueueItem = std::variant<LogRecord, FlushMarker>;

    void enqueue(LogRecord record);
    void pump_loop();
    void deliver(const LogRecord &record);
    void flush_sinks() noexcept;

    std::vector<UniqueRef<Sink>> sinks_;
    std::size_t capacity_;
    OverflowPolicy policy_;
    std::atomic<LogLevel> min_level_{LogLevel::trace};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> sink_failures_{0};
    std::mutex mutex_;
    std::condition_variable records_available_; ///< pump waits: work or shutdown
    std::condition_variable space_available_;   ///< block-policy producers wait: a slot or shutdown
    std::condition_variable flush_done_;        ///< flush() waits: its ticket completed
    std::deque<QueueItem> queue_;               ///< guarded by mutex_
    std::uint64_t flush_requested_ = 0;         ///< guarded by mutex_
    std::uint64_t flush_completed_ = 0;         ///< guarded by mutex_
    bool stopping_ = false;                     ///< guarded by mutex_
    std::thread::id pump_id_;                   ///< immutable after construction: the reentrancy guards
                                                ///< read it lock-free while shutdown() joins pump_
    std::thread pump_;                          ///< initialized last in the constructor
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_LOGGER_HPP
