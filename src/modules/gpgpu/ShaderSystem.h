#pragma once

#include "gpgpu/ComputeShader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::gpgpu {

class Gpgpu;
class GpuBuffer;

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
class ShaderSystem {
public:
    static constexpr int kMaxBindings = ComputeShader::kMaxBindings;
    static constexpr int kDefaultLocalSize = 64;

    ShaderSystem() = default;
    ~ShaderSystem();

    ShaderSystem(const ShaderSystem &) = delete;
    ShaderSystem &operator=(const ShaderSystem &) = delete;

    void setGpgpu(Gpgpu *gpu);
    Gpgpu *getGpgpu() const { return gpu_; }

    /** Compile GLSL compute and take ownership of the resulting ComputeShader. */
    void setShaderSource(const std::string &glsl);

    /**
     * Use an existing ComputeShader. Does not take ownership unless
     * takeOwnership is true.
     */
    void setShader(ComputeShader *shader, bool takeOwnership = false);

    ComputeShader *getShader() const { return shader_; }

    void setLocalSize(int localSize);
    int getLocalSize() const { return localSize_; }

    /** Ensure binding has a storage buffer with at least floatCount floats. */
    GpuBuffer *ensureBuffer(int binding, int floatCount);

    GpuBuffer *getBuffer(int binding) const;

    void upload(int binding, const float *data, int floatCount);
    void download(int binding, float *out, int floatCount) const;

    /** Convenience: upload from / download to a vector. */
    void upload(int binding, const std::vector<float> &data);
    std::vector<float> download(int binding, int floatCount) const;

    void setFloat(int index, float value);
    float getFloat(int index) const;

    /**
     * Bind buffers, set push[0]=dt and push[1]=entityCount, dispatch workgroups.
     * No-op when shader/gpu missing or entityCount <= 0.
     */
    void dispatch(int entityCount, float dt = 0.f);

    void clearBuffers();

private:
    Gpgpu *gpu_ = nullptr;
    ComputeShader *shader_ = nullptr;
    bool ownsShader_ = false;
    int localSize_ = kDefaultLocalSize;
    GpuBuffer *buffers_[kMaxBindings]{};
    bool ownsBuffer_[kMaxBindings]{};
};

}  // namespace eve::gpgpu
