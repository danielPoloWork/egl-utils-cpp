// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for Stopwatch (component #19, roadmap 7.1). Timing assertions are lower-bound or
// monotonicity only — sleep_for guarantees at least the requested time against the same
// steady clock the stopwatch reads, while upper bounds would flake on loaded CI runners.
#include <doctest/doctest.h>

#include <it/d4np/util/stopwatch.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace {

using it::d4np::util::Stopwatch;
using namespace std::chrono_literals;

} // namespace

TEST_CASE("a default stopwatch is stopped at zero") {
    const Stopwatch stopwatch;
    CHECK_FALSE(stopwatch.running());
    CHECK(stopwatch.elapsed() == Stopwatch::duration::zero());
    CHECK(stopwatch.elapsed_microseconds() == 0);
}

TEST_CASE("start_new is already running and elapsed is non-decreasing while running") {
    const Stopwatch stopwatch = Stopwatch::start_new();
    CHECK(stopwatch.running());
    const Stopwatch::duration first = stopwatch.elapsed();
    const Stopwatch::duration second = stopwatch.elapsed();
    CHECK(second >= first); // steady_clock is monotonic
}

TEST_CASE("stop freezes the total and measures at least the slept time") {
    Stopwatch stopwatch = Stopwatch::start_new();
    std::this_thread::sleep_for(5ms);
    stopwatch.stop();

    const Stopwatch::duration frozen = stopwatch.elapsed();
    CHECK(frozen >= 5ms);                 // sleep_for guarantees at least this much steady-clock time
    CHECK(stopwatch.elapsed() == frozen); // stopped: no drift
    CHECK(stopwatch.elapsed_microseconds() >= 5000);
    CHECK(stopwatch.elapsed_milliseconds() >= 5);
}

TEST_CASE("start/stop cycles accumulate") {
    Stopwatch stopwatch;
    stopwatch.start();
    std::this_thread::sleep_for(3ms);
    stopwatch.stop();
    const Stopwatch::duration after_first = stopwatch.elapsed();

    stopwatch.start();
    std::this_thread::sleep_for(3ms);
    stopwatch.stop();

    CHECK(stopwatch.elapsed() >= after_first + 3ms); // second interval added to the first
}

TEST_CASE("reset zeroes and restart times a fresh interval") {
    Stopwatch stopwatch = Stopwatch::start_new();
    std::this_thread::sleep_for(2ms);
    stopwatch.reset();
    CHECK_FALSE(stopwatch.running());
    CHECK(stopwatch.elapsed() == Stopwatch::duration::zero());

    std::this_thread::sleep_for(2ms); // while stopped: must not count
    CHECK(stopwatch.elapsed() == Stopwatch::duration::zero());

    stopwatch.restart();
    CHECK(stopwatch.running());
    std::this_thread::sleep_for(2ms);
    stopwatch.stop();
    CHECK(stopwatch.elapsed() >= 2ms);
    CHECK(stopwatch.elapsed() < Stopwatch::duration::max()); // sanity: a real measurement
}

TEST_CASE("sequencing misuse is diagnosed, not undefined") {
    Stopwatch stopwatch;
    CHECK_THROWS_AS(stopwatch.stop(), std::logic_error); // not running

    stopwatch.start();
    CHECK_THROWS_AS(stopwatch.start(), std::logic_error); // already running
    stopwatch.stop();
    CHECK_THROWS_AS(stopwatch.stop(), std::logic_error); // stopped again

    stopwatch.reset(); // reset is always legal
    stopwatch.restart();
    CHECK(stopwatch.running());
}

TEST_CASE("stopwatches are plain values: copies are independent") {
    Stopwatch original = Stopwatch::start_new();
    std::this_thread::sleep_for(2ms);
    original.stop();

    Stopwatch copy = original;
    copy.reset();
    CHECK(copy.elapsed() == Stopwatch::duration::zero());
    CHECK(original.elapsed() >= 2ms); // untouched by the copy's reset
}
