// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// ObjectPool<T> (component #1): a thread-safe, fixed-capacity pool of pre-allocated objects.
// It implements the Object Pool creational pattern (ADR-0008): a fixed set of objects is
// constructed once, then lent out and reclaimed so hot paths avoid per-use allocation and
// construction. Acquire and release are O(1) (a mutex-guarded free list of slot indices).
// Borrowed objects are handed out through a move-only RAII Handle that returns the slot to
// the pool on destruction. Header-only.
#ifndef IT_D4NP_UTIL_OBJECT_POOL_HPP
#define IT_D4NP_UTIL_OBJECT_POOL_HPP

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace it::d4np::util {

/// A fixed-capacity pool of reusable `T` objects with O(1), thread-safe acquire/release.
///
/// The pool constructs `capacity` objects up front (value-initialized, or produced by a
/// factory) and lends them via `try_acquire()`, which returns an engaged `std::optional`
/// holding a @ref Handle when a slot is free and `std::nullopt` when the pool is exhausted.
/// The handle grants exclusive access to one object and returns it to the pool when destroyed.
///
/// @tparam T the pooled object type.
/// @note Thread-safe: concurrent `try_acquire`/release and the size queries are serialized by
///       an internal mutex. A borrowed object is owned exclusively by its handle, so the user
///       needs no further synchronization to use it. Reused objects retain their previous
///       state — reinitialize on acquire if that matters. The pool must outlive every handle.
template <typename T> class ObjectPool {
  public:
    using size_type = std::size_t;

    /// Move-only RAII borrow of a pooled object; returns the slot to the pool on destruction.
    class Handle {
      public:
        Handle(const Handle &) = delete;
        Handle &operator=(const Handle &) = delete;
        Handle(Handle &&other) noexcept : pool_(std::exchange(other.pool_, nullptr)), index_(other.index_) {}
        Handle &operator=(Handle &&other) noexcept {
            if (this != &other) {
                release_slot();
                pool_ = std::exchange(other.pool_, nullptr);
                index_ = other.index_;
            }
            return *this;
        }
        ~Handle() { release_slot(); }

        [[nodiscard]] T &operator*() const noexcept { return pool_->storage_[index_]; }
        [[nodiscard]] T *operator->() const noexcept { return std::addressof(pool_->storage_[index_]); }
        [[nodiscard]] T &get() const noexcept { return pool_->storage_[index_]; }

      private:
        friend class ObjectPool;
        Handle(ObjectPool *pool, size_type index) noexcept : pool_(pool), index_(index) {}
        void release_slot() noexcept {
            if (pool_ != nullptr) {
                pool_->release(index_);
                pool_ = nullptr;
            }
        }

        ObjectPool *pool_ = nullptr;
        size_type index_ = 0;
    };

    /// Constructs a pool of `capacity` value-initialized `T` objects.
    explicit ObjectPool(size_type capacity) : storage_(capacity) { init_free_list(); }

    /// Constructs a pool of `capacity` objects, each produced by calling `factory()`.
    template <typename Factory> ObjectPool(size_type capacity, Factory factory) {
        storage_.reserve(capacity);
        for (size_type i = 0; i < capacity; ++i) {
            storage_.emplace_back(factory());
        }
        init_free_list();
    }

    ObjectPool(const ObjectPool &) = delete;
    ObjectPool &operator=(const ObjectPool &) = delete;
    ObjectPool(ObjectPool &&) = delete;
    ObjectPool &operator=(ObjectPool &&) = delete;
    ~ObjectPool() = default;

    /// Borrows an object if one is free. Returns `std::nullopt` when the pool is exhausted.
    [[nodiscard]] std::optional<Handle> try_acquire() {
        const std::scoped_lock lock(mutex_);
        if (free_count_ == 0) {
            return std::nullopt;
        }
        --free_count_;
        return Handle(this, free_slots_[free_count_]);
    }

    /// Total number of objects the pool holds (fixed for the pool's lifetime).
    [[nodiscard]] size_type capacity() const noexcept { return storage_.size(); }

    /// Number of objects currently available to borrow.
    [[nodiscard]] size_type available() const {
        const std::scoped_lock lock(mutex_);
        return free_count_;
    }

    /// Number of objects currently borrowed.
    [[nodiscard]] size_type in_use() const {
        const std::scoped_lock lock(mutex_);
        return storage_.size() - free_count_;
    }

  private:
    void init_free_list() {
        free_slots_.resize(storage_.size());
        for (size_type i = 0; i < storage_.size(); ++i) {
            free_slots_[i] = i;
        }
        free_count_ = storage_.size();
    }

    // noexcept: the free-slot stack is pre-sized to capacity and never holds more than
    // capacity entries, so the write below cannot allocate or throw.
    void release(size_type index) noexcept {
        const std::scoped_lock lock(mutex_);
        free_slots_[free_count_] = index;
        ++free_count_;
    }

    std::vector<T> storage_;            // fixed after construction; addresses/size never change
    std::vector<size_type> free_slots_; // stack of available slot indices (guarded by mutex_)
    size_type free_count_ = 0;          // number of valid entries at the top of free_slots_
    mutable std::mutex mutex_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_OBJECT_POOL_HPP
