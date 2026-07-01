// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// StackAllocator<Size> (component #3): a bump (monotonic) allocator over a fixed-size buffer
// held inside the object. Declared as a local, its storage lives on the stack, giving fast,
// allocation-free scratch space for short-lived data. Allocation is a pointer bump with
// alignment; there is no per-allocation free — memory is reclaimed wholesale via reset(), or
// back to a saved point via mark()/rewind(). This is the "purpose requires raw memory" case
// carved out by ADR-0005 (see ADR-0007). Header-only.
#ifndef IT_D4NP_UTIL_STACK_ALLOCATOR_HPP
#define IT_D4NP_UTIL_STACK_ALLOCATOR_HPP

#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>

namespace it::d4np::util {

/// A monotonic bump allocator backed by `Size` bytes of in-object storage.
///
/// Each `allocate` call returns the next suitably-aligned slice of the buffer and advances an
/// internal offset; it never reuses freed space. Reclaim everything with `reset()`, or unwind
/// to an earlier high-water mark with `mark()`/`rewind()`. Because allocations point directly
/// into the object's buffer, the allocator is neither copyable nor movable — moving it would
/// dangle every outstanding pointer.
///
/// @tparam Size total capacity in bytes. The buffer is aligned for any scalar type
///         (`alignof(std::max_align_t)`).
/// @note Not thread-safe. Returned storage is raw and uninitialized: the caller is responsible
///       for constructing (and, for non-trivial types, destroying) objects within it.
template <std::size_t Size> class StackAllocator {
    static_assert(Size > 0, "StackAllocator<Size> requires a non-zero capacity");

  public:
    using size_type = std::size_t;

    StackAllocator() = default;
    StackAllocator(const StackAllocator &) = delete;
    StackAllocator &operator=(const StackAllocator &) = delete;
    StackAllocator(StackAllocator &&) = delete;
    StackAllocator &operator=(StackAllocator &&) = delete;
    ~StackAllocator() = default;

    /// Allocates `bytes` of storage aligned to `alignment`.
    /// @param alignment a power of two (asserted in debug builds).
    /// @return a pointer to uninitialized, suitably-aligned storage.
    /// @throws std::bad_alloc if the remaining capacity cannot satisfy the request.
    [[nodiscard]] void *allocate(size_type bytes, size_type alignment = alignof(std::max_align_t)) {
        assert(alignment > 0 && (alignment & (alignment - 1)) == 0 && "alignment must be a power of two");

        std::byte *base = buffer_.data();
        void *cursor = std::next(base, static_cast<std::ptrdiff_t>(offset_));
        size_type space = Size - offset_;
        void *aligned = std::align(alignment, bytes, cursor, space);
        if (aligned == nullptr) {
            throw std::bad_alloc{};
        }

        auto *aligned_bytes = static_cast<std::byte *>(aligned);
        offset_ = static_cast<size_type>(std::distance(base, aligned_bytes)) + bytes;
        return aligned;
    }

    /// Allocates uninitialized storage for `count` objects of type `T`, aligned to `alignof(T)`.
    template <typename T> [[nodiscard]] T *allocate_uninitialized(size_type count = 1) {
        return static_cast<T *>(allocate(count * sizeof(T), alignof(T)));
    }

    /// Reclaims all storage; every previously returned pointer becomes invalid.
    void reset() noexcept { offset_ = 0; }

    /// Current high-water mark, for use with rewind().
    [[nodiscard]] size_type mark() const noexcept { return offset_; }

    /// Reclaims storage allocated since `marker` (a value previously returned by mark()).
    void rewind(size_type marker) noexcept {
        assert(marker <= offset_ && "rewind marker must not exceed the current offset");
        offset_ = marker;
    }

    [[nodiscard]] static constexpr size_type capacity() noexcept { return Size; }
    [[nodiscard]] size_type used() const noexcept { return offset_; }
    [[nodiscard]] size_type remaining() const noexcept { return Size - offset_; }

  private:
    // Zero-initialized on construction: it makes the member-init contract explicit and costs a
    // single one-time fill; allocations still hand back raw storage the caller must set up.
    alignas(std::max_align_t) std::array<std::byte, Size> buffer_{};
    size_type offset_ = 0;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_STACK_ALLOCATOR_HPP
