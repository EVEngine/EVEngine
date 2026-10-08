#pragma once

#include "common/GpuResidentBufferView.h"

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
    /** @brief Constructs a GpuBuffer. */
    GpuBuffer() = default;
    /** @brief Releases GpuBuffer resources. */
    virtual ~GpuBuffer() = default;

    GpuBuffer(const GpuBuffer &) = delete;
    GpuBuffer &operator=(const GpuBuffer &) = delete;

    /** @brief Byte length of the owned buffer. */
    virtual int getSize() const = 0;
    /** @brief Returns the usage. */
    virtual std::string getUsage() const = 0;

    /** @brief Writes data. */
    virtual void writeData(data::ByteData *data, int dstOffset = 0) = 0;
    /** @brief Reads data. */
    virtual data::ByteData *readData(int srcOffset = 0, int size = -1) = 0;

    /** @brief Writes float 32. */
    virtual void writeFloat32(int floatIndex, float value) = 0;
    /** @brief Reads float 32. */
    virtual float readFloat32(int floatIndex) = 0;
    /** @brief Fill float 32. */
    virtual void fillFloat32(float value) = 0;

    /** @brief Bulk float upload/download (one transfer). startIndex is in floats. */
    virtual void writeFloat32s(const float *data, int count, int startIndex = 0) = 0;
    /** @brief Reads float 32 s. */
    virtual void readFloat32s(float *out, int count, int startIndex = 0) const = 0;

    /** @brief Uploads bytes. */
    virtual void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0) = 0;
    /** @brief Downloads bytes. */
    virtual void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0) const = 0;

    /**
     * @brief Return a transient non-owning native view for same-device rendering.
     * Unsupported/test buffers return an Unknown view. The buffer owns the handle
     * and must outlive every frame command that consumes the returned view.
     */
    virtual GpuResidentBufferView residentView() const { return {}; }
};

}  // namespace eve::gpgpu
