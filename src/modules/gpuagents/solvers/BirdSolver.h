#pragma once
#include "common/Export.h"

#include "gpuagents/IAgentSolver.h"

namespace eve::gpuagents {

/** @brief Bird flock solver with lift / drag / stall / bank (CPU reference). */
class EVENGINE_API_DOMAINS BirdSolver final : public IAgentSolver {
public:
    /** @brief Kind. */
    EffectKind kind() const override { return EffectKind::Bird; }
    /** @brief Step. */
    void step(std::span<const AgentState> read, std::span<AgentState> write, const EffectProfile& profile,
              const EnvironmentSnapshot& env, float dt) override;
};

}  // namespace eve::gpuagents
