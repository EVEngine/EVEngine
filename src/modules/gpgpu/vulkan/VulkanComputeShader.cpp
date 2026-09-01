#include "gpgpu/vulkan/VulkanComputeShader.h"
#include "gpgpu/vulkan/VulkanGpuBuffer.h"

#include "common/Exception.h"

#include <array>

namespace eve::gpgpu {

VulkanComputeShader::~VulkanComputeShader() {
    if (!device_ || !static_cast<VkDevice>(device_->instance)) return;
    auto &dev = *device_;
    if (pipeline_) {
        dev->destroyPipeline(pipeline_, device_->allocation_callbacks);
        pipeline_ = vk::Pipeline{};
    }
    if (pipelineLayout_) {
        dev->destroyPipelineLayout(pipelineLayout_, device_->allocation_callbacks);
        pipelineLayout_ = vk::PipelineLayout{};
    }
    if (setLayout_) {
        dev->destroyDescriptorSetLayout(setLayout_, device_->allocation_callbacks);
        setLayout_ = vk::DescriptorSetLayout{};
    }
    if (descriptorPool_) {
        // Sets allocated from the pool are freed with it.
        dev->destroyDescriptorPool(descriptorPool_, device_->allocation_callbacks);
        descriptorPool_ = vk::DescriptorPool{};
        descriptorSet_ = vk::DescriptorSet{};
    }
    if (module_) {
        dev->destroyShaderModule(module_, device_->allocation_callbacks);
        module_ = vk::ShaderModule{};
    }
    if (dummyBuffer_) {
        dev->destroyBuffer(dummyBuffer_, device_->allocation_callbacks);
        dummyBuffer_ = vk::Buffer{};
    }
    if (dummyMemory_) {
        dev->freeMemory(dummyMemory_, device_->allocation_callbacks);
        dummyMemory_ = vk::DeviceMemory{};
    }
    device_ = nullptr;
}

void VulkanComputeShader::bindBuffer(int binding, GpuBuffer *buffer) {
    if (binding < 0 || binding >= kMaxBindings) return;
    bindings_[size_t(binding)] = buffer;
    descriptorsDirty_ = true;
}

GpuBuffer *VulkanComputeShader::getBoundBuffer(int binding) const {
    if (binding < 0 || binding >= kMaxBindings) return nullptr;
    return bindings_[size_t(binding)];
}

void VulkanComputeShader::setFloat(int index, float value) {
    if (index < 0 || index >= kMaxFloats) return;
    push_[size_t(index)] = value;
}

float VulkanComputeShader::getFloat(int index) const {
    if (index < 0 || index >= kMaxFloats) return 0.f;
    return push_[size_t(index)];
}

void VulkanComputeShader::clearBindings() {
    bindings_.fill(nullptr);
    descriptorsDirty_ = true;
}

void VulkanComputeShader::flushDescriptors(vkb::Device &device) {
    if (!descriptorsDirty_ && descriptorSet_) return;

    if (!descriptorPool_) {
        // maxSets sets x kMaxBindings storage-buffer descriptors per set: the
        // pool must hold the deferred backlog from multi-dispatch Sequences.
        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eStorageBuffer,
                                        uint32_t(kMaxBindings) * 64};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 64;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        descriptorPool_ = device->createDescriptorPool(poolInfo, device.allocation_callbacks);
    }

    if (descriptorSet_) {
        if (activeSequenceCount_ > 0) {
            // A previously recorded command buffer in the open Sequence may
            // still reference this set. Defer the free until that Sequence
            // submits and waits; the pool is sized for the deferred backlog.
            pendingSets_.push_back(descriptorSet_);
        } else {
            device->freeDescriptorSets(descriptorPool_, descriptorSet_);
        }
        descriptorSet_ = vk::DescriptorSet{};
    }

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &setLayout_;
    auto sets = device->allocateDescriptorSets(alloc);
    descriptorSet_ = sets[0];

    // Dummy 4-byte SSBO for unused bindings (validation requires all layout slots valid).
    if (!dummyBuffer_) {
        using buf = vk::BufferUsageFlagBits;
        using pfb = vk::MemoryPropertyFlagBits;
        vkb::GenericBuffer tmp(device, buf::eStorageBuffer | buf::eTransferDst, 4, pfb::eDeviceLocal);
        dummyBuffer_ = tmp.buffer;
        dummyMemory_ = tmp.memory;
        // Without detach(), tmp's destructor frees the handles, leaving the
        // dummy SSBO dangling: descriptor writes reference a destroyed buffer
        // (crashes MoltenVK) and ~VulkanComputeShader double-frees it.
        tmp.detach();
    }

    std::array<vk::DescriptorBufferInfo, kMaxBindings> infos{};
    std::array<vk::WriteDescriptorSet, kMaxBindings> writes{};
    for (int i = 0; i < kMaxBindings; ++i) {
        auto *vb = dynamic_cast<VulkanGpuBuffer *>(bindings_[size_t(i)]);
        infos[size_t(i)].buffer = (vb && vb->buffer_) ? vb->buffer_ : dummyBuffer_;
        infos[size_t(i)].offset = 0;
        infos[size_t(i)].range = (vb && vb->buffer_) ? vb->size_ : vk::DeviceSize(4);
        writes[size_t(i)].dstSet = descriptorSet_;
        writes[size_t(i)].dstBinding = uint32_t(i);
        writes[size_t(i)].dstArrayElement = 0;
        writes[size_t(i)].descriptorCount = 1;
        writes[size_t(i)].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[size_t(i)].pBufferInfo = &infos[size_t(i)];
    }
    device->updateDescriptorSets(kMaxBindings, writes.data(), 0, nullptr);

    descriptorsDirty_ = false;
}

void VulkanComputeShader::beginSequence() { ++activeSequenceCount_; }

void VulkanComputeShader::endSequence() {
    if (activeSequenceCount_ > 0) --activeSequenceCount_;
}

void VulkanComputeShader::releasePendingDescriptors(vkb::Device &device) {
    if (activeSequenceCount_ > 0) return;
    if (pendingSets_.empty()) return;
    for (vk::DescriptorSet set : pendingSets_)
        device->freeDescriptorSets(descriptorPool_, set);
    pendingSets_.clear();
}

}  // namespace eve::gpgpu
