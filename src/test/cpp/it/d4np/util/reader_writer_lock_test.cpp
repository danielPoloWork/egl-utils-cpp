// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for ReaderWriterLock (component #8, roadmap 6.2). Covers reader concurrency, writer
// exclusivity, the writer-preference policy (a waiting writer holds back new readers), timed
// acquisitions including the give-up-wakes-readers path, std lock-guard compatibility, and a
// multi-writer counter under contention. Thread cases avoid elapsed-time assertions; TSan
// covers race checking in CI.
#include <doctest/doctest.h>

#include <it/d4np/util/reader_writer_lock.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace {

using it::d4np::util::ReaderWriterLock;
using namespace std::chrono_literals;

// Long enough that a healthy wakeup always beats it, even on a saturated CI box.
constexpr auto generous = 5s;

} // namespace

TEST_CASE("readers share, writers exclude") {
    ReaderWriterLock lock;

    lock.lock_shared(); // main is a reader
    bool other_reader_entered = false;
    bool writer_excluded = true;
    std::thread reader{[&] {
        other_reader_entered = lock.try_lock_shared(); // a second reader passes
        if (other_reader_entered) {
            writer_excluded = !lock.try_lock(); // a writer does not (from its own thread)
            lock.unlock_shared();
        }
    }};
    reader.join();
    CHECK(other_reader_entered);
    CHECK(writer_excluded);
    lock.unlock_shared();

    lock.lock(); // now main is a writer
    bool reader_excluded = false;
    bool other_writer_excluded = false;
    std::thread prober{[&] {
        reader_excluded = !lock.try_lock_shared();
        other_writer_excluded = !lock.try_lock();
    }};
    prober.join();
    CHECK(reader_excluded);
    CHECK(other_writer_excluded);
    lock.unlock();
}

TEST_CASE("the lock is free again after each mode is released") {
    ReaderWriterLock lock;
    lock.lock();
    lock.unlock();
    CHECK(lock.try_lock_shared());
    lock.unlock_shared();
    CHECK(lock.try_lock());
    lock.unlock();
}

TEST_CASE("a waiting writer holds back new readers and runs before them") {
    ReaderWriterLock lock;
    std::atomic<bool> writer_ran{false};

    lock.lock_shared(); // main reader in — the writer below must wait
    std::thread writer{[&] {
        lock.lock();
        writer_ran.store(true);
        lock.unlock();
    }};

    // Poll until the writer has registered as waiting: from that moment, new readers are
    // held back, so try_lock_shared must start failing (writer preference).
    bool new_readers_blocked = false;
    for (int i = 0; i < 10000 && !new_readers_blocked; ++i) {
        if (lock.try_lock_shared()) {
            lock.unlock_shared();
            std::this_thread::sleep_for(1ms);
        } else {
            new_readers_blocked = true;
        }
    }
    CHECK(new_readers_blocked);
    CHECK_FALSE(writer_ran.load()); // still excluded by the main reader

    lock.unlock_shared(); // last reader out hands the lock to the writer
    writer.join();
    CHECK(writer_ran.load());
    CHECK(lock.try_lock_shared()); // and readers are admitted again afterwards
    lock.unlock_shared();
}

TEST_CASE("timed acquisitions fail cleanly while the other mode is held") {
    ReaderWriterLock lock;

    lock.lock(); // writer holds
    std::thread prober{[&] {
        CHECK_FALSE(lock.try_lock_shared_for(10ms));
        CHECK_FALSE(lock.try_lock_for(10ms));
        CHECK_FALSE(lock.try_lock_shared_until(std::chrono::steady_clock::now() - 1ms));
        CHECK_FALSE(lock.try_lock_until(std::chrono::steady_clock::now() - 1ms));
    }};
    prober.join();
    lock.unlock();

    CHECK(lock.try_lock_for(0ms)); // and succeed immediately on a free lock
    lock.unlock();
    CHECK(lock.try_lock_shared_until(std::chrono::steady_clock::now()));
    lock.unlock_shared();
}

TEST_CASE("a timed writer that gives up re-admits the readers it was holding back") {
    ReaderWriterLock lock;

    lock.lock_shared(); // this reader never leaves, so the timed writer below must fail
    std::thread writer{[&] { CHECK_FALSE(lock.try_lock_for(50ms)); }};

    // Whatever the interleaving, once the writer has timed out readers must flow again;
    // while it waits they are held back — hence the generous timed acquisition.
    std::thread reader{[&] {
        CHECK(lock.try_lock_shared_for(generous));
        lock.unlock_shared();
    }};
    writer.join();
    reader.join();
    lock.unlock_shared();
}

TEST_CASE("std lock guards compose with the SharedTimedMutex interface") {
    ReaderWriterLock lock;
    int value = 0;
    {
        const std::scoped_lock writer{lock};
        value = 42;
    }
    {
        const std::shared_lock reader{lock};
        CHECK(value == 42);
    }
    std::unique_lock<ReaderWriterLock> timed{lock, std::defer_lock};
    CHECK(timed.try_lock_for(0ms));
}

TEST_CASE("a shared counter stays consistent under writer contention") {
    constexpr int writers = 4;
    constexpr int increments = 1000;
    ReaderWriterLock lock;
    int counter = 0; // guarded by `lock`
    std::atomic<bool> stop_readers{false};
    bool reader_saw_torn_value = false; // written by the reader thread only, read after join

    std::vector<std::thread> threads;
    threads.reserve(writers + 1);
    for (int w = 0; w < writers; ++w) {
        threads.emplace_back([&] {
            for (int i = 0; i < increments; ++i) {
                const std::scoped_lock guard{lock};
                ++counter;
            }
        });
    }
    threads.emplace_back([&] { // a reader hammering the shared side meanwhile
        while (!stop_readers.load()) {
            const std::shared_lock guard{lock};
            if (counter < 0 || counter > writers * increments) {
                reader_saw_torn_value = true;
            }
        }
    });

    for (int w = 0; w < writers; ++w) {
        threads[static_cast<std::size_t>(w)].join();
    }
    stop_readers.store(true);
    threads.back().join();
    CHECK_FALSE(reader_saw_torn_value);

    const std::shared_lock guard{lock};
    CHECK(counter == writers * increments);
}
