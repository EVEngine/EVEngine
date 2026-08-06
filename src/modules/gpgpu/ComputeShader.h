#pragma once

#include "vkbuilder.hpp"

#include <array>
#include <cstdint>

namespace eve::gpgpu {

class GpuBuffer;

/**
 * Vulkan compute pipeline + descriptor bindings.
 * Bind storage buffers then dispatch via Gpgpu::dispatch.
 * Push constants: float[32] (same size as graphics::Shader).
 */
class ComputeShader {
public:
    static constexpr int kMaxBindings = 8;
    static constexpr int kMaxFloats = 32;
    static constexpr uint32_t kPushConstantBytes = uint32_t(kMaxFloats * sizeof(float));

    ComputeShader() = default;
    ~ComputeShader();

    ComputeShader(const ComputeShader &) = delete;
    ComputeShader &operator=(const ComputeShader &) = delete;

    /** Bind a storage buffer to set=0 binding. binding in [0, kMaxBindings). */
    void bindBuffer(int binding, GpuBuffer *buffer);

    GpuBuffer *getBoundBuffer(int binding) const;

    void setFloat(int index, float value);
    float getFloat(int index) const;

    void clearBindings();

    /** Rebuild descriptor set from current bindings (called by dispatch). */
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
    std::array<float, kMaxFloats> push_{};
    bool descriptorsDirty_ = true;
};

}  // namespace eve::gpgpu
