#pragma once

#include "common/Export.h"
#include "rpg/SkillTypes.h"
#include "rpg/StatusTypes.h"
#include "rpg/VitalsSystem.h"

#include <vector>

namespace eve::rpg::detail {
/** @brief One simulation's owning event queues; actors are borrowed until that simulation ends. */
struct SimulationEvents {
    std::vector<SkillCastEvent>    casts;
    std::vector<StatusTickEvent>   ticks;
    std::vector<StatusChangeEvent> changes;
    std::vector<VitalsEvent>       vitals;
};
/** @brief Borrow the current thread's event owner for the immediate synchronous operation. */
EVENGINE_API_PLATFORM SimulationEvents& simulationEvents();
/**
 * @brief Redirect RPG events into a caller-owned simulation for a nested synchronous scope.
 * @ownership Borrows next until scope destruction; scopes must unwind in stack order.
 * @thread RPG simulation thread only; no callbacks or locks in construction/destruction.
 * @remarks Pair with ecs::ScopedTable to isolate both actors and events. The default queues are untouched.
 */
class EVENGINE_API_PLATFORM SimulationEventScope {
public:
    /** @brief Activate next; its owner must outlive this scope. */
    explicit SimulationEventScope(SimulationEvents& next);
    /** @brief Restore the previous event owner before the scoped actors are destroyed. */
    ~SimulationEventScope();
    SimulationEventScope(const SimulationEventScope&)            = delete;
    SimulationEventScope& operator=(const SimulationEventScope&) = delete;

private:
    SimulationEvents& previous_;
};
}  // namespace eve::rpg::detail
