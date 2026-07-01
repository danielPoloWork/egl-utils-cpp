// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// CircularBuffer<T> (component #13): a fixed-capacity ring buffer (FIFO) for streaming. Storage
// is a single std::vector<T> sized once at construction and reused via head/count indices — no
// per-element allocation, no reallocation, O(1) push/pop (ADR-0011). Two push policies are
// offered: try_push rejects when full; push_overwrite drops the oldest element to make room.
// Header-only.
#ifndef IT_D4NP_UTIL_CIRCULAR_BUFFER_HPP
#define IT_D4NP_UTIL_CIRCULAR_BUFFER_HPP

#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace it::d4np::util {

/// A fixed-capacity circular (ring) buffer with FIFO semantics.
///
/// Elements are pushed at the back and popped from the front; the oldest element is `front()`
/// and the newest is `back()`. Capacity is fixed at construction and the buffer never
/// reallocates. `try_push` reports failure when full; `push_overwrite` instead discards the
/// oldest element so the newest always fits — the ring-buffer behavior wanted for live streams.
///
/// @tparam T the element type. Must be default-constructible (the backing slots are
///         value-initialized up front) and assignable.
/// @note Not thread-safe. @pre The capacity passed to the constructor must be non-zero.
///       `front()`/`back()` on an empty buffer are undefined (asserted in debug builds).
template <typename T> class CircularBuffer {
  public:
    using value_type = T;
    using size_type = std::size_t;

    /// Constructs an empty buffer that can hold up to `capacity` elements.
    explicit CircularBuffer(size_type capacity) : buffer_(capacity) {
        assert(capacity > 0 && "CircularBuffer capacity must be non-zero");
    }

    // --- Capacity --------------------------------------------------------------------------
    [[nodiscard]] size_type capacity() const noexcept { return buffer_.size(); }
    [[nodiscard]] size_type size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] bool full() const noexcept { return count_ == buffer_.size(); }
    void clear() noexcept {
        head_ = 0;
        count_ = 0;
    }

    // --- Push / pop ------------------------------------------------------------------------
    /// Appends `value` if there is room. Returns false (leaving the buffer unchanged) when full.
    [[nodiscard]] bool try_push(const T &value) { return push_impl(value); }
    [[nodiscard]] bool try_push(T &&value) { return push_impl(std::move(value)); }

    /// Appends `value`, discarding the oldest element first if the buffer is full.
    void push_overwrite(const T &value) { push_overwrite_impl(value); }
    void push_overwrite(T &&value) { push_overwrite_impl(std::move(value)); }

    /// Removes and returns the oldest element, or `std::nullopt` if the buffer is empty.
    [[nodiscard]] std::optional<T> pop() {
        if (empty()) {
            return std::nullopt;
        }
        std::optional<T> value(std::move(buffer_[head_]));
        head_ = advance(head_);
        --count_;
        return value;
    }

    // --- Access (undefined when empty) -----------------------------------------------------
    [[nodiscard]] T &front() noexcept {
        assert(!empty());
        return buffer_[head_];
    }
    [[nodiscard]] const T &front() const noexcept {
        assert(!empty());
        return buffer_[head_];
    }
    [[nodiscard]] T &back() noexcept {
        assert(!empty());
        return buffer_[back_index()];
    }
    [[nodiscard]] const T &back() const noexcept {
        assert(!empty());
        return buffer_[back_index()];
    }

  private:
    // Next index after `index`, wrapping to 0 at capacity (no modulo / division).
    [[nodiscard]] size_type advance(size_type index) const noexcept {
        const size_type next = index + 1;
        return next == buffer_.size() ? size_type{0} : next;
    }
    // Index of the slot one past the last element (where the next push lands).
    [[nodiscard]] size_type tail_index() const noexcept {
        size_type tail = head_ + count_;
        if (tail >= buffer_.size()) {
            tail -= buffer_.size();
        }
        return tail;
    }
    // Index of the newest element; valid only when the buffer is non-empty.
    [[nodiscard]] size_type back_index() const noexcept {
        size_type back = head_ + count_ - 1;
        if (back >= buffer_.size()) {
            back -= buffer_.size();
        }
        return back;
    }

    template <typename U> bool push_impl(U &&value) {
        if (full()) {
            return false;
        }
        buffer_[tail_index()] = std::forward<U>(value);
        ++count_;
        return true;
    }

    template <typename U> void push_overwrite_impl(U &&value) {
        if (full()) {
            // Reuse the oldest slot for the new element, then rotate: the overwritten slot
            // becomes the newest position and head_ moves to the next-oldest element.
            buffer_[head_] = std::forward<U>(value);
            head_ = advance(head_);
        } else {
            buffer_[tail_index()] = std::forward<U>(value);
            ++count_;
        }
    }

    std::vector<T> buffer_;
    size_type head_ = 0;  // index of the oldest element
    size_type count_ = 0; // number of elements currently stored
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_CIRCULAR_BUFFER_HPP
