// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// UniqueRef<T> (component #2): a non-null, unique-ownership smart pointer. It is the
// always-valid counterpart to std::unique_ptr — there is no default constructor, no
// construction from nullptr, and no operator bool, so a fully-constructed UniqueRef is
// guaranteed to own an object (see ADR-0005 for the ownership model and the single
// moved-from exception). Header-only; heap ownership is delegated to std::unique_ptr so the
// type stays leak-safe and works with any object type.
#ifndef IT_D4NP_UTIL_UNIQUE_REF_HPP
#define IT_D4NP_UTIL_UNIQUE_REF_HPP

#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace it::d4np::util {

/// A move-only smart pointer that owns exactly one heap object and is never null.
///
/// Unlike `std::unique_ptr<T>`, `UniqueRef<T>` has no empty state reachable by construction:
/// the default constructor and construction from `nullptr` are deleted, and there is no
/// `operator bool`/`reset()`. The only way to reach an empty state is to move *from* an
/// instance (or to move out via @ref to_unique_ptr); a moved-from `UniqueRef` must not be
/// dereferenced, exactly as for a moved-from `std::unique_ptr`. Construct one with
/// @ref make_unique_ref.
///
/// @tparam T the owned object type. Non-array; use a dedicated container for arrays.
/// @note Const-ness is shallow (as for `std::unique_ptr`): a `const UniqueRef<T>` still hands
///       out a mutable `T&`. Not thread-safe; external synchronization is required for shared
///       access. See ADR-0005.
template <typename T> class UniqueRef {
  public:
    using element_type = T;
    using pointer = T *;

    /// Deleted: a UniqueRef always owns an object, so there is no null/empty construction.
    UniqueRef() = delete;
    UniqueRef(std::nullptr_t) = delete;

    /// Adopts ownership of an existing object.
    /// @pre `owned != nullptr` (checked by assertion in debug builds).
    explicit UniqueRef(std::unique_ptr<T> owned) noexcept : ptr_(std::move(owned)) { assert(ptr_ != nullptr); }

    UniqueRef(const UniqueRef &) = delete;
    UniqueRef &operator=(const UniqueRef &) = delete;
    UniqueRef(UniqueRef &&) noexcept = default;
    UniqueRef &operator=(UniqueRef &&) noexcept = default;
    ~UniqueRef() = default;

    /// Converting move from `UniqueRef<U>` when `U*` is convertible to `T*` (e.g. a derived
    /// type to a base). The source is left moved-from. As with `std::unique_ptr`, deleting
    /// through a base pointer requires a virtual destructor on the base.
    template <typename U>
        requires(std::is_convertible_v<U *, T *> && !std::is_same_v<U, T>)
    UniqueRef(UniqueRef<U> &&other) noexcept : ptr_(std::move(other).to_unique_ptr()) {}

    /// The owned object. Undefined if this instance has been moved from.
    [[nodiscard]] T &operator*() const noexcept {
        assert(ptr_ != nullptr);
        return *ptr_;
    }
    [[nodiscard]] T *operator->() const noexcept {
        assert(ptr_ != nullptr);
        return ptr_.get();
    }

    /// Raw pointer to the owned object, for interop. Non-null unless moved-from.
    [[nodiscard]] T *get() const noexcept { return ptr_.get(); }

    /// Relinquishes ownership as a `std::unique_ptr`, leaving this instance moved-from.
    /// Only callable on an rvalue, so the empty state cannot be observed through a name.
    [[nodiscard]] std::unique_ptr<T> to_unique_ptr() && noexcept { return std::move(ptr_); }

    void swap(UniqueRef &other) noexcept { ptr_.swap(other.ptr_); }
    friend void swap(UniqueRef &lhs, UniqueRef &rhs) noexcept { lhs.swap(rhs); }

  private:
    std::unique_ptr<T> ptr_;
};

/// Constructs a `T` on the heap and returns a `UniqueRef<T>` owning it. The non-null
/// invariant holds by construction: allocation failure throws (it never yields a null ref).
template <typename T, typename... Args> [[nodiscard]] UniqueRef<T> make_unique_ref(Args &&...args) {
    return UniqueRef<T>(std::make_unique<T>(std::forward<Args>(args)...));
}

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_UNIQUE_REF_HPP
