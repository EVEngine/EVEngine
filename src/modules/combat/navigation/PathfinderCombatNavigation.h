#pragma once
#include "common/Export.h"


/** @file PathfinderCombatNavigation.h @brief Map Pathfinder steering adapter for combat locomotion. */

#include "combat/CombatLocomotion.h"

#include <functional>
#include <memory>

namespace eve::map {
class Pathfinder;
}

namespace eve::combat::navigation {

/** @brief World/grid projection used by the map-backed combat navigation provider. */
struct PathfinderCombatNavigationConfig {
    CombatVector2 origin;
    double        cellSize = 1.0;

    /**
     * @brief Reject non-finite coordinates and non-positive cell size.
     * @return Success for a usable projection, otherwise InvalidArgument.
     */
    [[nodiscard]] Result<void> validate() const;
};

/**
 * @brief Synchronous combat steering provider backed by the canonical map Pathfinder.
 *
 * The provider borrows Pathfinder for its whole lifetime and retains no Path,
 * locomotion state, or goal between calls. Its owner must destroy combat
 * locomotion first (or clear the provider), then this adapter, then Pathfinder.
 * Calls are simulation-thread-affine and must not race Pathfinder mutation.
 */
class EVENGINE_API_DOMAINS PathfinderCombatNavigationProvider final : public ICombatNavigationProvider {
public:
    /**
     * @brief Validate configuration and create an owning adapter.
     * @param pathfinder Borrowed Pathfinder that must outlive the returned provider.
     * @param config Owning world/grid projection copied into the provider.
     * @return Owning provider or InvalidArgument for invalid projection data.
     */
    [[nodiscard]] static Result<std::unique_ptr<PathfinderCombatNavigationProvider>> create(
        map::Pathfinder& pathfinder, PathfinderCombatNavigationConfig config);

    /** @copydoc ICombatNavigationProvider::steer */
    [[nodiscard]] Result<CombatNavigationSteering> steer(const CombatLocomotionState& state,
                                                          const CombatNavigationGoal& goal,
                                                          SimulationTick tick) override;

private:
    PathfinderCombatNavigationProvider(map::Pathfinder& pathfinder,
                                       PathfinderCombatNavigationConfig config) noexcept;

    std::reference_wrapper<map::Pathfinder> pathfinder_;
    PathfinderCombatNavigationConfig        config_;
};

}  // namespace eve::combat::navigation
