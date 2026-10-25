#pragma once
#include <cstdint>
#include "common/Export.h"

namespace eve::physics {
/** @brief Runtime lifecycle for one bone inside a geometry-collection instance. */
enum class BoneRuntimeState : std::uint8_t {
    Attached = 0,
    Detached = 1,
    Sleeping = 2,
};

/**
 * @brief Presentation snapshot for one bone (world pose + proxy extents).
 *
 * Owning value copied from a live instance or sampled from a baked clip.
 * Contains no runtime handles or borrowed pointers; safe after the source is destroyed.
 */
struct EVENGINE_API_BACKENDS BonePresentation {
    BoneRuntimeState state         = BoneRuntimeState::Attached;
    float            halfExtentX   = 0.5f;
    float            halfExtentY   = 0.5f;
    float            halfExtentZ   = 0.5f;
    float            worldX        = 0.f;
    float            worldY        = 0.f;
    float            worldZ        = 0.f;
    float            rotX          = 0.f;
    float            rotY          = 0.f;
    float            rotZ          = 0.f;
    float            rotW          = 1.f;
    int              clusterId     = 0;
    int              fractureLevel = 0;
};

}  // namespace eve::physics
