// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for UniqueRef<T> (component #2, roadmap 3.1): the non-null unique-ownership smart
// pointer. Compile-time guarantees (no null/default/copy construction) are asserted with
// static_assert; ownership and lifetime behavior is exercised at runtime and verified under
// ASan/Valgrind in CI.
#include <doctest/doctest.h>

#include <it/d4np/util/unique_ref.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace {

using it::d4np::util::make_unique_ref;
using it::d4np::util::UniqueRef;

// A type whose live-instance count lets tests prove exactly one construction/destruction.
// Neither copyable nor movable, so every instance is owned solely through its UniqueRef.
struct Tracked {
    static inline int live = 0;
    int value;
    explicit Tracked(int v) : value(v) { ++live; }
    ~Tracked() { --live; }
    Tracked(const Tracked &) = delete;
    Tracked &operator=(const Tracked &) = delete;
    Tracked(Tracked &&) = delete;
    Tracked &operator=(Tracked &&) = delete;
};

struct Base {
    Base() = default;
    Base(const Base &) = default;
    Base &operator=(const Base &) = default;
    Base(Base &&) = default;
    Base &operator=(Base &&) = default;
    virtual ~Base() = default;
    [[nodiscard]] virtual int tag() const { return 0; }
};
struct Derived : Base {
    [[nodiscard]] int tag() const override { return 42; }
};

// --- Compile-time contract: there is no reachable null state -------------------------------
static_assert(!std::is_default_constructible_v<UniqueRef<int>>, "UniqueRef must not be default-constructible");
static_assert(!std::is_constructible_v<UniqueRef<int>, std::nullptr_t>, "UniqueRef must not accept nullptr");
static_assert(!std::is_copy_constructible_v<UniqueRef<int>>, "UniqueRef must be move-only");
static_assert(!std::is_copy_assignable_v<UniqueRef<int>>, "UniqueRef must be move-only");
static_assert(std::is_move_constructible_v<UniqueRef<int>>, "UniqueRef must be move-constructible");
static_assert(std::is_move_assignable_v<UniqueRef<int>>, "UniqueRef must be move-assignable");
static_assert(std::is_nothrow_move_constructible_v<UniqueRef<int>>, "moving must be noexcept");
static_assert(sizeof(UniqueRef<int>) == sizeof(int *), "UniqueRef must be a thin pointer wrapper");
static_assert(!std::is_convertible_v<UniqueRef<Base>, UniqueRef<Derived>>, "no base-to-derived conversion");

} // namespace

TEST_CASE("make_unique_ref constructs an owned, dereferenceable object") {
    auto ref = make_unique_ref<int>(7);
    CHECK(*ref == 7);
    CHECK(ref.get() != nullptr);
    *ref = 99;
    CHECK(*ref == 99);
}

TEST_CASE("operator-> forwards to the owned object") {
    struct Point {
        int x, y;
    };
    auto ref = make_unique_ref<Point>(Point{.x = 3, .y = 4});
    CHECK(ref->x == 3);
    CHECK(ref->y == 4);
}

TEST_CASE("ownership is released exactly once when the ref is destroyed") {
    REQUIRE(Tracked::live == 0);
    {
        auto ref = make_unique_ref<Tracked>(1);
        CHECK(Tracked::live == 1);
        CHECK(ref->value == 1);
    }
    CHECK(Tracked::live == 0);
}

TEST_CASE("move transfers ownership without constructing or destroying the object") {
    REQUIRE(Tracked::live == 0);
    auto src = make_unique_ref<Tracked>(5);
    CHECK(Tracked::live == 1);

    auto dst = std::move(src);
    CHECK(Tracked::live == 1); // no extra construction/destruction on transfer
    CHECK(dst->value == 5);
    CHECK(src.get() == nullptr); // NOLINT(bugprone-use-after-move) — asserting the moved-from state
}

TEST_CASE("move assignment replaces the target's object") {
    REQUIRE(Tracked::live == 0);
    auto a = make_unique_ref<Tracked>(1);
    auto b = make_unique_ref<Tracked>(2);
    CHECK(Tracked::live == 2);

    a = std::move(b);
    CHECK(Tracked::live == 1); // a's original object was released
    CHECK(a->value == 2);
}

TEST_CASE("converting move from derived to base preserves polymorphic ownership") {
    REQUIRE(Tracked::live == 0);
    UniqueRef<Derived> derived = make_unique_ref<Derived>();
    UniqueRef<Base> base = std::move(derived);
    CHECK(base->tag() == 42);
    CHECK(base.get() != nullptr);
}

TEST_CASE("to_unique_ptr hands ownership to a std::unique_ptr") {
    REQUIRE(Tracked::live == 0);
    std::unique_ptr<Tracked> owned;
    {
        auto ref = make_unique_ref<Tracked>(8);
        owned = std::move(ref).to_unique_ptr();
        CHECK(ref.get() == nullptr); // NOLINT(bugprone-use-after-move) — moved-from is expected
    }
    CHECK(Tracked::live == 1); // still alive: the unique_ptr owns it now
    CHECK(owned->value == 8);
    owned.reset();
    CHECK(Tracked::live == 0);
}

TEST_CASE("swap exchanges the owned objects") {
    auto a = make_unique_ref<int>(1);
    auto b = make_unique_ref<int>(2);
    swap(a, b);
    CHECK(*a == 2);
    CHECK(*b == 1);
}
