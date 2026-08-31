#include "gpgpu/vulkan/VulkanGpuBuffer.h"
#include "gpgpu/vulkan/VulkanUtil.h"

#include "graphics/vulkan/Graphics.h"

#include "common/Exception.h"
#include "data/ByteData.h"

#include <cstring>
#include <vector>

namespace eve::gpgpu {

GpuResidentBufferView VulkanGpuBuffer::residentView() const {
    GpuResidentBufferView view;
    if (!buffer_) return view;
    const VkBuffer raw = static_cast<VkBuffer>(buffer_);
    static_assert(sizeof(raw) <= sizeof(view.nativeHandle));
    std::memcpy(&view.nativeHandle, &raw, sizeof(raw));
    view.backend   = GpuResidentBackend::Vulkan;
    view.sizeBytes = uint64_t(size_);
    return view;
}

VulkanGpuBuffer::~VulkanGpuBuffer() {
    if (!device_ || !static_cast<VkDevice>(device_->instance)) return;
    if (buffer_) {
        (*device_)->destroyBuffer(buffer_, device_->allocation_callbacks);
        buffer_ = vk::Buffer{};
    }
    if (memory_) {
        (*device_)->freeMemory(memory_, device_->allocation_callbacks);
        memory_ = vk::DeviceMemory{};
    }
    device_ = nullptr;
}

void VulkanGpuBuffer::uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset) {
    if (!device_ || !buffer_ || !src || nbytes == 0) return;
    if (dstOffset + nbytes > size_)
        throw Exception("GpuBuffer.write: out of range (offset=%llu size=%llu capacity=%llu)",
                        (unsigned long long)dstOffset, (unsigned long long)nbytes,
                        (unsigned long long)size_);

    auto *vkg = requireVulkanGraphics();
    auto queue = computeQueue(vkg);
    auto pool = computeCommandPool(vkg);

    if (hostVisible_) {
        void *ptr = (*device_)->mapMemory(memory_, dstOffset, nbytes, vk::MemoryMapFlags{});
        std::memcpy(ptr, src, size_t(nbytes));
        (*device_)->unmapMemory(memory_);
        return;
    }

    using buf = vk::BufferUsageFlagBits;
    using pfb = vk::MemoryPropertyFlagBits;
    vkb::GenericBuffer staging(*device_, buf::eTransferSrc, nbytes,
                               pfb::eHostVisible | pfb::eHostCoherent);
    staging.updateLocal(vkb::FrameSlot::gpuIdle(), src, nbytes);
    vkb::executeImmediately(device_->instance, pool, queue, [&](vk::CommandBuffer cb) {
        vk::BufferCopy bc{0, vk::DeviceSize(dstOffset), vk::DeviceSize(nbytes)};
        cb.copyBuffer(staging.buffer, buffer_, bc);
    });
}

void VulkanGpuBuffer::downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset) const {
    if (!device_ || !buffer_ || !dst || nbytes == 0) return;
    if (srcOffset + nbytes > size_)
        throw Exception("GpuBuffer.read: out of range (offset=%llu size=%llu capacity=%llu)",
                        (unsigned long long)srcOffset, (unsigned long long)nbytes,
                        (unsigned long long)size_);

    auto *vkg = requireVulkanGraphics();
    auto queue = computeQueue(vkg);
    auto pool = computeCommandPool(vkg);

    if (hostVisible_) {
        void *ptr = (*device_)->mapMemory(memory_, srcOffset, nbytes, vk::MemoryMapFlags{});
        std::memcpy(dst, ptr, size_t(nbytes));
        (*device_)->unmapMemory(memory_);
        return;
    }

    using buf = vk::BufferUsageFlagBits;
    using pfb = vk::MemoryPropertyFlagBits;
    vkb::GenericBuffer staging(*device_, buf::eTransferDst, nbytes,
                               pfb::eHostVisible | pfb::eHostCoherent);
    vkb::executeImmediately(device_->instance, pool, queue, [&](vk::CommandBuffer cb) {
        vk::BufferCopy bc{vk::DeviceSize(srcOffset), 0, vk::DeviceSize(nbytes)};
        cb.copyBuffer(buffer_, staging.buffer, bc);
    });
    void *ptr = (*device_)->mapMemory(staging.memory, 0, nbytes, vk::MemoryMapFlags{});
    std::memcpy(dst, ptr, size_t(nbytes));
    (*device_)->unmapMemory(staging.memory);
}

void VulkanGpuBuffer::writeData(data::ByteData *data, int dstOffset) {
    if (!data) return;
    uploadBytes(data->getData(), data->getSize(), uint64_t(dstOffset < 0 ? 0 : dstOffset));
}

data::ByteData *VulkanGpuBuffer::readData(int srcOffset, int size) {
    const int off = srcOffset < 0 ? 0 : srcOffset;
    int nbytes = size;
    if (nbytes < 0) nbytes = int(size_) - off;
    if (nbytes <= 0) return new data::ByteData(size_t(0));
    auto *out = new data::ByteData(size_t(nbytes));
    downloadBytes(out->getData(), uint64_t(nbytes), uint64_t(off));
    return out;
}

void VulkanGpuBuffer::writeFloat32(int floatIndex, float value) {
    if (floatIndex < 0) return;
    uploadBytes(&value, sizeof(float), uint64_t(floatIndex) * sizeof(float));
}

float VulkanGpuBuffer::readFloat32(int floatIndex) {
    if (floatIndex < 0) return 0.f;
    float v = 0.f;
    downloadBytes(&v, sizeof(float), uint64_t(floatIndex) * sizeof(float));
    return v;
}

void VulkanGpuBuffer::writeFloat32s(const float *data, int count, int startIndex) {
    if (!data || count <= 0 || startIndex < 0) return;
    uploadBytes(data, uint64_t(count) * sizeof(float),
                uint64_t(startIndex) * sizeof(float));
}

void VulkanGpuBuffer::readFloat32s(float *out, int count, int startIndex) const {
    if (!out || count <= 0 || startIndex < 0) return;
    downloadBytes(out, uint64_t(count) * sizeof(float),
                  uint64_t(startIndex) * sizeof(float));
}

void VulkanGpuBuffer::fillFloat32(float value) {
    if (size_ < sizeof(float)) return;
    const size_t count = size_t(size_ / sizeof(float));
    std::vector<float> tmp(count, value);
    uploadBytes(tmp.data(), count * sizeof(float), 0);
}

}  // namespace eve::gpgpu
