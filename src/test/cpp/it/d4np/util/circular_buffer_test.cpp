// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for CircularBuffer<T> (component #13, roadmap 4.3): the fixed-capacity ring buffer.
// Covers FIFO order, full/empty accounting, the two push policies (reject vs overwrite),
// wrap-around, front/back access, and move-only element types.
#include <doctest/doctest.h>

#include <it/d4np/util/circular_buffer.hpp>

#include <memory>
#include <optional>

namespace {

using it::d4np::util::CircularBuffer;

} // namespace

TEST_CASE("a fresh buffer is empty with the requested capacity") {
    CircularBuffer<int> buffer(3);
    CHECK(buffer.capacity() == 3);
    CHECK(buffer.size() == 0);
    CHECK(buffer.empty());
    CHECK_FALSE(buffer.full());
}

TEST_CASE("try_push fills the buffer and then reports failure when full") {
    CircularBuffer<int> buffer(2);
    CHECK(buffer.try_push(1));
    CHECK(buffer.try_push(2));
    CHECK(buffer.full());
    CHECK_FALSE(buffer.try_push(3)); // rejected, buffer unchanged
    CHECK(buffer.size() == 2);
    CHECK(buffer.front() == 1);
    CHECK(buffer.back() == 2);
}

TEST_CASE("pop returns elements in FIFO order and nullopt when empty") {
    CircularBuffer<int> buffer(3);
    CHECK(buffer.try_push(10));
    CHECK(buffer.try_push(20));
    CHECK(buffer.try_push(30));
    CHECK(buffer.pop() == 10);
    CHECK(buffer.pop() == 20);
    CHECK(buffer.pop() == 30);
    CHECK(buffer.empty());
    CHECK(buffer.pop() == std::nullopt);
}

TEST_CASE("push_overwrite discards the oldest element when full") {
    CircularBuffer<int> buffer(3);
    buffer.push_overwrite(1);
    buffer.push_overwrite(2);
    buffer.push_overwrite(3);
    buffer.push_overwrite(4); // drops 1
    CHECK(buffer.size() == 3);
    CHECK(buffer.front() == 2);
    CHECK(buffer.back() == 4);
    CHECK(buffer.pop() == 2);
    CHECK(buffer.pop() == 3);
    CHECK(buffer.pop() == 4);
}

TEST_CASE("indices wrap around correctly under interleaved push/pop") {
    CircularBuffer<int> buffer(3);
    CHECK(buffer.try_push(1));
    CHECK(buffer.try_push(2));
    CHECK(buffer.try_push(3));
    CHECK(buffer.pop() == 1);  // frees the first slot
    CHECK(buffer.try_push(4)); // wraps into that slot
    CHECK(buffer.front() == 2);
    CHECK(buffer.back() == 4);
    CHECK(buffer.pop() == 2);
    CHECK(buffer.pop() == 3);
    CHECK(buffer.pop() == 4);
    CHECK(buffer.empty());
}

TEST_CASE("clear empties the buffer without changing capacity") {
    CircularBuffer<int> buffer(4);
    CHECK(buffer.try_push(1));
    CHECK(buffer.try_push(2));
    buffer.clear();
    CHECK(buffer.empty());
    CHECK(buffer.capacity() == 4);
    CHECK(buffer.try_push(9));
    CHECK(buffer.front() == 9);
}

TEST_CASE("works with move-only element types") {
    CircularBuffer<std::unique_ptr<int>> buffer(2);
    CHECK(buffer.try_push(std::make_unique<int>(7)));
    CHECK(buffer.try_push(std::make_unique<int>(8)));
    CHECK_FALSE(buffer.try_push(std::make_unique<int>(9))); // full

    const std::optional<std::unique_ptr<int>> first = buffer.pop();
    REQUIRE(first.has_value());
    if (first.has_value()) {
        REQUIRE(first.value() != nullptr);
        CHECK(*first.value() == 7);
    }
}
