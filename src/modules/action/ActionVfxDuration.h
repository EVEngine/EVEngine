#pragma once

/** @file ActionVfxDuration.h @brief Optional VFX resource-duration query for action editors. */

#include "common/Result.h"
#include "common/Time.h"

#include <string_view>

namespace eve::action {

/**
 * @brief Optional synchronous provider of authored VFX resource duration.
 *
 * The provider returns an owning Duration and retains no view into the URI.
 * Calls are owner-thread-only and non-reentrant. The providing module must
 * revoke the capability before destroying its implementation.
 */
class IActionVfxDurationProvider {
public:
    static constexpr const char* capabilityName = "IActionVfxDurationProvider";
    virtual ~IActionVfxDurationProvider() = default;

    /**
     * @brief Resolve the natural finite playback duration of one VFX resource.
     * @param uri Borrowed resource URI valid only for this synchronous call.
     * @return Finite positive duration, or a structured missing, load, or unbounded diagnostic.
     */
    [[nodiscard]] virtual Result<Duration> naturalDuration(std::string_view uri) const = 0;
};

}  // namespace eve::action
