#pragma once

#include "gpgpu/ComputeShader.h"
#include "vkbuilder.hpp"

#include <array>
#include <vector>

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
    bool deferSetFree_ = false;
};

}  // namespace eve::gpgpu
