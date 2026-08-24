#pragma once

#include "common/Export.h"

#include <cstdint>

namespace eve {

/** @brief Closest hit returned by the optional camera-obstruction provider. */
struct EVENGINE_API CameraObstructionHit {
    bool  hit      = false;
    int   bodyId   = -1;
    float fraction = 1.f;
    float x = 0.f, y = 0.f, z = 0.f;
    float normalX = 0.f, normalY = 0.f, normalZ = 0.f;
};

/** @brief Cross-module query used by camera without linking against physics. */
class EVENGINE_API ICameraObstructionQuery {
public:
    static constexpr const char* capabilityName = "eve.camera.ICameraObstructionQuery";
    virtual ~ICameraObstructionQuery() = default;

    virtual bool sphereCast(float fromX, float fromY, float fromZ, float toX, float toY,
                            float toZ, float radius, uint64_t maskBits, int ignoredBodyId,
                            CameraObstructionHit* out) = 0;
};

}  // namespace eve
