// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for Logger (component #20, roadmap 7.3). Compiled only when the STATIC tier is
// enabled (like stack_trace_test). The custom test sinks write into caller-owned state;
// the tests read that state only after flush()/shutdown(), whose internal synchronization
// orders the reads after every sink call — so only GateSink, which the pump and the test
// thread touch concurrently by design, carries its own lock. UDP delivery is asserted
// structurally (a local send succeeds); receipt verification would re-implement platform
// sockets in a test, and datagram loss is invisible by contract anyway (ADR-0020).
#include <doctest/doctest.h>

#include <it/d4np/util/logger.hpp>
#include <it/d4np/util/unique_ref.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using it::d4np::util::ConsoleSink;
using it::d4np::util::ConsoleStream;
using it::d4np::util::FileSink;
using it::d4np::util::level_name;
using it::d4np::util::Logger;
using it::d4np::util::LogLevel;
using it::d4np::util::LogRecord;
using it::d4np::util::make_unique_ref;
using it::d4np::util::OverflowPolicy;
using it::d4np::util::Sink;
using it::d4np::util::UdpSink;
using it::d4np::util::UniqueRef;

// The level vocabulary is a compile-time contract.
static_assert(level_name(LogLevel::trace) == "trace");
static_assert(level_name(LogLevel::debug) == "debug");
static_assert(level_name(LogLevel::info) == "info");
static_assert(level_name(LogLevel::warning) == "warning");
static_assert(level_name(LogLevel::error) == "error");
static_assert(level_name(LogLevel::critical) == "critical");
static_assert(level_name(LogLevel::off) == "off");
static_assert(Logger::default_queue_capacity == 1024);

/// Caller-owned storage a RecordingSink delivers into; declared before (so destroyed
/// after) the logger that owns the sink.
struct Captured {
    std::vector<LogRecord> records;
    std::vector<std::string> lines;
    std::size_t flushes = 0;
};

/// Copies every delivered record and rendered line into the Captured it was given.
class RecordingSink final : public Sink {
  public:
    explicit RecordingSink(Captured &captured) : captured_(&captured) {}

    void write(const LogRecord &record, std::string_view line) override {
        captured_->records.push_back(record);
        captured_->lines.emplace_back(line);
    }
    void flush() override { ++captured_->flushes; }

  private:
    Captured *captured_;
};

/// Throws from write (never from flush): exercises the pump's failure containment with an
/// exact sink_failures() count.
class ThrowingSink final : public Sink {
  public:
    void write(const LogRecord & /*record*/, std::string_view /*line*/) override {
        throw std::runtime_error("ThrowingSink: write");
    }
    void flush() override {}
};

/// Blocks the pump inside write until the test opens the gate — the only deterministic
/// way to hold the bounded queue full. Internally locked: the pump writes while the test
/// thread opens/reads.
class GateSink final : public Sink {
  public:
    void write(const LogRecord & /*record*/, std::string_view /*line*/) override {
        std::unique_lock<std::mutex> lock{mutex_};
        opened_.wait(lock, [this] { return open_; });
        ++written_;
    }
    void flush() override {}

    void open() {
        {
            const std::scoped_lock lock{mutex_};
            open_ = true;
        }
        opened_.notify_all();
    }

    [[nodiscard]] std::size_t written() {
        const std::scoped_lock lock{mutex_};
        return written_;
    }

  private:
    std::mutex mutex_;
    std::condition_variable opened_;
    bool open_ = false;       ///< guarded by mutex_
    std::size_t written_ = 0; ///< guarded by mutex_
};

/// Calls flush() on its owning logger from inside write — the documented deadlock case
/// the reentrancy guard must reject.
class ReentrantFlushSink final : public Sink {
  public:
    void write(const LogRecord & /*record*/, std::string_view /*line*/) override {
        if (owner == nullptr) {
            return;
        }
        try {
            owner->flush();
        } catch (const std::logic_error &) {
            rejected = true;
        }
    }
    void flush() override {}

    Logger *owner = nullptr; ///< set by the test after the logger exists
    bool rejected = false;   ///< read by the test only after Logger::flush()/shutdown()
};

/// One-sink vector builder (the converting move turns UniqueRef<SinkType> into
/// UniqueRef<Sink>).
template <typename SinkType, typename... Args> std::vector<UniqueRef<Sink>> one_sink(Args &&...args) {
    std::vector<UniqueRef<Sink>> sinks;
    sinks.emplace_back(make_unique_ref<SinkType>(std::forward<Args>(args)...));
    return sinks;
}

[[nodiscard]] bool all_digits(std::string_view text) {
    return !text.empty() &&
           std::ranges::all_of(text, [](char character) { return character >= '0' && character <= '9'; });
}

/// The number right after `prefix` in `text` (vararg-free parsing: cert-err34-c bans the
/// scanf family).
[[nodiscard]] std::size_t number_after(std::string_view text, std::string_view prefix) {
    const std::size_t at = text.find(prefix);
    REQUIRE(at != std::string_view::npos);
    const std::string_view digits = text.substr(at + prefix.size());
    std::size_t value = 0;
    const char *const first = digits.data();
    const char *const last = std::next(first, static_cast<std::ptrdiff_t>(digits.size()));
    const std::from_chars_result result = std::from_chars(first, last, value);
    REQUIRE(result.ec == std::errc{});
    return value;
}

/// Structurally validates "[YYYY-MM-DD HH:MM:SS.mmm] [level] [thread] message" without
/// pinning the actual timestamp or thread id.
void check_line_shape(const std::string &line, std::string_view level, std::string_view message) {
    // "[YYYY-MM-DD HH:MM:SS.mmm]" is exactly 25 characters.
    REQUIRE(line.size() > 25);
    const std::string_view view{line};
    CHECK(view.substr(0, 1) == "[");
    CHECK(all_digits(view.substr(1, 4))); // year
    CHECK(view.substr(5, 1) == "-");
    CHECK(all_digits(view.substr(6, 2))); // month
    CHECK(view.substr(8, 1) == "-");
    CHECK(all_digits(view.substr(9, 2))); // day
    CHECK(view.substr(11, 1) == " ");
    CHECK(all_digits(view.substr(12, 2))); // hour
    CHECK(view.substr(14, 1) == ":");
    CHECK(all_digits(view.substr(15, 2))); // minute
    CHECK(view.substr(17, 1) == ":");
    CHECK(all_digits(view.substr(18, 2))); // second
    CHECK(view.substr(20, 1) == ".");
    CHECK(all_digits(view.substr(21, 3))); // millisecond
    CHECK(view.substr(24, 1) == "]");

    std::string_view rest = view.substr(25);
    const std::string level_field = std::string(" [") + std::string(level) + "] [";
    REQUIRE(rest.substr(0, level_field.size()) == level_field);
    rest.remove_prefix(level_field.size());

    const std::size_t closing = rest.find("] ");
    REQUIRE(closing != std::string_view::npos);
    CHECK(all_digits(rest.substr(0, closing))); // thread id
    CHECK(rest.substr(closing + 2) == message);
}

} // namespace

TEST_CASE("constructor validates its arguments") {
    std::vector<UniqueRef<Sink>> no_sinks;
    CHECK_THROWS_AS(Logger(std::move(no_sinks)), std::invalid_argument);

    Captured captured;
    CHECK_THROWS_AS(Logger(one_sink<RecordingSink>(captured), 0), std::invalid_argument);
}

TEST_CASE("records arrive in FIFO order with formatted messages and metadata") {
    Captured captured;
    {
        Logger logger{one_sink<RecordingSink>(captured)};
        logger.info("answer={} ok={}", 42, true);
        logger.warning("plain");
        logger.error("{{literal}} {}", 7);
    } // destructor shutdown drains

    REQUIRE(captured.records.size() == 3);
    REQUIRE(captured.lines.size() == 3);

    CHECK(captured.records[0].level == LogLevel::info);
    CHECK(captured.records[0].message == "answer=42 ok=true");
    CHECK(captured.records[1].level == LogLevel::warning);
    CHECK(captured.records[1].message == "plain");
    CHECK(captured.records[2].level == LogLevel::error);
    CHECK(captured.records[2].message == "{literal} 7");

    // All three were logged by this one thread, and capture time was really taken.
    CHECK(captured.records[0].thread_id != 0);
    CHECK(captured.records[0].thread_id == captured.records[1].thread_id);
    CHECK(captured.records[1].thread_id == captured.records[2].thread_id);
    CHECK(captured.records[0].time.time_since_epoch().count() != 0);
    CHECK(captured.records[0].time <= captured.records[2].time);

    check_line_shape(captured.lines[0], "info", "answer=42 ok=true");
    check_line_shape(captured.lines[1], "warning", "plain");
    check_line_shape(captured.lines[2], "error", "{literal} 7");
}

TEST_CASE("the runtime level filters records below it") {
    Captured captured;
    {
        Logger logger{one_sink<RecordingSink>(captured)};
        CHECK(logger.level() == LogLevel::trace); // deliver-everything default

        logger.set_level(LogLevel::warning);
        CHECK(logger.level() == LogLevel::warning);
        logger.trace("filtered");
        logger.debug("filtered");
        logger.info("filtered");
        logger.warning("kept");
        logger.error("kept");

        logger.set_level(LogLevel::off); // silences even critical
        logger.critical("filtered");

        logger.flush();
        CHECK(logger.dropped() == 0); // filtered is not dropped
    }

    REQUIRE(captured.records.size() == 2);
    CHECK(captured.records[0].message == "kept");
    CHECK(captured.records[1].message == "kept");
}

TEST_CASE("logging at level off is rejected") {
    Captured captured;
    Logger logger{one_sink<RecordingSink>(captured)};
    CHECK_THROWS_AS(logger.log(LogLevel::off, "never"), std::invalid_argument);
}

TEST_CASE("flush delivers everything enqueued before it and flushes the sinks") {
    Captured captured;
    Logger logger{one_sink<RecordingSink>(captured)};
    constexpr std::size_t record_count = 50;
    for (std::size_t i = 0; i < record_count; ++i) {
        logger.info("record {}", i);
    }
    logger.flush();
    // The logger is still running: flush() alone must have completed the delivery.
    CHECK(captured.records.size() == record_count);
    CHECK(captured.flushes != 0);
}

TEST_CASE("shutdown drains, is idempotent, and further logging throws") {
    Captured captured;
    Logger logger{one_sink<RecordingSink>(captured)};
    constexpr std::size_t record_count = 10;
    for (std::size_t i = 0; i < record_count; ++i) {
        logger.debug("record {}", i);
    }
    logger.shutdown();

    CHECK(captured.records.size() == record_count);
    CHECK_THROWS_AS(logger.info("after shutdown"), std::logic_error);
    CHECK_NOTHROW(logger.shutdown()); // idempotent
    CHECK_NOTHROW(logger.flush());    // documented: returns immediately after shutdown
}

TEST_CASE("a full queue drops under OverflowPolicy::drop and counts what it refused") {
    auto gate_owned = make_unique_ref<GateSink>();
    GateSink &gate = *gate_owned;
    std::vector<UniqueRef<Sink>> sinks;
    sinks.emplace_back(std::move(gate_owned));

    Logger logger{std::move(sinks), 2, OverflowPolicy::drop};
    constexpr std::uint64_t attempted = 5;
    for (std::uint64_t i = 0; i < attempted; ++i) {
        logger.info("record {}", i); // never blocks under drop
    }
    // Capacity 2 plus at most one record already pulled into the (gated) sink: at least
    // two of the five must have been refused. The count is final — this thread was the
    // only producer.
    const std::uint64_t dropped = logger.dropped();
    CHECK(dropped >= 2);
    CHECK(dropped < attempted);

    gate.open();
    logger.shutdown(); // drains every accepted record
    CHECK(gate.written() == attempted - dropped);
}

TEST_CASE("a full queue blocks producers under OverflowPolicy::block and loses nothing") {
    auto gate_owned = make_unique_ref<GateSink>();
    GateSink &gate = *gate_owned;
    std::vector<UniqueRef<Sink>> sinks;
    sinks.emplace_back(std::move(gate_owned));

    Logger logger{std::move(sinks), 1, OverflowPolicy::block};
    constexpr std::uint64_t attempted = 4;
    std::thread producer{[&logger] {
        for (std::uint64_t i = 0; i < attempted; ++i) {
            logger.info("record {}", i); // blocks on the full queue until the pump drains
        }
    }};
    gate.open();
    producer.join();
    logger.shutdown();

    CHECK(logger.dropped() == 0);
    CHECK(gate.written() == attempted);
}

TEST_CASE("a throwing sink is counted and its siblings still deliver") {
    Captured captured;
    std::vector<UniqueRef<Sink>> sinks;
    sinks.emplace_back(make_unique_ref<ThrowingSink>()); // first, so a kill would starve the recorder
    sinks.emplace_back(make_unique_ref<RecordingSink>(captured));

    Logger logger{std::move(sinks)};
    constexpr std::uint64_t record_count = 3;
    for (std::uint64_t i = 0; i < record_count; ++i) {
        logger.info("record {}", i);
    }
    logger.flush();

    CHECK(logger.sink_failures() == record_count); // one failed write per record
    CHECK(captured.records.size() == record_count);
}

TEST_CASE("flush from inside a sink is rejected instead of deadlocking") {
    auto sink_owned = make_unique_ref<ReentrantFlushSink>();
    ReentrantFlushSink &sink = *sink_owned;
    std::vector<UniqueRef<Sink>> sinks;
    sinks.emplace_back(std::move(sink_owned));

    Logger logger{std::move(sinks)};
    sink.owner = &logger; // published to the pump by the next enqueue's lock hand-off
    logger.info("provoke the reentrant flush");
    logger.flush();

    CHECK(sink.rejected);
}

TEST_CASE("concurrent producers all deliver, in per-producer order, with per-thread ids") {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t records_per_producer = 25;

    Captured captured;
    {
        Logger logger{one_sink<RecordingSink>(captured)};
        std::vector<std::thread> producers;
        producers.reserve(producer_count);
        for (std::size_t producer = 0; producer < producer_count; ++producer) {
            producers.emplace_back([&logger, producer] {
                for (std::size_t sequence = 0; sequence < records_per_producer; ++sequence) {
                    logger.info("producer {} sequence {}", producer, sequence);
                }
            });
        }
        for (std::thread &producer : producers) {
            producer.join();
        }
    }

    REQUIRE(captured.records.size() == producer_count * records_per_producer);

    // Per producer: sequences arrive in submission order and every record carries the
    // same OS thread id.
    std::vector<std::size_t> next_sequence(producer_count, 0);
    std::vector<std::uint64_t> thread_ids(producer_count, 0);
    for (const LogRecord &record : captured.records) {
        const std::size_t producer = number_after(record.message, "producer ");
        const std::size_t sequence = number_after(record.message, "sequence ");
        REQUIRE(producer < producer_count);
        CHECK(sequence == next_sequence[producer]);
        ++next_sequence[producer];
        if (thread_ids[producer] == 0) {
            thread_ids[producer] = record.thread_id;
        }
        CHECK(record.thread_id == thread_ids[producer]);
    }
    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        CHECK(next_sequence[producer] == records_per_producer);
    }
}

TEST_CASE("FileSink writes one line per record and append preserves existing content") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "egl_util_logger_test.log";
    std::filesystem::remove(path);

    {
        Logger logger{one_sink<FileSink>(path.string(), /*append=*/false)};
        logger.info("first");
        logger.info("second");
    }
    {
        Logger logger{one_sink<FileSink>(path.string(), /*append=*/true)};
        logger.info("third");
    }

    std::ifstream input{path, std::ios::binary};
    REQUIRE(input.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    input.close();
    std::filesystem::remove(path);

    REQUIRE(lines.size() == 3);
    check_line_shape(lines[0], "info", "first");
    check_line_shape(lines[1], "info", "second");
    check_line_shape(lines[2], "info", "third");
}

TEST_CASE("FileSink refuses an unopenable path") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "egl_util_logger_no_such_dir" / "test.log";
    CHECK_THROWS_AS(FileSink(path.string()), std::runtime_error);
}

TEST_CASE("ConsoleSink writes and flushes the requested stream") {
    ConsoleSink sink{ConsoleStream::standard_error};
    const LogRecord record{
        .time = std::chrono::system_clock::now(), .level = LogLevel::info, .thread_id = 1, .message = "smoke"};
    CHECK_NOTHROW(sink.write(record, "logger_test: console sink smoke line (expected output)"));
    CHECK_NOTHROW(sink.flush());
}

TEST_CASE("UdpSink requires a numeric host literal") {
    CHECK_THROWS_AS(UdpSink("localhost", 514), std::invalid_argument);
    CHECK_THROWS_AS(UdpSink("definitely not an address", 514), std::invalid_argument);
}

TEST_CASE("UdpSink sends one datagram per record") {
    // No listener needed: UDP is fire-and-forget, and only *local* send failures are
    // observable by contract (ADR-0020). Port 39481 is an arbitrary high port.
    UdpSink sink{"127.0.0.1", 39481};
    const LogRecord record{
        .time = std::chrono::system_clock::now(), .level = LogLevel::debug, .thread_id = 1, .message = "datagram"};
    CHECK_NOTHROW(sink.write(record, "[ts] [debug] [1] datagram"));
    CHECK_NOTHROW(sink.flush()); // documented no-op
}
