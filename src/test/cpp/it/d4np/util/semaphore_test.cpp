// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for Semaphore (component #9, roadmap 6.1). Single-threaded semantics (counting,
// try_acquire, timed waits, error model) plus multithreaded handoff and multi-waiter wakeup.
// Thread cases synchronize through the semaphore itself with generous timeouts — no
// elapsed-time assertions, so they stay robust on loaded CI runners. TSan covers the race
// checking in CI (the tsan preset is not runnable under MSVC locally).
#include <doctest/doctest.h>

#include <it/d4np/util/semaphore.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using it::d4np::util::Semaphore;
using namespace std::chrono_literals;

// Long enough that a healthy wakeup always beats it, even on a saturated CI box.
constexpr auto generous = 5s;

} // namespace

TEST_CASE("the count is consumed by try_acquire and observable via available") {
    Semaphore semaphore{2};
    CHECK(semaphore.available() == 2);
    CHECK(semaphore.try_acquire());
    CHECK(semaphore.try_acquire());
    CHECK_FALSE(semaphore.try_acquire()); // exhausted
    CHECK(semaphore.available() == 0);
}

TEST_CASE("release returns units and release(0) is a no-op") {
    Semaphore semaphore{0};
    CHECK_FALSE(semaphore.try_acquire());
    semaphore.release();
    CHECK(semaphore.available() == 1);
    semaphore.release(3);
    CHECK(semaphore.available() == 4);
    semaphore.release(0);
    CHECK(semaphore.available() == 4);
}

TEST_CASE("misuse is diagnosed, not undefined") {
    CHECK_THROWS_AS(Semaphore{-1}, std::invalid_argument);

    Semaphore semaphore{1};
    CHECK_THROWS_AS(semaphore.release(-1), std::invalid_argument);

    Semaphore full{Semaphore::max()};
    CHECK_THROWS_AS(full.release(), std::overflow_error);
    CHECK(full.available() == Semaphore::max()); // the failed release changed nothing
}

TEST_CASE("acquire succeeds immediately while units are available") {
    Semaphore semaphore{1};
    semaphore.acquire(); // must not block
    CHECK(semaphore.available() == 0);
}

TEST_CASE("timed waits fail cleanly on an empty semaphore") {
    Semaphore semaphore{0};
    CHECK_FALSE(semaphore.try_acquire_for(10ms));
    CHECK_FALSE(semaphore.try_acquire_until(std::chrono::steady_clock::now() - 1ms)); // past deadline
    CHECK(semaphore.available() == 0);
}

TEST_CASE("timed waits succeed immediately while units are available") {
    Semaphore semaphore{2};
    CHECK(semaphore.try_acquire_for(0ms));
    CHECK(semaphore.try_acquire_until(std::chrono::steady_clock::now()));
    CHECK(semaphore.available() == 0);
}

TEST_CASE("a blocked acquirer is woken by a release from another thread") {
    Semaphore work{0};
    Semaphore done{0};
    std::atomic<bool> acquired{false};

    std::thread waiter{[&] {
        work.acquire(); // blocks until main releases
        acquired.store(true);
        done.release();
    }};

    CHECK_FALSE(acquired.load()); // the waiter cannot have passed acquire() before the release
    work.release();
    CHECK(done.try_acquire_for(generous));
    CHECK(acquired.load());
    waiter.join();
}

TEST_CASE("a bulk release wakes every waiter") {
    constexpr int waiters = 4;
    Semaphore work{0};
    Semaphore done{0};

    std::vector<std::thread> threads;
    threads.reserve(waiters);
    for (int i = 0; i < waiters; ++i) {
        threads.emplace_back([&] {
            work.acquire();
            done.release();
        });
    }

    work.release(waiters);
    for (int i = 0; i < waiters; ++i) {
        CHECK(done.try_acquire_for(generous)); // each waiter got exactly one unit
    }
    for (std::thread &thread : threads) {
        thread.join();
    }
    CHECK(work.available() == 0);
}

TEST_CASE("a semaphore pair enforces strict ping-pong alternation") {
    constexpr int rounds = 100;
    Semaphore ping{1};
    Semaphore pong{0};
    int sequence = 0;
    bool ordered = true;

    std::thread other{[&] {
        for (int i = 0; i < rounds; ++i) {
            pong.acquire();
            ordered = ordered && (sequence == (2 * i) + 1);
            ++sequence; // protected by the alternation itself
            ping.release();
        }
    }};

    for (int i = 0; i < rounds; ++i) {
        ping.acquire();
        ordered = ordered && (sequence == 2 * i);
        ++sequence;
        pong.release();
    }
    other.join();

    CHECK(ordered);
    CHECK(sequence == 2 * rounds);
}
