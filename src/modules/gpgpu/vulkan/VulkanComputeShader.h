#pragma once

#include "gpgpu/ComputeShader.h"
#include "vkbuilder.hpp"

#include <array>

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
};

}  // namespace eve::gpgpu
