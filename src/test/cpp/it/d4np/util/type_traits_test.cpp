// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for it::d4np::util type traits (component #25). The traits are compile-time, so the
// primary proof is a battery of static_asserts (a regression fails the build); the TEST_CASE
// exists so the suite reports a runtime check too.
#include <doctest/doctest.h>

#include <it/d4np/util/type_traits.hpp>

#include <string>
#include <type_traits>
#include <vector>

namespace {

// always_false_v is always false, regardless of the type arguments.
static_assert(!it::d4np::util::always_false_v<int>);
static_assert(!it::d4np::util::always_false_v<int, double, std::string>);

// Detection idiom: probe for a nested ::value_type.
template <typename T> using value_type_t = typename T::value_type;

static_assert(it::d4np::util::is_detected_v<value_type_t, std::vector<int>>);
static_assert(!it::d4np::util::is_detected_v<value_type_t, int>);
static_assert(std::is_same_v<it::d4np::util::detected_t<value_type_t, std::vector<int>>, int>);
static_assert(std::is_same_v<it::d4np::util::detected_or_t<long, value_type_t, int>, long>);

// is_specialization_of: recognise a primary class template's specializations.
static_assert(it::d4np::util::is_specialization_of_v<std::vector<int>, std::vector>);
static_assert(!it::d4np::util::is_specialization_of_v<int, std::vector>);

// is_any_of_v: exact membership in a type set.
static_assert(it::d4np::util::is_any_of_v<int, char, int, double>);
static_assert(!it::d4np::util::is_any_of_v<float, char, int, double>);

} // namespace

TEST_CASE("type traits resolve at compile time") {
    CHECK(it::d4np::util::is_detected_v<value_type_t, std::vector<int>>);
    CHECK_FALSE(it::d4np::util::is_detected_v<value_type_t, int>);
    CHECK(it::d4np::util::is_specialization_of_v<std::vector<int>, std::vector>);
    CHECK(it::d4np::util::is_any_of_v<int, char, int, double>);
}
