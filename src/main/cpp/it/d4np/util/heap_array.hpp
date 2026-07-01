// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// HeapArray<T> (component #4): a fixed-size, heap-allocated array with bounds-checked access.
// The size is chosen once at construction and never changes — there is no push_back, resize,
// reserve, or reallocation, so the container never pays growth overhead (ADR-0006). Storage
// is delegated to std::vector<T> (the standard contiguous owner), following the ownership-by-
// composition precedent of ADR-0005; the growth-related surface is deliberately not exposed.
// The type has value (deep-copy) semantics. Header-only.
#ifndef IT_D4NP_UTIL_HEAP_ARRAY_HPP
#define IT_D4NP_UTIL_HEAP_ARRAY_HPP

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <vector>

namespace it::d4np::util {

/// A fixed-size array whose elements live on the heap in one contiguous block.
///
/// `HeapArray<T>` is the middle ground between `std::array` (size fixed at compile time, on the
/// stack) and `std::vector` (heap, but growable): the size is a runtime value fixed at
/// construction, and the container is intentionally not resizable — the growth API is not
/// exposed. Access is contiguous and cache-friendly; `at()` is bounds-checked and throws
/// `std::out_of_range`, while `operator[]` is unchecked (asserted in debug builds). The type
/// is copyable with deep-copy semantics.
///
/// @tparam T element type. The sized constructor `HeapArray(n)` value-initializes the block
///         and so requires `T` to be default-constructible; the fill and initializer-list
///         constructors require only copy-construction. See ADR-0006.
/// @note Not thread-safe; concurrent mutation requires external synchronization.
template <typename T> class HeapArray {
    using storage = std::vector<T>;

  public:
    using value_type = typename storage::value_type;
    using size_type = typename storage::size_type;
    using reference = typename storage::reference;
    using const_reference = typename storage::const_reference;
    using pointer = typename storage::pointer;
    using const_pointer = typename storage::const_pointer;
    using iterator = typename storage::iterator;
    using const_iterator = typename storage::const_iterator;

    /// Constructs an array of `size` value-initialized elements.
    explicit HeapArray(size_type size) : data_(size) {}

    /// Constructs an array of `size` copies of `value`.
    HeapArray(size_type size, const T &value) : data_(size, value) {}

    /// Constructs an array holding a copy of each element of `init`, in order.
    HeapArray(std::initializer_list<T> init) : data_(init) {}

    // Copy/move/destroy follow the rule of zero: std::vector already provides deep-copy value
    // semantics, a noexcept move that empties the source, and leak-free destruction.

    // --- Capacity --------------------------------------------------------------------------
    [[nodiscard]] size_type size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    // --- Element access --------------------------------------------------------------------
    /// Bounds-checked access. @throws std::out_of_range if `index >= size()`.
    [[nodiscard]] reference at(size_type index) { return data_.at(index); }
    [[nodiscard]] const_reference at(size_type index) const { return data_.at(index); }

    /// Unchecked access. Behavior is undefined if `index >= size()` (asserted in debug builds).
    [[nodiscard]] reference operator[](size_type index) noexcept {
        assert(index < data_.size());
        return data_[index];
    }
    [[nodiscard]] const_reference operator[](size_type index) const noexcept {
        assert(index < data_.size());
        return data_[index];
    }

    /// First / last element. Undefined if the array is empty.
    [[nodiscard]] reference front() noexcept { return data_.front(); }
    [[nodiscard]] const_reference front() const noexcept { return data_.front(); }
    [[nodiscard]] reference back() noexcept { return data_.back(); }
    [[nodiscard]] const_reference back() const noexcept { return data_.back(); }

    [[nodiscard]] pointer data() noexcept { return data_.data(); }
    [[nodiscard]] const_pointer data() const noexcept { return data_.data(); }

    // --- Iterators -------------------------------------------------------------------------
    [[nodiscard]] iterator begin() noexcept { return data_.begin(); }
    [[nodiscard]] const_iterator begin() const noexcept { return data_.begin(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data_.cbegin(); }
    [[nodiscard]] iterator end() noexcept { return data_.end(); }
    [[nodiscard]] const_iterator end() const noexcept { return data_.end(); }
    [[nodiscard]] const_iterator cend() const noexcept { return data_.cend(); }

    // --- Modifiers -------------------------------------------------------------------------
    /// Assigns `value` to every element.
    void fill(const T &value) { std::fill(data_.begin(), data_.end(), value); }

    void swap(HeapArray &other) noexcept { data_.swap(other.data_); }
    friend void swap(HeapArray &lhs, HeapArray &rhs) noexcept { lhs.swap(rhs); }

  private:
    storage data_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_HEAP_ARRAY_HPP
