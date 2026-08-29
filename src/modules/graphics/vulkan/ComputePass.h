#pragma once

#include "vkbuilder.hpp"

#include <cstdint>
#include <vector>

namespace eve::graphics::vulkan {

/**
 * @brief Minimal compute pipeline wrapper for in-frame compute sections.
 *
 * Stage 2 (GPU cull / HZB) records compute dispatches into the main frame
 * command buffer between render passes; the gpgpu module stays CPU-side
 * experimental and is not used here.
 */
class ComputePass {
public:
    ComputePass() = default;
    ComputePass(const ComputePass &) = delete;
    ComputePass &operator=(const ComputePass &) = delete;
    ComputePass(ComputePass &&o) noexcept : device_(o.device_), pipeline_(o.pipeline_) {
        o.device_ = nullptr;
        o.pipeline_ = nullptr;
    }
    ComputePass &operator=(ComputePass &&o) noexcept;
    ~ComputePass();

    /** @brief Create from embedded SPIR-V words; layout must outlive the pass. */
    bool create(vkb::Device &device, vk::PipelineLayout layout,
                const std::vector<uint32_t> &spv);

    /** @brief Record one dispatch (local size baked into the shader). */
    void record(vk::CommandBuffer cb, uint32_t groupsX, uint32_t groupsY = 1,
                uint32_t groupsZ = 1) const;

    vk::Pipeline pipeline() const { return pipeline_; }

private:
    vkb::Device *device_ = nullptr;
    vk::Pipeline pipeline_ = nullptr;
};

}  // namespace eve::graphics::vulkan

