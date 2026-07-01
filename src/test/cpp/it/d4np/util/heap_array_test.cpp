// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for HeapArray<T> (component #4, roadmap 3.2): a fixed-size, bounds-checked heap
// array. Covers construction variants, checked/unchecked access, value (deep-copy) and move
// semantics, iteration, and the destruction balance (verified under ASan/Valgrind in CI).
#include <doctest/doctest.h>

#include <it/d4np/util/heap_array.hpp>

#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

using it::d4np::util::HeapArray;

// Live-instance counter to prove every element is destroyed exactly once.
struct Counted {
    static inline int live = 0;
    int value = 0;
    Counted() { ++live; }
    Counted(const Counted &other) : value(other.value) { ++live; }
    Counted &operator=(const Counted &) = default;
    Counted(Counted &&other) noexcept : value(other.value) { ++live; }
    Counted &operator=(Counted &&) noexcept = default;
    ~Counted() { --live; }
};

static_assert(std::is_copy_constructible_v<HeapArray<int>>, "HeapArray should be copyable");
static_assert(std::is_move_constructible_v<HeapArray<int>>, "HeapArray should be movable");
static_assert(std::is_nothrow_move_constructible_v<HeapArray<int>>, "moving must be noexcept");

} // namespace

TEST_CASE("sized construction value-initializes the elements") {
    HeapArray<int> arr(4);
    CHECK(arr.size() == 4);
    CHECK_FALSE(arr.empty());
    for (int v : arr) {
        CHECK(v == 0);
    }
}

TEST_CASE("fill construction sets every element") {
    HeapArray<int> arr(3, 7);
    CHECK(arr.size() == 3);
    CHECK(arr[0] == 7);
    CHECK(arr[1] == 7);
    CHECK(arr[2] == 7);
}

TEST_CASE("initializer-list construction preserves order") {
    HeapArray<int> arr{10, 20, 30};
    CHECK(arr.size() == 3);
    CHECK(arr.front() == 10);
    CHECK(arr.back() == 30);
    CHECK(arr[1] == 20);
}

TEST_CASE("at() is bounds-checked and throws out_of_range") {
    HeapArray<int> arr(2, 1);
    CHECK(arr.at(0) == 1);
    CHECK(arr.at(1) == 1);
    CHECK_THROWS_AS((void)arr.at(2), std::out_of_range);
    CHECK_THROWS_AS((void)std::as_const(arr).at(99), std::out_of_range);
}

TEST_CASE("operator[] writes through to storage") {
    HeapArray<int> arr(3);
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    CHECK(arr.front() == 1);
    CHECK(arr.back() == 3);
    CHECK(std::accumulate(arr.begin(), arr.end(), 0) == 6);
}

TEST_CASE("empty array has matching begin/end and zero size") {
    HeapArray<int> arr(0);
    CHECK(arr.empty());
    CHECK(arr.size() == 0);
    CHECK(arr.begin() == arr.end());
}

TEST_CASE("copy is a deep, independent copy") {
    HeapArray<int> original{1, 2, 3};
    HeapArray<int> copy = original;
    copy[0] = 99;
    CHECK(original[0] == 1); // original is untouched
    CHECK(copy[0] == 99);
    CHECK(copy.size() == 3);
}

TEST_CASE("copy assignment replaces contents") {
    HeapArray<int> a(2, 5);
    HeapArray<int> b{1, 2, 3, 4};
    a = b;
    CHECK(a.size() == 4);
    CHECK(a.back() == 4);
}

TEST_CASE("move transfers the buffer and empties the source") {
    HeapArray<int> src{1, 2, 3};
    HeapArray<int> dst = std::move(src);
    CHECK(dst.size() == 3);
    CHECK(dst[2] == 3);
    CHECK(src.empty());              // NOLINT(bugprone-use-after-move) — asserting moved-from state
    CHECK(src.begin() == src.end()); // NOLINT(bugprone-use-after-move)
}

TEST_CASE("fill() overwrites all elements") {
    HeapArray<int> arr(4);
    arr.fill(42);
    for (int v : arr) {
        CHECK(v == 42);
    }
}

TEST_CASE("swap exchanges buffers and sizes") {
    HeapArray<int> a{1, 2};
    HeapArray<int> b{9, 8, 7};
    swap(a, b);
    CHECK(a.size() == 3);
    CHECK(a.front() == 9);
    CHECK(b.size() == 2);
    CHECK(b.front() == 1);
}

TEST_CASE("every element is destroyed exactly once") {
    REQUIRE(Counted::live == 0);
    {
        HeapArray<Counted> arr(5);
        CHECK(Counted::live == 5);
        {
            HeapArray<Counted> copy = arr; // deep copy: 5 more live
            CHECK(Counted::live == 10);
            copy[0].value = 42;
            CHECK(arr[0].value == 0); // the copy is independent of the original
        }
        CHECK(Counted::live == 5); // copy destroyed
    }
    CHECK(Counted::live == 0); // original destroyed
}
