// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Stopwatch (component #19): a microsecond-grade profiler over std::chrono::steady_clock —
// monotonic on every supported platform, unlike high_resolution_clock, which the standard
// permits to alias the NTP-adjustable system_clock (ADR-0018). Accumulating semantics:
// start/stop cycles add up; reset/restart give per-interval timing. Header-only.
#ifndef IT_D4NP_UTIL_STOPWATCH_HPP
#define IT_D4NP_UTIL_STOPWATCH_HPP

#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace it::d4np::util {

/// Accumulating high-resolution stopwatch for profiling code sections.
///
/// A default-constructed stopwatch is stopped at zero; `start()`/`stop()` add each timed
/// interval to the running total, so a section entered N times reports its total cost.
/// `elapsed()` includes the currently open interval while running. `reset()` zeroes,
/// `restart()` is reset-then-start, and `Stopwatch::start_new()` constructs already
/// running. Sequencing misuse throws `std::logic_error` — a measurement tool must not turn
/// state bugs into quietly wrong numbers (ADR-0018).
///
/// @note A plain value type (copyable, movable); not thread-safe — one stopwatch belongs
/// to one thread.
class Stopwatch {
  public:
    using clock = std::chrono::steady_clock;
    using duration = std::chrono::nanoseconds;

    /// Stopped, with zero accumulated time.
    Stopwatch() = default;

    /// A stopwatch that is already running.
    [[nodiscard]] static Stopwatch start_new() {
        Stopwatch stopwatch;
        stopwatch.start();
        return stopwatch;
    }

    /// Opens a new timed interval. @throws std::logic_error if already running.
    void start() {
        if (running_) {
            throw std::logic_error("Stopwatch: already running");
        }
        started_at_ = clock::now();
        running_ = true;
    }

    /// Closes the current interval, adding it to the total.
    /// @throws std::logic_error if not running.
    void stop() {
        if (!running_) {
            throw std::logic_error("Stopwatch: not running");
        }
        accumulated_ += std::chrono::duration_cast<duration>(clock::now() - started_at_);
        running_ = false;
    }

    /// Stops (if needed) and zeroes the accumulated total.
    void reset() noexcept {
        accumulated_ = duration::zero();
        running_ = false;
    }

    /// Zeroes the total and immediately starts a fresh interval (per-interval timing).
    void restart() {
        reset();
        start();
    }

    /// Whether an interval is currently open.
    [[nodiscard]] bool running() const noexcept { return running_; }

    /// The accumulated total, including the currently open interval while running.
    /// Non-decreasing while running (steady_clock is monotonic).
    [[nodiscard]] duration elapsed() const {
        if (running_) {
            return accumulated_ + std::chrono::duration_cast<duration>(clock::now() - started_at_);
        }
        return accumulated_;
    }

    /// `elapsed()` truncated to whole microseconds.
    [[nodiscard]] std::int64_t elapsed_microseconds() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(elapsed()).count();
    }

    /// `elapsed()` truncated to whole milliseconds.
    [[nodiscard]] std::int64_t elapsed_milliseconds() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed()).count();
    }

  private:
    clock::time_point started_at_;
    duration accumulated_{duration::zero()};
    bool running_ = false;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_STOPWATCH_HPP
