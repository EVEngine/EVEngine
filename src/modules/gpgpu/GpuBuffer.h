#pragma once

#include <cstdint>
#include <string>

namespace eve::data {
class ByteData;
}

namespace eve::gpgpu {

/**
 * @brief Backend-agnostic GPU buffer for compute (storage) or CPU staging transfers.
 * Squirrel-owned; derived class destroys GPU resources in destructor.
 */
class GpuBuffer {
public:
    GpuBuffer() = default;
    virtual ~GpuBuffer() = default;

    GpuBuffer(const GpuBuffer &) = delete;
    GpuBuffer &operator=(const GpuBuffer &) = delete;

    virtual int getSize() const = 0;
    virtual std::string getUsage() const = 0;

    virtual void writeData(data::ByteData *data, int dstOffset = 0) = 0;
    virtual data::ByteData *readData(int srcOffset = 0, int size = -1) = 0;

    virtual void writeFloat32(int floatIndex, float value) = 0;
    virtual float readFloat32(int floatIndex) = 0;
    virtual void fillFloat32(float value) = 0;

    /** @brief Bulk float upload/download (one transfer). startIndex is in floats. */
    virtual void writeFloat32s(const float *data, int count, int startIndex = 0) = 0;
    virtual void readFloat32s(float *out, int count, int startIndex = 0) const = 0;

    virtual void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0) = 0;
    virtual void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0) const = 0;
};

}  // namespace eve::gpgpu
