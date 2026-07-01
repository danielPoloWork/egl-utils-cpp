// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for StackAllocator<Size> (component #3, roadmap 3.3): the fixed-buffer bump allocator.
// Covers capacity accounting, alignment, sequential bumping, exhaustion, and the reset() /
// mark() / rewind() reclamation model.
#include <doctest/doctest.h>

#include <it/d4np/util/stack_allocator.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace {

using it::d4np::util::StackAllocator;

// True iff `ptr` is aligned to `alignment`, without reinterpret_cast: std::align returns the
// pointer unchanged exactly when it is already aligned (with ample probe space).
[[nodiscard]] bool aligned(void *ptr, std::size_t alignment) {
    void *probe = ptr;
    std::size_t space = alignment;
    return std::align(alignment, 1, probe, space) == ptr;
}

static_assert(!std::is_copy_constructible_v<StackAllocator<64>>, "must not be copyable");
static_assert(!std::is_move_constructible_v<StackAllocator<64>>, "must not be movable");

} // namespace

TEST_CASE("a fresh allocator reports full capacity and zero use") {
    StackAllocator<128> alloc;
    CHECK(alloc.capacity() == 128);
    CHECK(alloc.used() == 0);
    CHECK(alloc.remaining() == 128);
}

TEST_CASE("allocate returns aligned, non-null storage and advances the offset") {
    StackAllocator<256> alloc;
    void *p = alloc.allocate(10, 8);
    CHECK(p != nullptr);
    CHECK(aligned(p, 8));
    CHECK(alloc.used() >= 10);
    CHECK(alloc.used() + alloc.remaining() == alloc.capacity());
}

TEST_CASE("successive allocations honor their requested alignment") {
    StackAllocator<256> alloc;
    (void)alloc.allocate(1, 1); // misalign the cursor
    void *a = alloc.allocate(4, 4);
    void *b = alloc.allocate(8, 16);
    CHECK(aligned(a, 4));
    CHECK(aligned(b, 16));
}

TEST_CASE("allocate_uninitialized yields typed, writable, aligned storage") {
    StackAllocator<256> alloc;
    auto *value = alloc.allocate_uninitialized<int>();
    CHECK(aligned(value, alignof(int)));
    *value = 12345;
    CHECK(*value == 12345);

    auto *arr = alloc.allocate_uninitialized<double>(3);
    CHECK(aligned(arr, alignof(double)));
    for (std::size_t i = 0; i < 3; ++i) {
        *std::next(arr, static_cast<std::ptrdiff_t>(i)) = static_cast<double>(i);
    }
    CHECK(*arr == 0.0);
    CHECK(*std::next(arr, 2) == 2.0);
}

TEST_CASE("allocation throws std::bad_alloc when capacity is exhausted") {
    StackAllocator<16> alloc;
    (void)alloc.allocate(16, 1);
    CHECK(alloc.remaining() == 0);
    CHECK_THROWS_AS((void)alloc.allocate(1, 1), std::bad_alloc);
}

TEST_CASE("reset reclaims the whole buffer") {
    StackAllocator<64> alloc;
    (void)alloc.allocate(32, 1);
    CHECK(alloc.used() == 32);
    alloc.reset();
    CHECK(alloc.used() == 0);
    CHECK(alloc.remaining() == 64);
    void *p = alloc.allocate(64, 1); // full buffer available again
    CHECK(p != nullptr);
}

TEST_CASE("mark and rewind reclaim only the storage taken since the mark") {
    StackAllocator<128> alloc;
    (void)alloc.allocate(16, 1);
    const auto marker = alloc.mark();
    (void)alloc.allocate(32, 1);
    CHECK(alloc.used() == 48);
    alloc.rewind(marker);
    CHECK(alloc.used() == 16);
}
