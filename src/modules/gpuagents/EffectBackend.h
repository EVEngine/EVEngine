#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "gpuagents/AgentInstanceRenderer.h"
#include "gpuagents/EffectProfile.h"
#include "gpuagents/GpuAgentSimulation.h"

#include <memory>
#include <string>

namespace eve::gpuagents {

/**
 * @brief Binds an EffectProfile to a GpuAgentSimulation and optional renderer.
 *
 * Profile is data-only; Backend owns the live simulation and exposes step/reset.
 */
class EVENGINE_API_DOMAINS EffectBackend {
public:
    EffectBackend() = default;

    /**
     * @brief Configure from a profile and initialize empty capacity.
     * @param profile Effect parameters.
     * @param displayName Optional debug label.
     */
    [[nodiscard("check backend configure")]] Result<void> configure(const EffectProfile& profile,
                                                                    std::string displayName = {});

    /** @brief Owned simulation. */
    GpuAgentSimulation& simulation() { return simulation_; }
    /** @brief Const simulation. */
    const GpuAgentSimulation& simulation() const { return simulation_; }

    /** @brief Instance renderer packing transforms from current states. */
    AgentInstanceRenderer& renderer() { return renderer_; }
    /** @brief Const renderer. */
    const AgentInstanceRenderer& renderer() const { return renderer_; }

    /** @brief Debug / registry name. */
    const std::string& name() const { return name_; }

    /** @brief Active effect kind. */
    EffectKind kind() const { return simulation_.profile().kind; }

    /**
     * @brief Advance simulation then refresh instance buffers.
     * @param dt Frame delta.
     * @param env Environment snapshot.
     */
    [[nodiscard("check backend step")]] Result<void> step(float dt, const EnvironmentSnapshot& env);

    /** @brief Reset simulation agents. */
    void reset() { simulation_.reset(); }

private:
    std::string          name_;
    GpuAgentSimulation   simulation_;
    AgentInstanceRenderer renderer_;
};

}  // namespace eve::gpuagents
