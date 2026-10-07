#pragma once
#include "common/Export.h"

#include "gpuagents/AgentState.h"

#include <glm/glm.hpp>

#include <vector>

namespace eve::gpuagents {

/**
 * @brief Packs standardized AgentState into instance transforms for draw submission.
 *
 * P0 exposes CPU-side matrices + custom attributes; P1 uploads to GPU instance buffers.
 */
class EVENGINE_API_DOMAINS AgentInstanceRenderer {
public:
    /**
     * @brief Rebuild instance buffers from simulation states.
     * @param states Current agent states.
     * @return Number of packed live instances.
     */
    int sync(const std::vector<AgentState>& states);

    /** @brief Live instance count after last sync. */
    int instanceCount() const { return instanceCount_; }

    /** @brief Column-major 4x4 model matrices for each live agent. */
    const std::vector<glm::mat4>& matrices() const { return matrices_; }

    /** @brief Per-instance custom float4 (from AgentState::customData). */
    const std::vector<glm::vec4>& customAttrs() const { return customAttrs_; }

    /** @brief Per-instance ages. */
    const std::vector<float>& ages() const { return ages_; }

private:
    int                      instanceCount_ = 0;
    std::vector<glm::mat4>   matrices_;
    std::vector<glm::vec4>   customAttrs_;
    std::vector<float>       ages_;
};

}  // namespace eve::gpuagents
