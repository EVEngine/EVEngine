#pragma once
#include <memory>
namespace eve {
class LifetimeOwner;
/** @brief Owner-thread lifetime observation; cannot retain or access its owner. */
class LifetimeWitness {
    friend class LifetimeOwner;
    std::weak_ptr<const void> state_;

public:
    /** @brief Construct an invalid witness. */
    LifetimeWitness() = default;
    /** @brief Query original owner liveness; owner-thread only. */
    [[nodiscard]] bool isAlive() const noexcept { return !state_.expired(); }
};
/** @brief Sole owner of an opaque lifetime record; copying is forbidden. */
class LifetimeOwner {
    std::shared_ptr<const void> state_ = std::make_shared<const unsigned char>(0);

public:
    /** @brief Allocate a lifetime record at owner construction. */
    LifetimeOwner()                                = default;
    LifetimeOwner(const LifetimeOwner&)            = delete;
    LifetimeOwner& operator=(const LifetimeOwner&) = delete;
    /** @brief Invalidate witnesses without callbacks. */
    void reset() noexcept { state_.reset(); }
    /** @brief Copy a witness without granting shared ownership. */
    [[nodiscard]] LifetimeWitness witness() const noexcept {
        LifetimeWitness result;
        result.state_ = state_;
        return result;
    }
};
}  // namespace eve
