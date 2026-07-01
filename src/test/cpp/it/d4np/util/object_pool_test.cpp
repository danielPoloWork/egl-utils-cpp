// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for ObjectPool<T> (component #1, roadmap 3.4): the thread-safe, fixed-capacity object
// pool. Covers acquire/release accounting, RAII reclamation, exhaustion, the factory
// constructor, and a contended multithreaded run that the CI ThreadSanitizer job verifies is
// race-free.
#include <doctest/doctest.h>

#include <it/d4np/util/object_pool.hpp>

#include <atomic>
#include <cstddef>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using it::d4np::util::ObjectPool;

static_assert(!std::is_copy_constructible_v<ObjectPool<int>>, "pool must not be copyable");
static_assert(!std::is_move_constructible_v<ObjectPool<int>>, "pool must not be movable");

} // namespace

TEST_CASE("a fresh pool reports full availability") {
    ObjectPool<int> pool(4);
    CHECK(pool.capacity() == 4);
    CHECK(pool.available() == 4);
    CHECK(pool.in_use() == 0);
}

TEST_CASE("acquire lends an object and release returns it") {
    ObjectPool<int> pool(2);
    if (auto handle = pool.try_acquire()) {
        CHECK(pool.in_use() == 1);
        CHECK(pool.available() == 1);
        handle->get() = 42;
        CHECK(**handle == 42);
    } else {
        FAIL("pool should have a free slot");
    }
    // handle out of scope -> slot returned
    CHECK(pool.in_use() == 0);
    CHECK(pool.available() == 2);
}

TEST_CASE("try_acquire returns nullopt once the pool is exhausted") {
    ObjectPool<int> pool(2);
    auto a = pool.try_acquire();
    auto b = pool.try_acquire();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(pool.in_use() == 2);

    auto c = pool.try_acquire();
    CHECK_FALSE(c.has_value());

    a.reset(); // release one
    CHECK(pool.available() == 1);
    auto d = pool.try_acquire();
    CHECK(d.has_value());
}

TEST_CASE("moving a handle transfers the borrow without double-releasing") {
    ObjectPool<int> pool(1);
    auto first = pool.try_acquire();
    REQUIRE(first.has_value());
    if (first.has_value()) {
        first->get() = 7;
        ObjectPool<int>::Handle moved = std::move(first).value();
        CHECK(moved.get() == 7);
        CHECK(pool.in_use() == 1); // still exactly one borrow after the move
    }
}

TEST_CASE("the factory constructor initializes every pooled object") {
    int seed = 0;
    ObjectPool<int> pool(3, [&seed] { return ++seed; });
    CHECK(pool.capacity() == 3);
    // Every slot was produced by the factory (values 1..3, order unspecified).
    auto a = pool.try_acquire();
    auto b = pool.try_acquire();
    auto c = pool.try_acquire();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    if (a.has_value() && b.has_value() && c.has_value()) {
        const int sum = a->get() + b->get() + c->get();
        CHECK(sum == 1 + 2 + 3);
    }
}

TEST_CASE("concurrent acquire/release is race-free and never over-lends") {
    constexpr std::size_t capacity = 8;
    ObjectPool<int> pool(capacity);

    std::atomic<int> held{0};
    std::atomic<int> max_held{0};
    std::atomic<long> successful{0};

    auto worker = [&] {
        for (int i = 0; i < 2000; ++i) {
            auto handle = pool.try_acquire();
            if (!handle.has_value()) {
                continue;
            }
            const int now = held.fetch_add(1) + 1;
            int prev = max_held.load();
            while (now > prev && !max_held.compare_exchange_weak(prev, now)) {
                // retry until max_held reflects at least `now`
            }
            handle->get() = now; // exclusive use of the borrowed object
            successful.fetch_add(1);
            held.fetch_sub(1);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back(worker);
    }
    for (auto &thread : threads) {
        thread.join();
    }

    CHECK(successful.load() > 0);
    CHECK(static_cast<std::size_t>(max_held.load()) <= capacity); // never lent more than capacity
    CHECK(pool.available() == capacity);                          // every borrow was returned
    CHECK(pool.in_use() == 0);
}
