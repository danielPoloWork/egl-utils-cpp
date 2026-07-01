// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// FlatSet<T, Compare> (component #15): an ordered set stored as a single sorted, contiguous
// std::vector. Lookups are O(log n) binary searches; insert/erase are O(n) (a shift), traded
// for cache-friendly iteration and a compact footprint versus a node-based std::set
// (ADR-0009). Elements are immutable through the container (mutating one would break the sort
// invariant), so all iterators are const. Header-only.
#ifndef IT_D4NP_UTIL_FLAT_SET_HPP
#define IT_D4NP_UTIL_FLAT_SET_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace it::d4np::util {

/// A sorted, contiguous set with unique elements and binary-search lookup.
///
/// `FlatSet` keeps its elements in a single `std::vector` ordered by `Compare`. Two elements
/// are considered equivalent (and thus deduplicated) when neither compares less than the
/// other. Lookups (`find`, `contains`, `count`, `lower_bound`, `upper_bound`) are O(log n);
/// `insert` and `erase` are O(n) because they shift the tail. This favors read-heavy,
/// insert-light workloads and cache-friendly iteration.
///
/// @tparam T the element type.
/// @tparam Compare a strict-weak-ordering comparator over `T` (default `std::less<T>`).
/// @note Not thread-safe. Iterators are const: elements cannot be mutated in place because that
///       could violate the ordering invariant.
template <typename T, typename Compare = std::less<T>> class FlatSet {
  public:
    using value_type = T;
    using size_type = std::size_t;
    using key_compare = Compare;
    using const_iterator = typename std::vector<T>::const_iterator;
    using iterator = const_iterator;
    using const_reference = const T &;
    using reference = const_reference;

    FlatSet() = default;
    explicit FlatSet(Compare comp) : comp_(std::move(comp)) {}

    FlatSet(std::initializer_list<T> init, Compare comp = Compare{}) : comp_(std::move(comp)) {
        data_.assign(init);
        sort_unique();
    }

    template <typename InputIt>
    FlatSet(InputIt first, InputIt last, Compare comp = Compare{}) : comp_(std::move(comp)) {
        data_.assign(first, last);
        sort_unique();
    }

    // --- Capacity --------------------------------------------------------------------------
    [[nodiscard]] size_type size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    [[nodiscard]] size_type capacity() const noexcept { return data_.capacity(); }
    void reserve(size_type count) { data_.reserve(count); }
    void clear() noexcept { data_.clear(); }

    // --- Iterators (const only) ------------------------------------------------------------
    [[nodiscard]] const_iterator begin() const noexcept { return data_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return data_.end(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data_.cbegin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return data_.cend(); }

    // --- Lookup ----------------------------------------------------------------------------
    [[nodiscard]] const_iterator lower_bound(const T &value) const {
        return std::lower_bound(data_.begin(), data_.end(), value, comp_);
    }
    [[nodiscard]] const_iterator upper_bound(const T &value) const {
        return std::upper_bound(data_.begin(), data_.end(), value, comp_);
    }
    [[nodiscard]] const_iterator find(const T &value) const {
        const auto it = lower_bound(value);
        if (it != data_.end() && !comp_(value, *it)) {
            return it;
        }
        return data_.end();
    }
    [[nodiscard]] bool contains(const T &value) const { return find(value) != data_.end(); }
    [[nodiscard]] size_type count(const T &value) const { return contains(value) ? size_type{1} : size_type{0}; }

    // --- Modifiers -------------------------------------------------------------------------
    /// Inserts `value` if no equivalent element exists. Returns the position and whether it
    /// was inserted (false when an equivalent element was already present).
    std::pair<const_iterator, bool> insert(const T &value) { return insert_impl(value); }
    std::pair<const_iterator, bool> insert(T &&value) { return insert_impl(std::move(value)); }

    /// Erases the element equivalent to `value`, if any. Returns the number removed (0 or 1).
    size_type erase(const T &value) {
        const auto it = find(value);
        if (it == data_.end()) {
            return 0;
        }
        data_.erase(it);
        return 1;
    }
    /// Erases the element at `pos` (which must be a valid, dereferenceable iterator).
    const_iterator erase(const_iterator pos) { return data_.erase(pos); }

  private:
    template <typename U> std::pair<const_iterator, bool> insert_impl(U &&value) {
        auto pos = std::lower_bound(data_.begin(), data_.end(), value, comp_);
        if (pos != data_.end() && !comp_(value, *pos)) {
            return {pos, false};
        }
        return {data_.insert(pos, std::forward<U>(value)), true};
    }

    void sort_unique() {
        std::sort(data_.begin(), data_.end(), comp_);
        const auto equivalent = [this](const T &lhs, const T &rhs) { return !comp_(lhs, rhs) && !comp_(rhs, lhs); };
        data_.erase(std::unique(data_.begin(), data_.end(), equivalent), data_.end());
    }

    std::vector<T> data_;
    Compare comp_{};
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_FLAT_SET_HPP
