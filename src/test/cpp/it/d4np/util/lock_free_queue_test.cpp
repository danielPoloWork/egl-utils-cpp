// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for LockFreeQueue (component #7, roadmap 6.4). Single-threaded semantics (FIFO,
// full/empty, capacity rounding, wrap-around, destructor drain, move-only payloads) plus an
// MPMC stress case that pins the concurrency contract: nothing lost, nothing duplicated,
// per-producer FIFO preserved. Assertions run after joins; TSan covers races in CI, and the
// memory-ordering correctness argument itself lives in ADR-0016.
#include <doctest/doctest.h>

#include <it/d4np/util/lock_free_queue.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using it::d4np::util::LockFreeQueue;

/// Counts live instances so the destructor-drain test can prove cleanup.
struct Counted {
    static inline std::atomic<int> live{0};
    Counted() { live.fetch_add(1); }
    Counted(const Counted &) = delete;
    Counted &operator=(const Counted &) = delete;
    Counted(Counted && /*unused*/) noexcept { live.fetch_add(1); }
    Counted &operator=(Counted &&) noexcept = default;
    ~Counted() { live.fetch_sub(1); }
};

} // namespace

TEST_CASE("elements come out in FIFO order and empty pops are empty") {
    LockFreeQueue<int> queue{8};
    CHECK_FALSE(queue.try_pop().has_value()); // empty from the start

    for (int i = 1; i <= 5; ++i) {
        CHECK(queue.try_push(i));
    }
    for (int i = 1; i <= 5; ++i) {
        CHECK(queue.try_pop() == i);
    }
    CHECK_FALSE(queue.try_pop().has_value()); // drained
}

TEST_CASE("capacity is rounded up to the next power of two") {
    CHECK(LockFreeQueue<int>{1}.capacity() == 1);
    CHECK(LockFreeQueue<int>{3}.capacity() == 4);
    CHECK(LockFreeQueue<int>{8}.capacity() == 8);
    CHECK(LockFreeQueue<int>{9}.capacity() == 16);
    CHECK_THROWS_AS(LockFreeQueue<int>{0}, std::invalid_argument);
}

TEST_CASE("a full queue rejects pushes until a pop frees a slot") {
    LockFreeQueue<int> queue{2};
    CHECK(queue.try_push(1));
    CHECK(queue.try_push(2));
    CHECK_FALSE(queue.try_push(3)); // full: backpressure, not an error
    CHECK(queue.try_pop() == 1);
    CHECK(queue.try_push(3)); // the freed slot is reusable
    CHECK(queue.try_pop() == 2);
    CHECK(queue.try_pop() == 3);
}

TEST_CASE("the ring survives many laps of wrap-around") {
    LockFreeQueue<int> queue{2}; // tiny ring so the sequence counters lap constantly
    for (int lap = 0; lap < 1000; ++lap) {
        CHECK(queue.try_push(lap));
        CHECK(queue.try_pop() == lap);
    }
    CHECK_FALSE(queue.try_pop().has_value());
}

TEST_CASE("move-only payloads flow through") {
    LockFreeQueue<std::unique_ptr<int>> queue{4};
    CHECK(queue.try_push(std::make_unique<int>(7)));
    const std::optional<std::unique_ptr<int>> popped = queue.try_pop();
    REQUIRE(popped.has_value());
    REQUIRE(*popped != nullptr);
    CHECK(**popped == 7);
}

TEST_CASE("undelivered elements are destroyed with the queue") {
    CHECK(Counted::live.load() == 0);
    {
        LockFreeQueue<Counted> queue{4};
        CHECK(queue.try_push(Counted{}));
        CHECK(queue.try_push(Counted{}));
        CHECK(queue.try_push(Counted{}));
        CHECK(queue.try_pop().has_value()); // one delivered and destroyed by the caller
    } // two still queued: the queue must destroy them
    CHECK(Counted::live.load() == 0);
}

TEST_CASE("MPMC stress: nothing lost, nothing duplicated, per-producer FIFO holds") {
    constexpr int producers = 4;
    constexpr int consumers = 4;
    constexpr int per_producer = 2500;
    constexpr int total = producers * per_producer;
    constexpr int producer_stride = 1'000'000; // value = producer * stride + sequence

    LockFreeQueue<int> queue{64}; // much smaller than the traffic: forces contention + wrap
    std::atomic<int> popped_count{0};
    // One result set per consumer, written only by that consumer, read after the joins.
    std::vector<std::vector<int>> received(consumers);

    std::vector<std::thread> threads;
    threads.reserve(producers + consumers);
    for (int producer = 0; producer < producers; ++producer) {
        threads.emplace_back([&queue, producer] {
            for (int sequence = 0; sequence < per_producer; ++sequence) {
                while (!queue.try_push((producer * producer_stride) + sequence)) {
                    std::this_thread::yield(); // full: wait for consumers
                }
            }
        });
    }
    for (int consumer = 0; consumer < consumers; ++consumer) {
        threads.emplace_back([&queue, &popped_count, &received, consumer] {
            std::vector<int> &mine = received[static_cast<std::size_t>(consumer)];
            while (popped_count.load() < total) {
                if (const std::optional<int> value = queue.try_pop()) {
                    popped_count.fetch_add(1);
                    mine.push_back(*value);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }

    // Nothing lost, nothing duplicated: every (producer, sequence) pair seen exactly once.
    std::vector<std::vector<bool>> seen(producers, std::vector<bool>(per_producer, false));
    int delivered = 0;
    bool duplicates = false;
    // Per-producer FIFO: within one consumer, sequences from a given producer ascend.
    bool producer_fifo_holds = true;
    for (const std::vector<int> &consumer_values : received) {
        std::vector<int> last_from(producers, -1);
        for (const int value : consumer_values) {
            const int producer = value / producer_stride;
            const int sequence = value % producer_stride;
            duplicates = duplicates || seen[static_cast<std::size_t>(producer)][static_cast<std::size_t>(sequence)];
            seen[static_cast<std::size_t>(producer)][static_cast<std::size_t>(sequence)] = true;
            producer_fifo_holds = producer_fifo_holds && sequence > last_from[static_cast<std::size_t>(producer)];
            last_from[static_cast<std::size_t>(producer)] = sequence;
            ++delivered;
        }
    }
    CHECK(delivered == total);
    CHECK_FALSE(duplicates);
    CHECK(producer_fifo_holds);
    CHECK_FALSE(queue.try_pop().has_value()); // and the queue ends drained
}
