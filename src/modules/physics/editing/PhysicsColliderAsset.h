#pragma once

#include "editing/EditingResult.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::physics_editing {

/** @brief Validated CPU geometry resolved from an authored collider asset. */
struct PhysicsColliderAssetGeometry {
    std::string               kind;
    std::vector<float>        vertices;
    std::vector<std::int32_t> indices;
    std::vector<float>        heights;
    int                       countX        = 0;
    int                       countZ        = 0;
    float                     cellSizeX     = 1.0F;
    float                     cellSizeZ     = 1.0F;
    float                     minimumHeight = 0.0F;
    float                     maximumHeight = 0.0F;
    bool                      loop          = false;
};

/** @brief Storage-neutral resolver for immutable collider geometry artifacts. */
class IPhysicsColliderAssetResolver {
public:
    virtual ~IPhysicsColliderAssetResolver() = default;

    /**
     * @brief Resolve an immutable artifact by stable reference and expected kind.
     * @return Owning geometry or a structured failure; no storage-owned views escape.
     */
    [[nodiscard]] virtual editing::Result<PhysicsColliderAssetGeometry> resolve(
        const std::string& reference, const std::string& expectedKind) const = 0;
};

}  // namespace eve::physics_editing
