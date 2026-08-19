#pragma once

#include "common/Export.h"

#include <string>

namespace eve {

/** @brief Particle system query surface (provided by the particles module). */
class EVENGINE_API IParticlesQuery {
public:
    static constexpr const char* capabilityName = "IParticlesQuery";

    virtual ~IParticlesQuery() = default;

    virtual int emitterCount() = 0;

    /** @brief Create + start + emit once; fills position/count. */
    virtual bool createEmitter(int bufferSize, float x, float y, const std::string& preset,
                               int count, float* outX, float* outY, int* outCount) = 0;
};

}  // namespace eve
