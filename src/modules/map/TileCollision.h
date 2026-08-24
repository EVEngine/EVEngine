#pragma once

#include "common/Export.h"

#include <cstddef>

namespace eve::map {

/** @brief One greedily merged solid tile rectangle in world coordinates. */
struct EVENGINE_API TileCollisionRect {
    float x      = 0.f;
    float y      = 0.f;
    float width  = 0.f;
    float height = 0.f;
};

/** @brief Optional sink implemented by a physics or project adapter. */
class EVENGINE_API ITileCollisionSink {
public:
    static constexpr const char* capabilityName = "ITileCollisionSink";
    virtual ~ITileCollisionSink()               = default;

    /** @brief Atomically replace collision geometry for a stable layer identity. */
    virtual void replaceTileCollision(const void* layer, const TileCollisionRect* rects, size_t count) = 0;
};

}  // namespace eve::map
