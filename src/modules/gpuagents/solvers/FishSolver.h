#pragma once
#include "common/Export.h"

#include "gpuagents/IAgentSolver.h"

namespace eve::gpuagents {

/** @brief 3D Boids fish school solver (CPU reference). */
class EVENGINE_API_DOMAINS FishSolver final : public IAgentSolver {
public:
    EffectKind kind() const override { return EffectKind::Fish; }
    void step(std::span<const AgentState> read, std::span<AgentState> write, const EffectProfile& profile,
              const EnvironmentSnapshot& env, float dt) override;
};

}  // namespace eve::gpuagents
