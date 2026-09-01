#pragma once

#include "gpgpu/ComputeShader.h"
#include "vkbuilder.hpp"

#include <array>
#include <vector>

namespace eve::gpgpu {

/** @brief Vulkan 计算着色器（SPIR-V pipeline + descriptor 管理）。 */
class VulkanComputeShader final : public ComputeShader {
public:
    ~VulkanComputeShader() override;

    /** @brief 绑定/解绑 GpuBuffer 到指定 binding。 */
    void bindBuffer(int binding, GpuBuffer *buffer) override;
    GpuBuffer *getBoundBuffer(int binding) const override;
    /** @brief 标量 uniform 读写。 */
    void setFloat(int index, float value) override;
    float getFloat(int index) const override;
    /** @brief 清空全部绑定。 */
    void clearBindings() override;

    /** @brief 内部：把脏绑定刷成 descriptor set。 */
    void flushDescriptors(vkb::Device &device);

    /** While a Sequence is recording, superseded sets are deferred (see flushDescriptors). */
    void beginSequence();
    void endSequence();

    /**
     * Free descriptor sets that were superseded while recorded (but not yet
     * submitted) command buffers still referenced them. Safe to call only
     * after every submission that used this shader has completed.
     */
    void releasePendingDescriptors(vkb::Device &device);

    vkb::Device *device_ = nullptr;
    vk::ShaderModule module_{};
    vk::DescriptorSetLayout setLayout_{};
    vk::PipelineLayout pipelineLayout_{};
    vk::Pipeline pipeline_{};
    vk::DescriptorPool descriptorPool_{};
    vk::DescriptorSet descriptorSet_{};
    vk::Buffer dummyBuffer_{};
    vk::DeviceMemory dummyMemory_{};

    std::array<GpuBuffer *, kMaxBindings> bindings_{};
    bool descriptorsDirty_ = true;

    // Sets superseded by newer flushes; still referenced by pending command
    // buffers until the owning Sequence submits and waits.
    std::vector<vk::DescriptorSet> pendingSets_;
    uint32_t                       activeSequenceCount_ = 0;
};

}  // namespace eve::gpgpu
