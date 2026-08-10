#pragma once

#include "gpgpu/ComputeShader.h"
#include "vkbuilder.hpp"

#include <array>

namespace eve::gpgpu {

class VulkanComputeShader final : public ComputeShader {
public:
    ~VulkanComputeShader() override;

    void bindBuffer(int binding, GpuBuffer *buffer) override;
    GpuBuffer *getBoundBuffer(int binding) const override;
    void setFloat(int index, float value) override;
    float getFloat(int index) const override;
    void clearBindings() override;

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
