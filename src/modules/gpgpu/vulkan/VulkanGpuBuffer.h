#pragma once

#include "gpgpu/GpuBuffer.h"
#include "vkbuilder.hpp"

#include <string>

namespace eve::gpgpu {

class VulkanGpuBuffer final : public GpuBuffer {
public:
    ~VulkanGpuBuffer() override;

    int getSize() const override { return int(size_); }
    std::string getUsage() const override { return usage_; }

    void writeData(data::ByteData *data, int dstOffset = 0) override;
    data::ByteData *readData(int srcOffset = 0, int size = -1) override;
    void writeFloat32(int floatIndex, float value) override;
    float readFloat32(int floatIndex) override;
    void fillFloat32(float value) override;
    void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0) override;
    void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0) override;

    vkb::Device *device_ = nullptr;
    vk::Buffer buffer_{};
    vk::DeviceMemory memory_{};
    vk::DeviceSize size_ = 0;
    std::string usage_ = "storage";
    bool hostVisible_ = false;
};

}  // namespace eve::gpgpu
