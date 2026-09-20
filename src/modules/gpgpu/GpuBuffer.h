#pragma once

#include "common/GpuResidentBufferView.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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

    /**
     * @brief 回读一批 float 并**拥有一份拷贝**（脚本与工具面）。
     *
     * @param count 要读的 float 个数；@param startIndex 起始下标，单位是 **float** 不是字节。
     * @return 读到的值；返回的数组长度就是这次真正读到的个数。
     * @remarks 范围**按容量裁剪**，绝不越界：`count <= 0`、`startIndex < 0`、`startIndex` 已经
     *          在容量之后都返回空数组，`count` 超出剩余容量时只返回剩下的那些。脚本面没有异常
     *          可读，所以"多要了"不该变成一个脚本错误，也不该读到缓冲区外面去；元素个数由
     *          `getSize() / 4` 决定，调用方用 `.len()` 就能看出实际拿到了多少。
     * @cost 一次 vector 分配 + 一次传输（与读到的个数成正比）。热路径请用指针版重载
     *       （零分配、由调用方持有缓冲）。
     * @remarks 派生类不需要覆写：它只是指针版的一次包装，转成 owning 值是为了脚本面能返回数组。
     */
    [[nodiscard]] std::vector<float> readFloat32sCopy(int count, int startIndex = 0) const {
        if (count <= 0 || startIndex < 0) return {};
        const int capacity = getSize() / int(sizeof(float));
        if (startIndex >= capacity) return {};
        const int          available = std::min(count, capacity - startIndex);
        std::vector<float> out(size_t(available), 0.f);
        readFloat32s(out.data(), available, startIndex);
        return out;
    }

    virtual void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0) = 0;
    virtual void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0) const = 0;

    /**
     * @brief Return a transient non-owning native view for same-device rendering.
     * Unsupported/test buffers return an Unknown view. The buffer owns the handle
     * and must outlive every frame command that consumes the returned view.
     */
    virtual GpuResidentBufferView residentView() const { return {}; }
};

}  // namespace eve::gpgpu
