#include "gpgpu/vulkan/VulkanGpgpu.h"
#include "gpgpu/vulkan/VulkanComputeShader.h"
#include "gpgpu/vulkan/VulkanGpuBuffer.h"
#include "gpgpu/vulkan/VulkanUtil.h"

#include "common/Exception.h"
#include "graphics/vulkan/Graphics.h"

namespace eve::gpgpu {

bool vulkanGpgpuReady() { return vulkanGraphicsReady(); }

ComputeShader *vulkanNewShaderFromSpirv(const std::vector<uint32_t> &spv) {
    auto *vkg = requireVulkanGraphics();
    auto &device = vkg->getDevice();

    auto *shader = new VulkanComputeShader();
    shader->device_ = &device;
    shader->module_ = vkb::PipelineBuilder::createShaderModule(device.instance, spv);

    // Fixed layout: set 0, bindings 0..N-1 = storage buffers (compute stage).
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    bindings.reserve(ComputeShader::kMaxBindings);
    for (int i = 0; i < ComputeShader::kMaxBindings; ++i) {
        vk::DescriptorSetLayoutBinding b{};
        b.binding = uint32_t(i);
        b.descriptorType = vk::DescriptorType::eStorageBuffer;
        b.descriptorCount = 1;
        b.stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings.push_back(b);
    }
    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = uint32_t(bindings.size());
    layoutInfo.pBindings = bindings.data();
    shader->setLayout_ = device->createDescriptorSetLayout(layoutInfo, device.allocation_callbacks);

    vk::PushConstantRange pcr{};
    pcr.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pcr.offset = 0;
    pcr.size = ComputeShader::kPushConstantBytes;

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &shader->setLayout_;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcr;
    shader->pipelineLayout_ = device->createPipelineLayout(plInfo, device.allocation_callbacks);

    vk::PipelineShaderStageCreateInfo stage{};
    stage.stage = vk::ShaderStageFlagBits::eCompute;
    stage.module = shader->module_;
    stage.pName = "main";

    vk::ComputePipelineCreateInfo cpInfo{};
    cpInfo.stage = stage;
    cpInfo.layout = shader->pipelineLayout_;
    auto result = device->createComputePipeline(vk::PipelineCache{}, cpInfo, device.allocation_callbacks);
    if (result.result != vk::Result::eSuccess) {
        delete shader;
        throw Exception("Gpgpu.newShader: createComputePipeline failed");
    }
    shader->pipeline_ = result.value;
    return shader;
}

GpuBuffer *vulkanNewBuffer(int byteSize, const std::string &usage) {
    if (byteSize <= 0) throw Exception("Gpgpu.newBuffer: byteSize must be > 0");
    auto *vkg = requireVulkanGraphics();
    auto &device = vkg->getDevice();

    const bool staging = (usage == "staging");
    const bool storage = (usage == "storage" || usage.empty());
    if (!staging && !storage)
        throw Exception("Gpgpu.newBuffer: usage must be \"storage\" or \"staging\"");

    using buf = vk::BufferUsageFlagBits;
    using pfb = vk::MemoryPropertyFlagBits;

    vk::BufferUsageFlags flags = buf::eTransferSrc | buf::eTransferDst;
    if (storage || staging) flags |= buf::eStorageBuffer;

    vk::MemoryPropertyFlags mem =
        staging ? (pfb::eHostVisible | pfb::eHostCoherent) : pfb::eDeviceLocal;

    auto *b = new VulkanGpuBuffer();
    b->device_ = &device;
    b->size_ = vk::DeviceSize(byteSize);
    b->usage_ = staging ? "staging" : "storage";
    b->hostVisible_ = staging;

    vkb::GenericBuffer tmp(device, flags, b->size_, mem);
    b->buffer_ = tmp.buffer;
    b->memory_ = tmp.memory;
#if defined(VKB_ENABLE_VMA)
    b->vmaAllocation_ = tmp.vma_allocation;
#endif
    tmp.detach();
    return b;
}

void vulkanDispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ) {
    auto *vs = dynamic_cast<VulkanComputeShader *>(shader);
    if (!vs || !vs->pipeline_) return;
    if (groupsX <= 0) groupsX = 1;
    if (groupsY <= 0) groupsY = 1;
    if (groupsZ <= 0) groupsZ = 1;

    auto *vkg = requireVulkanGraphics();
    auto &device = vkg->getDevice();
    auto queue = computeQueue(vkg);
    auto pool = computeCommandPool(vkg);
    if (!queue) throw Exception("Gpgpu.dispatch: no compute/graphics queue");

    vs->flushDescriptors(device);

    vkb::executeImmediately(device.instance, pool, queue, [&](vk::CommandBuffer cb) {
        cb.bindPipeline(vk::PipelineBindPoint::eCompute, vs->pipeline_);
        if (vs->descriptorSet_) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, vs->pipelineLayout_, 0,
                                  vs->descriptorSet_, nullptr);
        }
        cb.pushConstants(vs->pipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0,
                         ComputeShader::kPushConstantBytes, vs->pushConstantData());
        cb.dispatch(uint32_t(groupsX), uint32_t(groupsY), uint32_t(groupsZ));
    });
}

}  // namespace eve::gpgpu
