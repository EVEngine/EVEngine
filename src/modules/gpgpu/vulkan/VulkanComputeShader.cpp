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
    for (vk::DescriptorPool pool : retiredPools_) dev->destroyDescriptorPool(pool, device_->allocation_callbacks);
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
#if defined(VKB_ENABLE_VMA)
        if (dummyVmaAllocation_) {
            vmaDestroyBuffer(device_->vma_allocator, static_cast<VkBuffer>(dummyBuffer_),
                             dummyVmaAllocation_);
            dummyVmaAllocation_ = VK_NULL_HANDLE;
            dummyBuffer_ = vk::Buffer{};
            dummyMemory_ = vk::DeviceMemory{};
        } else
#endif
        {
            dev->destroyBuffer(dummyBuffer_, device_->allocation_callbacks);
            dummyBuffer_ = vk::Buffer{};
        }
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

    if (descriptorSet_) {
        if (activeSequenceCount_ > 0) {
            // Keep each immutable binding snapshot alive until all sequences
            // that may reference it have retired.
            pendingSets_.push_back({descriptorPool_, descriptorSet_});
        } else {
            device->freeDescriptorSets(descriptorPool_, descriptorSet_);
            --allocatedSets_;
        }
        descriptorSet_ = vk::DescriptorSet{};
    }

    constexpr uint32_t setsPerPool = 64;
    if (!descriptorPool_ || allocatedSets_ == setsPerPool) {
        // Long recording batches can retain more than one pool of snapshots.
        // Grow before exhausting maxSets; some drivers report fragmentation
        // while others allow allocations beyond the declared pool capacity.
        vk::DescriptorPoolSize       poolSize{vk::DescriptorType::eStorageBuffer, uint32_t(kMaxBindings) * setsPerPool};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets       = setsPerPool;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;
        const auto nextPool    = device->createDescriptorPool(poolInfo, device.allocation_callbacks);
        try {
            if (descriptorPool_) retiredPools_.push_back(descriptorPool_);
        } catch (...) {
            device->destroyDescriptorPool(nextPool, device.allocation_callbacks);
            throw;
        }
        descriptorPool_ = nextPool;
        allocatedSets_  = 0;
    }

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &setLayout_;
    auto sets = device->allocateDescriptorSets(alloc);
    descriptorSet_ = sets[0];
    ++allocatedSets_;

    // Dummy 4-byte SSBO for unused bindings (validation requires all layout slots valid).
    if (!dummyBuffer_) {
        using buf = vk::BufferUsageFlagBits;
        using pfb = vk::MemoryPropertyFlagBits;
        vkb::GenericBuffer tmp(device, buf::eStorageBuffer | buf::eTransferDst, 4, pfb::eDeviceLocal);
        dummyBuffer_ = tmp.buffer;
        dummyMemory_ = tmp.memory;
#if defined(VKB_ENABLE_VMA)
        dummyVmaAllocation_ = tmp.vma_allocation;
#endif
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
    for (const auto &lease : pendingSets_) {
        if (lease.pool != descriptorPool_) continue;
        device->freeDescriptorSets(lease.pool, lease.set);
        --allocatedSets_;
    }
    pendingSets_.clear();
    // Every set in a retired pool is now idle. Destroying the whole pool also
    // releases its descriptors without retaining a high-water allocation.
    for (vk::DescriptorPool pool : retiredPools_) device->destroyDescriptorPool(pool, device.allocation_callbacks);
    retiredPools_.clear();
}

}  // namespace eve::gpgpu
