#pragma once

#include <array>
#include <cstdint>

namespace eve::gpgpu {

class GpuBuffer;

/**
 * Backend-agnostic compute program.
 * Bind storage buffers then dispatch via Gpgpu::dispatch.
 * Push constants: float[32] (same size as graphics::Shader).
 */
class ComputeShader {
public:
    static constexpr int kMaxBindings = 8;
    static constexpr int kMaxFloats = 32;
    static constexpr uint32_t kPushConstantBytes = uint32_t(kMaxFloats * sizeof(float));

    ComputeShader() = default;
    virtual ~ComputeShader() = default;

    ComputeShader(const ComputeShader &) = delete;
    ComputeShader &operator=(const ComputeShader &) = delete;

    /** Bind a storage buffer to set=0 binding. binding in [0, kMaxBindings). */
    virtual void bindBuffer(int binding, GpuBuffer *buffer) = 0;
    virtual GpuBuffer *getBoundBuffer(int binding) const = 0;

    virtual void setFloat(int index, float value) = 0;
    virtual float getFloat(int index) const = 0;

    virtual void clearBindings() = 0;

    const float *pushConstantData() const { return push_.data(); }

protected:
    std::array<float, kMaxFloats> push_{};
};

}  // namespace eve::gpgpu
