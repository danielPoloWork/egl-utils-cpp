// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for TaskFuture / TaskPromise (component #6, roadmap 6.3). Covers the value handoff
// (same-thread and cross-thread), the void payload, exception propagation, the abandoned
// promise (destroyed and move-assigned-over), the misuse error model, timed waits, and the
// move-only-payload path that single-shot get() enables. TSan covers race checking in CI.
#include <doctest/doctest.h>

#include <it/d4np/util/task_future.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

using it::d4np::util::BrokenTaskPromise;
using it::d4np::util::TaskFuture;
using it::d4np::util::TaskPromise;
using namespace std::chrono_literals;

// Long enough that a healthy wakeup always beats it, even on a saturated CI box.
constexpr auto generous = 5s;

} // namespace

TEST_CASE("a value set before get is delivered, and get consumes the future") {
    TaskPromise<int> promise;
    TaskFuture<int> future = promise.get_future();
    CHECK(future.valid());
    CHECK_FALSE(future.ready());

    promise.set_value(42);
    CHECK(future.ready());
    CHECK(future.get() == 42);
    CHECK_FALSE(future.valid()); // single-shot: consumed
}

TEST_CASE("get blocks until the promise is satisfied from another thread") {
    TaskPromise<std::string> promise;
    TaskFuture<std::string> future = promise.get_future();

    std::thread producer{[&] { promise.set_value("payload"); }};
    CHECK(future.get() == "payload"); // waits if the producer has not run yet
    producer.join();
}

TEST_CASE("the void payload signals completion") {
    TaskPromise<void> promise;
    TaskFuture<void> future = promise.get_future();
    CHECK_FALSE(future.ready());
    promise.set_value();
    CHECK(future.ready());
    future.get(); // must neither block nor throw
    CHECK_FALSE(future.valid());
}

TEST_CASE("a stored exception is rethrown by get") {
    TaskPromise<int> promise;
    TaskFuture<int> future = promise.get_future();
    promise.set_exception(std::make_exception_ptr(std::runtime_error{"task failed"}));

    CHECK(future.ready()); // an exception is a result too
    CHECK_THROWS_WITH_AS(static_cast<void>(future.get()), "task failed", std::runtime_error);
    CHECK_FALSE(future.valid()); // consumed even on the exception path
}

TEST_CASE("an abandoned promise surfaces as BrokenTaskPromise") {
    TaskFuture<int> orphaned;
    {
        TaskPromise<int> promise;
        orphaned = promise.get_future();
    } // destroyed without a result
    CHECK(orphaned.ready());
    CHECK_THROWS_AS(static_cast<void>(orphaned.get()), BrokenTaskPromise);

    TaskPromise<int> first;
    TaskFuture<int> future = first.get_future();
    first = TaskPromise<int>{}; // move-assigning over an unsatisfied promise abandons it too
    CHECK_THROWS_AS(static_cast<void>(future.get()), BrokenTaskPromise);
    first.set_value(1); // the moved-in promise is intact
}

TEST_CASE("misuse is diagnosed, not undefined") {
    TaskPromise<int> promise;
    TaskFuture<int> future = promise.get_future();
    CHECK_THROWS_AS(static_cast<void>(promise.get_future()), std::logic_error); // only one future

    promise.set_value(1);
    CHECK_THROWS_AS(promise.set_value(2), std::logic_error); // result already set
    CHECK_THROWS_AS(promise.set_exception(std::make_exception_ptr(std::runtime_error{"late"})),
                    std::logic_error); // ditto via the exception channel
    CHECK_THROWS_AS(promise.set_exception(nullptr), std::invalid_argument);

    CHECK(future.get() == 1);
    CHECK_THROWS_AS(static_cast<void>(future.get()), std::logic_error); // already consumed
    const TaskFuture<int> never_attached;
    CHECK_THROWS_AS(never_attached.wait(), std::logic_error); // invalid future

    TaskPromise<int> moved_from;
    const TaskPromise<int> moved_to{std::move(moved_from)};
    // Use-after-move is exactly what the contract diagnoses here.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK_THROWS_AS(moved_from.set_value(3), std::logic_error);
}

TEST_CASE("timed waits report readiness") {
    TaskPromise<int> promise;
    TaskFuture<int> future = promise.get_future();

    CHECK_FALSE(future.wait_for(10ms));
    CHECK_FALSE(future.wait_until(std::chrono::steady_clock::now() - 1ms));

    std::thread producer{[&] { promise.set_value(7); }};
    CHECK(future.wait_for(generous));
    CHECK(future.wait_until(std::chrono::steady_clock::now())); // already ready: no wait
    producer.join();
    CHECK(future.get() == 7);
}

TEST_CASE("move-only payloads flow through the single-shot get") {
    TaskPromise<std::unique_ptr<int>> promise;
    TaskFuture<std::unique_ptr<int>> future = promise.get_future();

    std::thread producer{[&] { promise.set_value(std::make_unique<int>(99)); }};
    const std::unique_ptr<int> result = future.get(); // moved out of the shared state
    producer.join();
    REQUIRE(result != nullptr);
    CHECK(*result == 99);
}

TEST_CASE("set_value forwards convertible arguments") {
    TaskPromise<std::string> promise;
    TaskFuture<std::string> future = promise.get_future();
    promise.set_value("converted"); // const char* -> std::string, forwarded
    CHECK(future.get() == "converted");
}
