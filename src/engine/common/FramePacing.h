#pragma once

#include "common/Result.h"

namespace eve {

/**
 * @brief Host-loop frame pacing controlled independently of presentation.
 *
 * The provider is owned by the host OS module and is available only while that
 * module is loaded. Calls are synchronous on the game thread and invoke no
 * callbacks or scripts.
 */
class IFramePacing {
public:
    static constexpr const char* capabilityName = "eve.frame-pacing";
    virtual ~IFramePacing() = default;

    /** @brief Record the presentation VSync interval; zero means unsynchronized. */
    [[nodiscard]] virtual Result<void> setVerticalSyncCount(int count) = 0;
    /** @brief Return the current presentation VSync interval. */
    [[nodiscard]] virtual int getVerticalSyncCount() const noexcept = 0;
    /** @brief Set the software frame cap; -1 disables it. */
    [[nodiscard]] virtual Result<void> setTargetFramesPerSecond(int target) = 0;
    /** @brief Return the requested software frame cap, or -1 when disabled. */
    [[nodiscard]] virtual int getTargetFramesPerSecond() const noexcept = 0;
    /** @brief Apply the configured software frame cap after presentation. */
    virtual void limitFrame() = 0;
};

}  // namespace eve
