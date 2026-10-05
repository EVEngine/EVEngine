#pragma once
#include "common/Export.h"

#include "gpuagents/AgentState.h"
#include "gpuagents/EffectProfile.h"
#include "gpuagents/EnvironmentSnapshot.h"

#include <span>

namespace eve::gpuagents {

/**
 * @brief Solver kernel contract: consume read buffer, write next state.
 *
 * Device/thread: simulation thread only. Must not retain pointers across steps.
 */
class EVENGINE_API_DOMAINS IAgentSolver {
public:
    virtual ~IAgentSolver() = default;

    /** @brief Effect kind this solver implements. */
    virtual EffectKind kind() const = 0;

    /**
     * @brief Advance one fixed substep.
     * @param read Current agent states (size == capacity).
     * @param write Destination agent states (same size; may alias only if solver documents it — P0 forbids).
     * @param profile Active effect profile.
     * @param env Environment snapshot for this tick.
     * @param dt Fixed substep seconds.
     */
    virtual void step(std::span<const AgentState> read, std::span<AgentState> write, const EffectProfile& profile,
                      const EnvironmentSnapshot& env, float dt) = 0;
};

}  // namespace eve::gpuagents
