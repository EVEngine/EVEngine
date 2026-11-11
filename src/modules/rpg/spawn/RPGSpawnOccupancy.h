#pragma once
#include <memory>
#include "common/Export.h"
#include "spawn/SpawnSpace.h"
namespace ecs {
class Table;
}
namespace eve::rpg_spawn {
/**
 * @brief Occupancy projection read from actual RPG actor positions, without a second position registry.
 * @ownership Weak-observes the domain table; caller owns its lifetime. Stores only the explicit uniform body radius.
 * @thread ECS owner thread; synchronous read-only capture, no gameplay callbacks or reentry.
 * @remarks The first adapter supports a uniform configured footprint. Every actor must have a valid
 * SpawnedActorState position and rpg::ActorIdentity subject, including non-spawn actors; missing projection is an
 * error. Physical/Crowd backends may instead implement ISpawnOccupancy from their canonical heterogeneous shapes.
 */
class EVENGINE_API_PLATFORM RPGSpawnOccupancy final : public spawn::ISpawnOccupancy {
public:
    /** @brief Bind a domain table and nonnegative uniform footprint radius in world units; no actor pointers retained.
     */
    RPGSpawnOccupancy(std::shared_ptr<ecs::Table> world, double bodyRadius);
    /** @brief Return owning disks from live actor components or a structured missing/invalid projection error.
     * @cost Linear in domain actors, amortized per spatial plan/final publication check. */
    [[nodiscard]] Result<std::vector<spawn::OccupiedDisk>> capture() const override;

private:
    std::weak_ptr<ecs::Table> world_;
    double                    bodyRadius_;
};
}  // namespace eve::rpg_spawn
