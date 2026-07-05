// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// FlatMap<Key, Value, Compare> (component #14): an ordered key/value map stored as a single
// sorted, contiguous std::vector of pairs. Like FlatSet (ADR-0009) it trades O(n) insert/erase
// for O(log n) lookup and cache-friendly iteration. It additionally supports constexpr use
// (ADR-0010): the whole API is constexpr, and construction sorts via std::sort at runtime but
// via sorted insertion during constant evaluation (std::sort is not constexpr before C++26).
// Header-only.
#ifndef IT_D4NP_UTIL_FLAT_MAP_HPP
#define IT_D4NP_UTIL_FLAT_MAP_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace it::d4np::util {

/// A sorted, contiguous map with unique keys, binary-search lookup, and constexpr support.
///
/// Elements are `std::pair<Key, Value>` kept ordered by `Compare` on the key. Lookups
/// (`find`, `contains`, `count`, `at`, `lower_bound`, `upper_bound`) are O(log n); `insert`,
/// `operator[]`, and `erase` are O(n) because they shift the tail. When several entries share a
/// key (e.g. in an initializer list) the first one wins.
///
/// The entire interface is `constexpr`: a `FlatMap` can be built and queried inside a constant
/// expression (e.g. behind a `static_assert`). `find` returns a `const_iterator` compared against
/// `end()` (the STL-idiomatic result), not an optional:
///
/// ```cpp
/// constexpr bool ok = [] {
///     it::d4np::util::FlatMap<int, std::string_view> config;
///     config.insert(1, "Service.Start");
///     config.insert(2, "Service.Stop");
///     const auto it = config.find(1);                     // const_iterator, not optional
///     return it != config.end() && it->second == "Service.Start";
/// }();
/// static_assert(ok);
/// ```
///
/// See ADR-0009 (flat model) and ADR-0010 (the constexpr construction strategy).
///
/// @tparam Key the key type.
/// @tparam Value the mapped type.
/// @tparam Compare a strict-weak ordering over `Key` (default `std::less<Key>`).
/// @note Not thread-safe. Iterators are const (mutating a key in place could break ordering);
///       mutate values through `at()` or `operator[]`. Any insert/erase invalidates iterators.
template <typename Key, typename Value, typename Compare = std::less<Key>> class FlatMap {
  public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using size_type = std::size_t;
    using key_compare = Compare;
    using const_iterator = typename std::vector<value_type>::const_iterator;
    using iterator = const_iterator;

    constexpr FlatMap() = default;
    constexpr explicit FlatMap(Compare comp) : comp_(std::move(comp)) {}

    constexpr FlatMap(std::initializer_list<value_type> init, Compare comp = Compare{}) : comp_(std::move(comp)) {
        build_from(init.begin(), init.end());
    }

    template <typename InputIt>
    constexpr FlatMap(InputIt first, InputIt last, Compare comp = Compare{}) : comp_(std::move(comp)) {
        build_from(first, last);
    }

    // --- Capacity --------------------------------------------------------------------------
    [[nodiscard]] constexpr size_type size() const noexcept { return data_.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return data_.empty(); }
    [[nodiscard]] constexpr size_type capacity() const noexcept { return data_.capacity(); }
    constexpr void reserve(size_type count) { data_.reserve(count); }
    constexpr void clear() noexcept { data_.clear(); }

    // --- Iterators (const only) ------------------------------------------------------------
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_.begin(); }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return data_.end(); }
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return data_.cbegin(); }
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return data_.cend(); }

    // --- Lookup ----------------------------------------------------------------------------
    [[nodiscard]] constexpr const_iterator lower_bound(const Key &key) const {
        return std::lower_bound(data_.begin(), data_.end(), key, element_less());
    }
    [[nodiscard]] constexpr const_iterator upper_bound(const Key &key) const {
        return std::upper_bound(data_.begin(), data_.end(), key, key_less());
    }
    [[nodiscard]] constexpr const_iterator find(const Key &key) const {
        const auto pos = lower_bound(key);
        if (pos != data_.end() && !comp_(key, pos->first)) {
            return pos;
        }
        return data_.end();
    }
    [[nodiscard]] constexpr bool contains(const Key &key) const { return find(key) != data_.end(); }
    [[nodiscard]] constexpr size_type count(const Key &key) const {
        return contains(key) ? size_type{1} : size_type{0};
    }

    /// Bounded access to the value for `key`. @throws std::out_of_range if the key is absent.
    [[nodiscard]] constexpr Value &at(const Key &key) {
        const auto pos = locate(data_, key);
        if (pos == data_.end()) {
            throw std::out_of_range("FlatMap::at: key not found");
        }
        return pos->second;
    }
    [[nodiscard]] constexpr const Value &at(const Key &key) const {
        const auto pos = locate(data_, key);
        if (pos == data_.end()) {
            throw std::out_of_range("FlatMap::at: key not found");
        }
        return pos->second;
    }

    /// Returns a reference to the value for `key`, inserting a value-initialized one if absent.
    constexpr Value &operator[](const Key &key) {
        auto pos = std::lower_bound(data_.begin(), data_.end(), key, element_less());
        if (pos == data_.end() || comp_(key, pos->first)) {
            pos = data_.insert(pos, value_type{key, Value{}});
        }
        return pos->second;
    }

    // --- Modifiers -------------------------------------------------------------------------
    /// Inserts `kv` if its key is not already present. Returns the position and whether it was
    /// inserted (false when an entry with an equivalent key already existed — the old one stays).
    constexpr std::pair<const_iterator, bool> insert(const value_type &kv) { return insert_impl(kv); }
    constexpr std::pair<const_iterator, bool> insert(value_type &&kv) { return insert_impl(std::move(kv)); }

    /// Convenience overload constructing the pair from a key and value.
    constexpr std::pair<const_iterator, bool> insert(Key key, Value value) {
        return insert_impl(value_type{std::move(key), std::move(value)});
    }

    /// Erases the entry for `key`, if any. Returns the number removed (0 or 1).
    constexpr size_type erase(const Key &key) {
        const auto pos = locate(data_, key);
        if (pos == data_.end()) {
            return 0;
        }
        data_.erase(pos);
        return 1;
    }
    constexpr const_iterator erase(const_iterator pos) { return data_.erase(pos); }

  private:
    // Comparator adapting Compare to (element, key) order, for lower_bound.
    [[nodiscard]] constexpr auto element_less() const {
        return [this](const value_type &element, const Key &key) { return comp_(element.first, key); };
    }
    // Comparator adapting Compare to (key, element) order, for upper_bound.
    [[nodiscard]] constexpr auto key_less() const {
        return [this](const Key &key, const value_type &element) { return comp_(key, element.first); };
    }

    // Returns an iterator to the entry with the given key, or `container.end()` if absent.
    // Templated on the container reference so the const and non-const `at` overloads share it
    // and each yields the matching iterator constness; uses the member comparator.
    template <typename Container> constexpr auto locate(Container &container, const Key &key) const {
        auto pos = std::lower_bound(container.begin(), container.end(), key, element_less());
        if (pos != container.end() && !comp_(key, pos->first)) {
            return pos;
        }
        return container.end();
    }

    template <typename Pair> constexpr std::pair<const_iterator, bool> insert_impl(Pair &&kv) {
        auto pos = std::lower_bound(data_.begin(), data_.end(), kv.first, element_less());
        if (pos != data_.end() && !comp_(kv.first, pos->first)) {
            return {pos, false};
        }
        return {data_.insert(pos, std::forward<Pair>(kv)), true};
    }

    template <typename InputIt> constexpr void build_from(InputIt first, InputIt last) {
        if (std::is_constant_evaluated()) {
            // first key wins; sorted insertion is constexpr-legal (std::sort is not, pre-C++26).
            // std::for_each keeps the iterator increment out of user-level pointer arithmetic.
            std::for_each(first, last, [this](const value_type &kv) { insert(kv); });
        } else {
            data_.assign(first, last);
            std::stable_sort(data_.begin(), data_.end(), [this](const value_type &lhs, const value_type &rhs) {
                return comp_(lhs.first, rhs.first);
            });
            const auto same_key = [this](const value_type &lhs, const value_type &rhs) {
                return !comp_(lhs.first, rhs.first) && !comp_(rhs.first, lhs.first);
            };
            data_.erase(std::unique(data_.begin(), data_.end(), same_key), data_.end());
        }
    }

    std::vector<value_type> data_;
    Compare comp_{};
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_FLAT_MAP_HPP
