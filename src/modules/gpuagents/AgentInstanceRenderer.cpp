#include "gpuagents/AgentInstanceRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

namespace eve::gpuagents {

int AgentInstanceRenderer::sync(const std::vector<AgentState>& states) {
    matrices_.clear();
    customAttrs_.clear();
    ages_.clear();
    matrices_.reserve(states.size());
    customAttrs_.reserve(states.size());
    ages_.reserve(states.size());

    for (const auto& s : states) {
        if (s.alive == 0) continue;
        const glm::mat4 rot = glm::mat4_cast(s.rotation);
        const glm::mat4 tr  = glm::translate(glm::mat4(1.f), s.position);
        matrices_.push_back(tr * rot);
        customAttrs_.emplace_back(s.customData[0], s.customData[1], s.customData[2], s.customData[3]);
        ages_.push_back(s.age);
    }
    instanceCount_ = static_cast<int>(matrices_.size());
    return instanceCount_;
}

}  // namespace eve::gpuagents
