#include "graphics/vulkan/ComputePass.h"

namespace eve::graphics::vulkan {

ComputePass &ComputePass::operator=(ComputePass &&o) noexcept {
    if (this != &o) {
        if (device_ && pipeline_) device_->instance.destroyPipeline(pipeline_);
        device_ = o.device_;
        pipeline_ = o.pipeline_;
        o.device_ = nullptr;
        o.pipeline_ = nullptr;
    }
    return *this;
}

ComputePass::~ComputePass() {
    if (device_ && pipeline_) device_->instance.destroyPipeline(pipeline_);
}

bool ComputePass::create(vkb::Device &device, vk::PipelineLayout layout,
                         const std::vector<uint32_t> &spv) {
    if (spv.empty() || !layout) return false;
    if (pipeline_) {
        device.instance.destroyPipeline(pipeline_);
        pipeline_ = nullptr;
    }

    vk::ShaderModuleCreateInfo moduleInfo{};
    moduleInfo.codeSize = spv.size() * sizeof(uint32_t);
    moduleInfo.pCode = spv.data();
    vk::ShaderModule module = device.instance.createShaderModule(moduleInfo);

    // The vendored vulkan.hpp strips the vk::Device::createComputePipeline
    // wrapper; call the core C entry point directly (same ABI).
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage = VkPipelineShaderStageCreateInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
        VK_SHADER_STAGE_COMPUTE_BIT, static_cast<VkShaderModule>(module), "main", nullptr};
    info.layout = static_cast<VkPipelineLayout>(layout);
    VkPipeline created = VK_NULL_HANDLE;
    const VkResult r =
        vkCreateComputePipelines(static_cast<VkDevice>(device.instance), VK_NULL_HANDLE, 1, &info,
                                 nullptr, &created);
    device.instance.destroyShaderModule(module);
    if (r != VK_SUCCESS || !created) return false;

    device_ = &device;
    pipeline_ = created;
    return true;
}

void ComputePass::record(vk::CommandBuffer cb, uint32_t groupsX, uint32_t groupsY,
                         uint32_t groupsZ) const {
    if (!pipeline_) return;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_);
    cb.dispatch(groupsX, groupsY, groupsZ);
}

}  // namespace eve::graphics::vulkan
