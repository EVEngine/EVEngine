#pragma once

#include "common/Export.h"

namespace eve {

/** @brief Audio control surface (provided by the audio module). */
class EVENGINE_API_FOUNDATION_INLINE IAudioQuery {
public:
    static constexpr const char* capabilityName = "IAudioQuery";

    /** @brief I audio query. */
    virtual ~IAudioQuery() = default;

    /** @brief Volume. */
    virtual float volume() const = 0;
    /** @brief Sets the volume. */
    virtual void setVolume(float v) = 0;
    /** @brief Stops all. */
    virtual void stopAll() = 0;
};

}  // namespace eve
