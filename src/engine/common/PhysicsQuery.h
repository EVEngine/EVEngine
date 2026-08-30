#pragma once

#include "common/Export.h"

#include <string>

namespace eve {

/** @brief Ray-cast result (value type). */
struct EVENGINE_API RayHitInfo {
    bool hit = false;
    int bodyId = 0;
    float x = 0.f, y = 0.f;
    float normalX = 0.f, normalY = 0.f;
    float fraction = 0.f;
};

/** @brief 2D physics world query surface (provided by the physics module). */
class EVENGINE_API IPhysicsQuery {
public:
    static constexpr const char* capabilityName = "IPhysicsQuery";

    virtual ~IPhysicsQuery() = default;

    /** @brief Create a world with the given gravity; returns a non-negative id. */
    virtual int newWorld(float gravityX, float gravityY) = 0;
    /** @brief Number of worlds still alive. */
    virtual int worldCount() = 0;
    /** @brief Gravity of a world; false when the id is unknown. */
    virtual bool worldGravity(int id, float* gravityX, float* gravityY) = 0;
    /** @brief Segment ray-cast; fills the hit info. */
    virtual bool rayCast(int id, float x1, float y1, float x2, float y2, RayHitInfo* out) = 0;
    /** @brief Destroy a world. */
    virtual bool removeWorld(int id) = 0;
};

}  // namespace eve
