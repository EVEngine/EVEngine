#pragma once

#include <cstddef>
#include <string>

namespace eve {

/**
 * @brief Read-only cross-module view of animation events emitted by the latest simulation update.
 * @ownership Event names are returned by value; consumers retain no source storage.
 * @thread Affine to the animation source's update thread.
 * @reentrancy Implementations must not invoke callbacks.
 */
class IAnimationEventSource {
public:
    virtual ~IAnimationEventSource() = default;
    /** @brief Number of events emitted by the latest update. */
    [[nodiscard]] virtual std::size_t animationEventCount() const noexcept = 0;
    /** @brief Event name at index, or empty text for an invalid index. */
    [[nodiscard]] virtual std::string animationEventName(std::size_t index) const = 0;
};

}  // namespace eve
