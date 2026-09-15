#pragma once

/** @file EntitySpatialResolver.h @brief Optional ECS-entity to world-pose capability. */

#include "common/ECS.h"
#include "common/Export.h"
#include "common/Result.h"

#include <string_view>

namespace eve {

/** @brief Renderer-neutral owning world pose resolved for an ECS entity. */
struct EntitySpatialPose {
    double positionX = 0.0;
    double positionY = 0.0;
    double positionZ = 0.0;
    double rotationXDegrees = 0.0;
    double rotationYDegrees = 0.0;
    double rotationZDegrees = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double scaleZ = 1.0;
};

/**
 * @brief One optional domain provider capable of resolving selected ECS entities.
 * @remarks Providers are owner-thread-only, invoke no callbacks and return NotFound
 * for entity types they do not own. Returned poses own all data.
 */
class EVENGINE_API IEntitySpatialProvider {
public:
    static constexpr const char* capabilityName = "eve.entity-spatial-provider";
    virtual ~IEntitySpatialProvider() = default;

    /**
     * @brief Resolve a current world pose, optionally at a named bone/socket.
     * @param entity Generation-qualified ECS identity resolved during this call.
     * @param bone Optional stable bone/socket name; empty requests the entity root.
     * @return Owning pose, NotFound for an unsupported entity, or a structured failure.
     */
    [[nodiscard]] virtual Result<EntitySpatialPose> resolve(ecs::EntityHandle entity,
                                                            std::string_view bone) const = 0;
};

/**
 * @brief Resolve through registered providers in deterministic registration order.
 * @return First successful pose; the first non-NotFound failure; or NotFound.
 * @thread Owner thread only; provider registration must be settled before dispatch.
 * @reentrancy Providers must not mutate the capability listener registry.
 */
[[nodiscard]] EVENGINE_API Result<EntitySpatialPose> resolveEntitySpatialPose(ecs::EntityHandle entity,
                                                                              std::string_view bone = {});

}  // namespace eve
