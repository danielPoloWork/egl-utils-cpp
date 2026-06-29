// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Compile-time type-property utilities (component #25). These are the foundational traits
// the rest of egl-util-cpp builds on: a dependent always-false for static_assert, the
// detection idiom for probing member/expression validity, and helpers to recognise template
// specializations and type-set membership. Everything here is header-only and evaluated
// entirely at compile time.
#ifndef IT_D4NP_UTIL_TYPE_TRAITS_HPP
#define IT_D4NP_UTIL_TYPE_TRAITS_HPP

#include <type_traits>

namespace it::d4np::util {

/// A `false` that depends on its template parameters, so it is only evaluated when the
/// enclosing template is instantiated. Use it to fail a `static_assert` in an otherwise
/// unreachable `else`/default branch (a bare `static_assert(false, ...)` is ill-formed).
template <typename...> inline constexpr bool always_false_v = false;

namespace detail {

/// Sentinel type returned by the detection idiom when an expression is not detected. It is
/// not constructible, copyable, or movable, so accidental use is a hard error.
struct nonesuch {
    nonesuch() = delete;
    ~nonesuch() = delete;
    nonesuch(const nonesuch &) = delete;
    nonesuch(nonesuch &&) = delete;
    nonesuch &operator=(const nonesuch &) = delete;
    nonesuch &operator=(nonesuch &&) = delete;
};

template <typename Default, typename AlwaysVoid, template <typename...> class Op, typename... Args> struct detector {
    using value_t = std::false_type;
    using type = Default;
};

template <typename Default, template <typename...> class Op, typename... Args>
struct detector<Default, std::void_t<Op<Args...>>, Op, Args...> {
    using value_t = std::true_type;
    using type = Op<Args...>;
};

} // namespace detail

/// `std::true_type` when `Op<Args...>` is a valid type, else `std::false_type`.
/// `Op` is an alias template describing the expression to probe (the detection idiom).
template <template <typename...> class Op, typename... Args>
using is_detected = typename detail::detector<detail::nonesuch, void, Op, Args...>::value_t;

/// `true` when `Op<Args...>` is well-formed.
template <template <typename...> class Op, typename... Args>
inline constexpr bool is_detected_v = is_detected<Op, Args...>::value;

/// `Op<Args...>` when well-formed, otherwise `detail::nonesuch`.
template <template <typename...> class Op, typename... Args>
using detected_t = typename detail::detector<detail::nonesuch, void, Op, Args...>::type;

/// `Op<Args...>` when well-formed, otherwise the supplied `Default`.
template <typename Default, template <typename...> class Op, typename... Args>
using detected_or_t = typename detail::detector<Default, void, Op, Args...>::type;

/// `true` when `T` is a specialization of the primary class template `Primary`
/// (e.g. `is_specialization_of_v<std::vector<int>, std::vector>`). Only matches templates
/// whose parameters are all types.
template <typename T, template <typename...> class Primary> struct is_specialization_of : std::false_type {};

template <template <typename...> class Primary, typename... Args>
struct is_specialization_of<Primary<Args...>, Primary> : std::true_type {};

template <typename T, template <typename...> class Primary>
inline constexpr bool is_specialization_of_v = is_specialization_of<T, Primary>::value;

/// `true` when `T` is the same type (ignoring nothing — exact match) as one of `Us...`.
template <typename T, typename... Us> inline constexpr bool is_any_of_v = (std::is_same_v<T, Us> || ...);

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_TYPE_TRAITS_HPP
