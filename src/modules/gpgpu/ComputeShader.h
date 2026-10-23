#pragma once

#include <array>
#include <cstdint>

namespace eve::gpgpu {

class GpuBuffer;

/**
 * @brief Backend-agnostic compute program.
 * Bind storage buffers then dispatch via Gpgpu::dispatch.
 * Push constants: float[32] (same size as graphics::Shader).
 */
class ComputeShader {
public:
    static constexpr int kMaxBindings = 8;
    static constexpr int kMaxFloats = 32;
    static constexpr uint32_t kPushConstantBytes = uint32_t(kMaxFloats * sizeof(float));

    /** @brief Constructs a ComputeShader. */
    ComputeShader() = default;
    /** @brief Releases ComputeShader resources. */
    virtual ~ComputeShader() = default;

    ComputeShader(const ComputeShader &) = delete;
    ComputeShader &operator=(const ComputeShader &) = delete;

    /** @brief Bind a storage buffer to set=0 binding. binding in [0, kMaxBindings). */
    virtual void bindBuffer(int binding, GpuBuffer *buffer) = 0;
    /** @brief Returns the bound buffer. */
    virtual GpuBuffer *getBoundBuffer(int binding) const = 0;

    /** @brief Sets the float. */
    virtual void setFloat(int index, float value) = 0;
    /** @brief Returns the float. */
    virtual float getFloat(int index) const = 0;

    /** @brief Clears bindings. */
    virtual void clearBindings() = 0;

    /** @brief Pushes constant data. */
    const float *pushConstantData() const { return push_.data(); }

protected:
    std::array<float, kMaxFloats> push_{};
};

}  // namespace eve::gpgpu
