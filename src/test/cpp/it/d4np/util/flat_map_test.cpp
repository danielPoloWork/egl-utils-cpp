// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for FlatMap<Key, Value, Compare> (component #14, roadmap 4.2): the sorted-vector map.
// Compile-time behavior is proven with static_assert (the whole API is constexpr); runtime
// behavior — insertion, lookup, operator[], erase, ordering, custom comparator — is covered
// with doctest.
#include <doctest/doctest.h>

#include <it/d4np/util/flat_map.hpp>

#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using it::d4np::util::FlatMap;

// --- Compile-time contract: build and query a FlatMap inside a constant expression ---------
constexpr int build_and_sum() {
    FlatMap<int, int> map;
    map.insert(2, 20);
    map.insert(1, 10);
    map.insert(3, 30);
    map.insert(1, 99); // duplicate key: rejected, original kept
    return map.at(1) + map.at(2) + map.at(3);
}
static_assert(build_and_sum() == 60, "FlatMap must be usable at compile time");

constexpr bool init_list_is_sorted_and_unique() {
    FlatMap<int, int> map{{3, 30}, {1, 10}, {2, 20}, {1, 11}};
    return map.size() == 3 && map.at(1) == 10 && map.contains(2) && !map.contains(9) && map.begin()->first == 1 &&
           std::next(map.begin(), 2)->first == 3;
}
static_assert(init_list_is_sorted_and_unique(), "constexpr initializer-list construction");

constexpr int subscript_inserts() {
    FlatMap<int, int> map;
    map[5] = 50;
    map[5] += 5;
    return map[5];
}
static_assert(subscript_inserts() == 55, "constexpr operator[] insert-then-update");

// The spec's illustrative "constexpr FlatMap" example (spec §5 / the d4np-cpp intake §3),
// corrected to the shipped iterator-returning `find` and proven here to compile *and* evaluate as
// a constant expression — the example now matches its "constexpr" title (roadmap 11.7).
constexpr bool config_map_example() {
    FlatMap<int, std::string_view> config_map;
    config_map.insert(1, "Service.Start");
    config_map.insert(2, "Service.Stop");
    const auto it = config_map.find(1); // find returns a const_iterator, not an optional
    return it != config_map.end() && it->second == "Service.Start";
}
static_assert(config_map_example(), "constexpr FlatMap construction + iterator-based find");

} // namespace

TEST_CASE("insert keeps entries ordered by key and rejects duplicate keys") {
    FlatMap<int, std::string> map;
    CHECK(map.insert(2, "two").second);
    CHECK(map.insert(1, "one").second);
    CHECK_FALSE(map.insert(1, "uno").second); // duplicate key rejected

    std::vector<int> keys;
    for (const auto &entry : map) {
        keys.push_back(entry.first);
    }
    CHECK(keys == std::vector<int>{1, 2});
    CHECK(map.at(1) == "one"); // original value kept
}

TEST_CASE("at throws for a missing key and gives mutable access for a present one") {
    FlatMap<int, int> map{{1, 10}, {2, 20}};
    CHECK(map.at(2) == 20);
    map.at(2) = 200;
    CHECK(map.at(2) == 200);
    CHECK_THROWS_AS((void)map.at(99), std::out_of_range);
}

TEST_CASE("operator[] inserts a default value then allows update") {
    FlatMap<int, int> map;
    CHECK(map[7] == 0); // value-initialized on first access
    map[7] = 70;
    CHECK(map[7] == 70);
    CHECK(map.size() == 1);
}

TEST_CASE("lookup: find, contains, count") {
    FlatMap<std::string, int> map{{"a", 1}, {"c", 3}, {"b", 2}};
    CHECK(map.contains("b"));
    CHECK(map.count("c") == 1);
    CHECK(map.find("a")->second == 1);
    CHECK(map.find("z") == map.end());
    CHECK(map.count("z") == 0);
}

TEST_CASE("erase by key removes only the matching entry") {
    FlatMap<int, int> map{{1, 10}, {2, 20}, {3, 30}};
    CHECK(map.erase(2) == 1);
    CHECK(map.erase(2) == 0);
    CHECK_FALSE(map.contains(2));
    CHECK(map.size() == 2);
}

TEST_CASE("lower_bound and upper_bound bracket a key") {
    FlatMap<int, int> map{{10, 1}, {20, 2}, {30, 3}};
    CHECK(map.lower_bound(20)->first == 20);
    CHECK(map.upper_bound(20)->first == 30);
    CHECK(map.lower_bound(100) == map.end());
}

TEST_CASE("a custom comparator controls key ordering") {
    FlatMap<int, int, std::greater<>> map{{1, 1}, {3, 3}, {2, 2}};
    std::vector<int> keys;
    for (const auto &entry : map) {
        keys.push_back(entry.first);
    }
    CHECK(keys == std::vector<int>{3, 2, 1}); // descending by key
    CHECK(map.at(2) == 2);
}
