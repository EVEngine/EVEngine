#pragma once

/**
 * @file RuntimeBorrow.h
 * @brief Handle-ref and borrowed observation types without the slot-map registry.
 *
 * High-fanout facades that only expose `RuntimeHandleRef` / `Borrowed<T>` should
 * include this header instead of `RuntimeRegistry.h`, which also pulls the
 * owning slot store and its Result/optional/atomic dependencies.
 */

#include "common/RuntimeHandle.h"

#include <cstdint>

namespace eve::script {

/**
 * @brief Non-owning observation of a registry-owned object.
 *
 * @tparam T Observed object type; may be incomplete at the declaration site.
 * @ownership Never owns; the producing registry remains responsible for destruction.
 * @lifetime Valid only while the owner epoch remains current and no owner
 *           mutation, restore, clear or destruction has occurred.
 */
template <class T>
class Borrowed {
public:
    /** @brief Constructs an unbound Borrowed. */
    constexpr Borrowed() noexcept = default;
    /** @brief Constructs a Borrowed from a nullable pointer. */
    explicit constexpr Borrowed(T* value, std::uint64_t ownerEpoch = 0) noexcept
        : value_(value), ownerEpoch_(ownerEpoch) {}
    /** @brief Constructs a Borrowed from a live reference. */
    explicit constexpr Borrowed(T& value, std::uint64_t ownerEpoch = 0) noexcept
        : value_(&value), ownerEpoch_(ownerEpoch) {}

    /**
     * @brief Returns the observed pointer, or `nullptr` when unbound.
     * @return A nullable borrowed pointer; this wrapper never transfers ownership.
     * @ownership Borrowed from the owner that created this observation.
     * @nullable Yes.
     * @lifetime Valid only while the owner epoch remains current and no owner
     *           mutation, restore, clear or destruction has occurred.
     * @thread Use on the owner thread; the wrapper provides no synchronization.
     * @reentrancy Does not extend lifetime or invoke callbacks.
     */
    [[nodiscard]] constexpr T* get() const noexcept { return value_; }
    /** @brief Returns whether the observation currently names an object. */
    [[nodiscard]] constexpr bool isBound() const noexcept { return value_ != nullptr; }
    /** @brief Returns the epoch of the owner that produced this observation. */
    [[nodiscard]] constexpr std::uint64_t ownerEpoch() const noexcept { return ownerEpoch_; }
    /** @brief Pointer-like access to the borrowed object. */
    constexpr T* operator->() const noexcept { return value_; }
    /** @brief Dereferences the borrowed object; the caller owns validity proof. */
    constexpr T& operator*() const noexcept { return *value_; }

private:
    T*            value_      = nullptr;
    std::uint64_t ownerEpoch_ = 0;
};

/**
 * @brief Handle plus owner-lifetime epoch for a non-ECS registry object.
 *
 * The embedded `RuntimeHandle` supplies slot/generation stale detection. The
 * epoch additionally prevents a handle from an unloaded module instance from
 * accidentally resolving against a newly-created module instance whose slot
 * numbers happen to restart at the same values.
 */
template <class Tag>
struct RuntimeHandleRef {
    RuntimeHandle<Tag> handle     = RuntimeHandle<Tag>::invalid();
    std::uint64_t      ownerEpoch = 0;

    /** @brief Returns whether both the handle and its registry epoch are set. */
    [[nodiscard]] constexpr bool isValid() const noexcept { return handle.isValid() && ownerEpoch != 0; }
    /** @brief Returns the packed slot/generation projection. */
    [[nodiscard]] constexpr std::uint64_t packed() const noexcept { return handle.packed(); }
    friend constexpr bool operator==(const RuntimeHandleRef&, const RuntimeHandleRef&) noexcept = default;
};

}  // namespace eve::script
