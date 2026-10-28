#pragma once
#include "common/Export.h"


#include "gpgpu/ComputeShader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::gpgpu {

class Gpgpu;
class GpuBuffer;
class Sequence;

/**
 * ECS ↔ GPGPU bridge: run a compute shader as an ECS System over N entities.
 *
 * Layout convention (matches Gpgpu::dispatch):
 *   - set=0 binding i = SSBO of tightly-packed floats for that field group
 *   - push constants: data[0]=dt, data[1]=entityCount (float), data[2..]=user
 *
 * Typical flow:
 *   1. setGpgpu + setShaderSource (or setShader)
 *   2. pack / upload float SoA-or-AoS streams per binding
 *   3. dispatch(entityCount)
 *   4. download / unpack back into components
 *
 * Script: `eve.ShaderSystem` (extends `eve.System`) wraps this class.
 * C++ ECS: use `packViewComponent` / `unpackViewComponent` in EcsGpu.h.
 */
/** @brief EVENGINE_API_WORLD public API. */
class EVENGINE_API_WORLD ShaderSystem {
public:
    static constexpr int kMaxBindings = ComputeShader::kMaxBindings;
    static constexpr int kDefaultLocalSize = 64;

    /** @brief Shader system. */
    ShaderSystem() = default;
    /** @brief Shader system. */
    ~ShaderSystem();

    ShaderSystem(const ShaderSystem &) = delete;
    ShaderSystem &operator=(const ShaderSystem &) = delete;

    /** @brief Sets the gpgpu. */
    void setGpgpu(Gpgpu *gpu);
    /** @brief Returns the gpgpu. */
    Gpgpu *getGpgpu() const { return gpu_; }

    /** Compile GLSL compute and take ownership of the resulting ComputeShader. */
    /** @brief Sets the shader source. */
    void setShaderSource(const std::string &glsl);

    /**
     * Use an existing ComputeShader. Does not take ownership unless
     * takeOwnership is true.
     */
    /** @brief Sets the shader. */
    void setShader(ComputeShader *shader, bool takeOwnership = false);

    /** @brief Returns the shader. */
    ComputeShader *getShader() const { return shader_; }

    /** @brief Sets the local size. */
    void setLocalSize(int localSize);
    /** @brief Returns the local size. */
    int getLocalSize() const { return localSize_; }

    /** Ensure binding has a storage buffer with at least floatCount floats. */
    /** @brief Ensure buffer. */
    GpuBuffer *ensureBuffer(int binding, int floatCount);

    /** @brief Returns the buffer. */
    GpuBuffer *getBuffer(int binding) const;

    /**
     * @brief Attach a non-owning resident buffer produced by another GPU system.
     * @param binding Destination shader binding.
     * @param buffer Buffer whose owner must outlive this attachment and any recorded work.
     * Reattach after the owner reallocates the buffer.
     */
    void attachBuffer(int binding, GpuBuffer *buffer);

    /** @brief Uploads upload. */
    void upload(int binding, const float *data, int floatCount);
    /** @brief Downloads download. */
    void download(int binding, float *out, int floatCount) const;

    /** @brief Upload floats into an existing resident buffer range. */
    void uploadRange(int binding, const float *data, int floatCount, int startFloat);
    /** @brief Download floats from an existing resident buffer range. */
    void downloadRange(int binding, float *out, int floatCount, int startFloat) const;

    /** @brief Number of host-to-device uploads issued through this system. */
    uint64_t getUploadCount() const { return uploadCount_; }
    /** @brief Number of device-to-host downloads issued through this system. */
    uint64_t getDownloadCount() const { return downloadCount_; }
    /** @brief Number of compute dispatches issued through this system. */
    uint64_t getDispatchCount() const { return dispatchCount_; }
    /** @brief Reset transfer and dispatch counters used for profiling. */
    void resetStatistics();

    /** Convenience: upload from / download to a vector. */
    /** @brief Uploads upload. */
    void upload(int binding, const std::vector<float> &data);
    /** @brief Downloads download. */
    std::vector<float> download(int binding, int floatCount) const;

    /** @brief Sets the float. */
    void setFloat(int index, float value);
    /** @brief Returns the float. */
    float getFloat(int index) const;

    /**
     * Bind buffers, set push[0]=dt and push[1]=entityCount, dispatch workgroups.
     * No-op when shader/gpu missing or entityCount <= 0.
     */
    /** @brief Dispatches dispatch. */
    void dispatch(int entityCount, float dt = 0.f);

    /**
     * @brief Record this system into a caller-owned sequence without submitting or waiting.
     * The sequence and all attached buffers must remain alive through Sequence::submit().
     */
    void recordDispatch(Sequence *sequence, int entityCount, float dt = 0.f);

    /** @brief Clears buffers. */
    void clearBuffers();

private:
    Gpgpu *gpu_ = nullptr;
    ComputeShader *shader_ = nullptr;
    bool ownsShader_ = false;
    int localSize_ = kDefaultLocalSize;
    GpuBuffer *buffers_[kMaxBindings]{};
    bool ownsBuffer_[kMaxBindings]{};
    uint64_t         uploadCount_   = 0;
    mutable uint64_t downloadCount_ = 0;
    uint64_t         dispatchCount_ = 0;
};

}  // namespace eve::gpgpu
