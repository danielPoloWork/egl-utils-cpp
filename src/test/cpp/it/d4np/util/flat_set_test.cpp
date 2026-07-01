// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for FlatSet<T, Compare> (component #15, roadmap 4.1): the sorted-vector set. Covers
// unique insertion, sorted iteration, lookup, erase, the initializer-list / range / comparator
// constructors, and a custom (descending) comparator.
#include <doctest/doctest.h>

#include <it/d4np/util/flat_set.hpp>

#include <functional>
#include <string>
#include <vector>

namespace {

using it::d4np::util::FlatSet;

template <typename Set> [[nodiscard]] std::vector<typename Set::value_type> to_vector(const Set &set) {
    return {set.begin(), set.end()};
}

} // namespace

TEST_CASE("a default-constructed set is empty") {
    FlatSet<int> set;
    CHECK(set.empty());
    CHECK(set.size() == 0);
    CHECK(set.begin() == set.end());
}

TEST_CASE("insert keeps elements sorted regardless of insertion order") {
    FlatSet<int> set;
    for (int v : {5, 1, 4, 2, 3}) {
        set.insert(v);
    }
    CHECK(set.size() == 5);
    CHECK(to_vector(set) == std::vector<int>{1, 2, 3, 4, 5});
}

TEST_CASE("insert reports whether the element was new") {
    FlatSet<int> set;
    const auto first = set.insert(42);
    CHECK(first.second);
    CHECK(*first.first == 42);

    const auto again = set.insert(42);
    CHECK_FALSE(again.second); // duplicate rejected
    CHECK(set.size() == 1);
}

TEST_CASE("initializer-list construction sorts and deduplicates") {
    FlatSet<int> set{3, 1, 2, 3, 1};
    CHECK(set.size() == 3);
    CHECK(to_vector(set) == std::vector<int>{1, 2, 3});
}

TEST_CASE("range construction works from any input range") {
    const std::vector<int> source{9, 7, 8, 7};
    FlatSet<int> set(source.begin(), source.end());
    CHECK(to_vector(set) == std::vector<int>{7, 8, 9});
}

TEST_CASE("lookup: find, contains, count") {
    FlatSet<std::string> set{"apple", "cherry", "banana"};
    CHECK(set.contains("banana"));
    CHECK(set.count("cherry") == 1);
    CHECK(*set.find("apple") == "apple");
    CHECK(set.find("durian") == set.end());
    CHECK(set.count("durian") == 0);
}

TEST_CASE("erase by key removes only the matching element") {
    FlatSet<int> set{1, 2, 3, 4};
    CHECK(set.erase(3) == 1);
    CHECK(set.erase(3) == 0); // already gone
    CHECK(to_vector(set) == std::vector<int>{1, 2, 4});
}

TEST_CASE("erase by iterator returns the following position") {
    FlatSet<int> set{10, 20, 30};
    const auto next = set.erase(set.find(20));
    CHECK(*next == 30);
    CHECK(to_vector(set) == std::vector<int>{10, 30});
}

TEST_CASE("lower_bound and upper_bound bracket a key") {
    FlatSet<int> set{10, 20, 30, 40};
    CHECK(*set.lower_bound(20) == 20);
    CHECK(*set.upper_bound(20) == 30);
    CHECK(set.lower_bound(100) == set.end());
}

TEST_CASE("a custom comparator controls the ordering") {
    FlatSet<int, std::greater<>> set{1, 3, 2, 3};
    CHECK(set.size() == 3);
    CHECK(to_vector(set) == std::vector<int>{3, 2, 1}); // descending
    CHECK(set.contains(2));
    CHECK(*set.begin() == 3);
}

TEST_CASE("reserve does not change the contents") {
    FlatSet<int> set{1, 2, 3};
    set.reserve(64);
    CHECK(set.capacity() >= 64);
    CHECK(to_vector(set) == std::vector<int>{1, 2, 3});
}
