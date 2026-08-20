#pragma once

#include "common/Export.h"

namespace eve {

/** @brief Audio control surface (provided by the audio module). */
class EVENGINE_API IAudioQuery {
public:
    static constexpr const char* capabilityName = "IAudioQuery";

    virtual ~IAudioQuery() = default;

    virtual float volume() const = 0;
    virtual void setVolume(float v) = 0;
    virtual void stopAll() = 0;
};

}  // namespace eve
