#pragma once

#include "vkbuilder.hpp"

#include <cstdint>
#include <string>

namespace eve::data {
class ByteData;
}

namespace eve::gpgpu {

/**
 * GPU buffer for compute (storage) or CPU staging transfers.
 * Squirrel-owned; destroys Vulkan resources in destructor.
 */
class GpuBuffer {
public:
    GpuBuffer() = default;
    ~GpuBuffer();

    GpuBuffer(const GpuBuffer &) = delete;
    GpuBuffer &operator=(const GpuBuffer &) = delete;

    int getSize() const { return int(size_); }
    std::string getUsage() const { return usage_; }

    void writeData(data::ByteData *data, int dstOffset = 0);
    data::ByteData *readData(int srcOffset = 0, int size = -1);

    void writeFloat32(int floatIndex, float value);
    float readFloat32(int floatIndex);
    void fillFloat32(float value);

    void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0);
    void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0);

    vkb::Device *device_ = nullptr;
    vk::Buffer buffer_{};
    vk::DeviceMemory memory_{};
    vk::DeviceSize size_ = 0;
    std::string usage_ = "storage";
    bool hostVisible_ = false;
};

}  // namespace eve::gpgpu
