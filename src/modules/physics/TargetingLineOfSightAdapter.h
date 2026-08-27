#pragma once

/**
 * @file TargetingLineOfSightAdapter.h
 * @brief Physics-to-sensing line-of-sight capability adapter.
 */

#include "sensing/Targeting.h"

#include <memory>
#include <vector>

namespace eve::physics {

class World3D;

/**
 * @brief Read-only 3D physics implementation of sensing's LOS capability.
 *
 * World3D owns the solver and registers itself for the duration of its valid
 * lifetime. The adapter owns neither worlds nor bodies; registrations carry a
 * weak lifetime token, so stale worlds are skipped without dereferencing them.
 * At most one valid world may be active. A second different world is rejected
 * with Conflict; callers must explicitly choose which world supplies targeting
 * LOS. The adapter supports World3D locations only and never converts grid
 * coordinates. Calls are synchronous on the simulation thread.
 */
class TargetingLineOfSightAdapter final : public sensing::ILineOfSightQuery {
public:
    /**
     * @brief Registers a borrowed live World3D as the sole active LOS world.
     * @param world Non-null live World3D. Its lifetime is tracked weakly.
     * @return Applied, NoOp for the same world, or Conflict for another world.
     * @note This synchronous method must be called on the simulation thread.
     */
    [[nodiscard]] Result<void> addWorld(World3D* world);
    /**
     * @brief Removes a previously registered world; repeated removal is a NoOp.
     * @param world Non-null World3D previously passed to addWorld.
     * @return Applied, NoOp, or InvalidArgument for a null world.
     * @note This synchronous method must be called on the simulation thread and
     *       does not destroy the borrowed world.
     */
    [[nodiscard]] Result<void> removeWorld(World3D* world);

    /** @copydoc sensing::ILineOfSightQuery::query */
    [[nodiscard]] Result<sensing::LineOfSightResult> query(const sensing::TargetLocation& from,
                                                           const sensing::TargetLocation& to) const override;

private:
    struct RegisteredWorld {
        World3D* world = nullptr;
        std::weak_ptr<const void> lifetime;
    };

    std::vector<RegisteredWorld> worlds_;
};

}  // namespace eve::physics
