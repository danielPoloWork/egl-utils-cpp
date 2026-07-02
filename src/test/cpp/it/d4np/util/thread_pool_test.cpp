// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for ThreadPool (component #5, roadmap 6.5). Covers result and exception delivery
// through the returned TaskFuture, the stable priority contract (higher first, ties in
// submission order — proven on a single-worker pool gated by a Semaphore), drain-on-shutdown,
// the misuse error model, and a many-tasks contention case. Thread cases synchronize through
// the library's own primitives; TSan covers races in CI.
#include <doctest/doctest.h>

#include <it/d4np/util/semaphore.hpp>
#include <it/d4np/util/task_future.hpp>
#include <it/d4np/util/thread_pool.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using it::d4np::util::Semaphore;
using it::d4np::util::TaskFuture;
using it::d4np::util::ThreadPool;

} // namespace

TEST_CASE("submit delivers values, void completions, and move-only results") {
    ThreadPool pool{2};
    TaskFuture<int> answer = pool.submit([] { return 41 + 1; });
    TaskFuture<std::string> text = pool.submit([] { return std::string{"done"}; });

    std::atomic<bool> side_effect{false};
    TaskFuture<void> completion = pool.submit([&side_effect] { side_effect.store(true); });

    TaskFuture<std::unique_ptr<int>> boxed = pool.submit([] { return std::make_unique<int>(7); });

    CHECK(answer.get() == 42);
    CHECK(text.get() == "done");
    completion.get();
    CHECK(side_effect.load());
    const std::unique_ptr<int> box = boxed.get();
    REQUIRE(box != nullptr);
    CHECK(*box == 7);
}

TEST_CASE("a throwing task surfaces through its future and never kills the worker") {
    ThreadPool pool{1};
    TaskFuture<int> failing = pool.submit([]() -> int { throw std::runtime_error{"task blew up"}; });
    CHECK_THROWS_WITH_AS(static_cast<void>(failing.get()), "task blew up", std::runtime_error);

    // The single worker survived and keeps serving.
    TaskFuture<int> after = pool.submit([] { return 5; });
    CHECK(after.get() == 5);
}

TEST_CASE("higher priorities run first and equal priorities keep submission order") {
    ThreadPool pool{1}; // single worker: execution order is fully observable
    Semaphore gate{0};

    // Park the worker so the queue orders everything submitted meanwhile.
    TaskFuture<void> parked = pool.submit(100, [&gate] { gate.acquire(); });

    std::vector<int> order; // touched only by the single worker after the gate opens
    TaskFuture<void> low = pool.submit(1, [&order] { order.push_back(1); });
    TaskFuture<void> high = pool.submit(5, [&order] { order.push_back(5); });
    TaskFuture<void> mid = pool.submit(3, [&order] { order.push_back(3); });
    TaskFuture<void> first_default = pool.submit([&order] { order.push_back(100); });
    TaskFuture<void> second_default = pool.submit([&order] { order.push_back(200); });

    gate.release();
    parked.get();
    low.get();
    high.get();
    mid.get();
    first_default.get();
    second_default.get();

    CHECK(order == std::vector<int>{5, 3, 1, 100, 200}); // priority desc, then submission order
}

TEST_CASE("shutdown drains every queued task before joining") {
    std::atomic<int> executed{0};
    constexpr int tasks = 32;
    std::vector<TaskFuture<void>> futures;
    futures.reserve(tasks);
    {
        ThreadPool pool{2};
        Semaphore gate{0};
        TaskFuture<void> park_a = pool.submit(100, [&gate] { gate.acquire(); });
        TaskFuture<void> park_b = pool.submit(100, [&gate] { gate.acquire(); });
        for (int i = 0; i < tasks; ++i) {
            futures.push_back(pool.submit([&executed] { executed.fetch_add(1); }));
        }
        CHECK(pool.pending() <= static_cast<std::size_t>(tasks) + 2); // snapshot: at most the queued work
        gate.release(2);
        park_a.get();
        park_b.get();
    } // ~ThreadPool: shutdown() must run all 32 queued tasks, not drop them
    CHECK(executed.load() == tasks);
    for (TaskFuture<void> &future : futures) {
        future.get(); // every future is satisfied — none broken
    }
}

TEST_CASE("misuse is diagnosed, not undefined") {
    CHECK_THROWS_AS(ThreadPool{0}, std::invalid_argument);

    ThreadPool pool{1};
    CHECK(pool.thread_count() == 1);
    pool.shutdown();
    pool.shutdown(); // idempotent
    CHECK_THROWS_AS(static_cast<void>(pool.submit([] { return 1; })), std::logic_error);
}

TEST_CASE("many tasks across many workers all complete exactly once") {
    constexpr int tasks = 500;
    ThreadPool pool{4};
    std::atomic<int> sum{0};
    std::vector<TaskFuture<int>> futures;
    futures.reserve(tasks);
    for (int i = 1; i <= tasks; ++i) {
        futures.push_back(pool.submit([i, &sum] {
            sum.fetch_add(i);
            return i;
        }));
    }
    int futures_total = 0;
    for (TaskFuture<int> &future : futures) {
        futures_total += future.get();
    }
    constexpr int expected = tasks * (tasks + 1) / 2;
    CHECK(futures_total == expected);
    CHECK(sum.load() == expected);
}

TEST_CASE("tasks can submit follow-up tasks from inside the pool") {
    ThreadPool pool{2};
    TaskFuture<int> nested = pool.submit([&pool] {
        TaskFuture<int> inner = pool.submit([] { return 21; });
        return inner.get() * 2;
    });
    CHECK(nested.get() == 42);
}
