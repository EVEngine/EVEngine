#pragma once
#include "common/Export.h"

#include "gpuagents/IAgentSolver.h"

namespace eve::gpuagents {

/** @brief Life Network 2D surface-trail solver (CPU reference). */
class EVENGINE_API_DOMAINS LifeNetworkSolver final : public IAgentSolver {
public:
    EffectKind kind() const override { return EffectKind::LifeNetwork; }
    void step(std::span<const AgentState> read, std::span<AgentState> write, const EffectProfile& profile,
              const EnvironmentSnapshot& env, float dt) override;
};

}  // namespace eve::gpuagents
