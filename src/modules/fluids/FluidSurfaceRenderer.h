#pragma once

/**
 * @brief Screen-space fluid surface reconstruction (SSF).
 *
 * Splats particles into a depth/thickness buffer, bilaterally smooths the
 * depth (curvature-flow style), reconstructs normals from depth gradients and
 * shades the result as either water (Fresnel + refraction look) or mud
 * (diffuse, thickness-attenuated). The CPU reference mirrors the GLSL kernels
 * in FluidSsfKernels.h 1:1; the GPU path runs on the gpgpu compute queue.
 */

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::gpgpu {
class ComputeShader;
class Gpgpu;
class GpuBuffer;
class Sequence;
}  // namespace eve::gpgpu

namespace eve::fluids {

class FluidSimulator;

/** @brief Camera + reconstruction tuning for the SSF pipeline. */
struct FluidSurfaceParams {
    glm::vec3 eye{0.f, 0.f, -2.6f};
    glm::vec3 target{0.f};
    glm::vec3 up{0.f, 1.f, 0.f};
    float     fovYDeg          = 55.f;
    float     aspect           = 1.f;
    float     nearZ            = 0.05f;
    float     farZ             = 20.f;
    int       width            = 160;
    int       height           = 160;
    float     particleRadius   = 0.05f;
    int       smoothIterations = 2;
    float     depthFalloff     = 0.03f;
    float     thicknessScale   = 1.f;
    /** @brief 0 = water, 1 = mud. */
    int mode = 0;
};

/** @brief Buffer-level screen-space fluid renderer. */
class FluidSurfaceRenderer {
public:
    /**
     * @param params camera / resolution / material tuning.
     * @param preferGpu use compute kernels when a device is available.
     */
    FluidSurfaceRenderer(const FluidSurfaceParams& params, bool preferGpu);
    ~FluidSurfaceRenderer();

    FluidSurfaceRenderer(const FluidSurfaceRenderer&)            = delete;
    FluidSurfaceRenderer& operator=(const FluidSurfaceRenderer&) = delete;

    /** @brief Reconstruct a frame from particle positions. */
    void render(const std::vector<glm::vec3>& positions, float particleRadius);

    /** @brief Reconstruct a frame from a simulator's live particles. */
    void render(const FluidSimulator& sim);

    /** @brief Script-friendly wrapper: render(*sim) with a null check. */
    void renderFrom(FluidSimulator* sim);

    int getWidth() const { return params_.width; }
    int getHeight() const { return params_.height; }

    /** @brief Linear view depth per pixel (FLT_MAX where no fluid). */
    const std::vector<float>& depth() const { return depth_; }

    /** @brief View-space normals (0 where no fluid). */
    const std::vector<glm::vec3>& normals() const { return normals_; }

    /** @brief Accumulated thickness per pixel. */
    const std::vector<float>& thickness() const { return thickness_; }

    /** @brief RGBA8 shaded output. */
    const std::vector<uint8_t>& color() const { return color_; }

    /** @return true when the GPU compute path is active. */
    bool usingGpu() const { return gpuOk_; }

    /** @brief Switch shading: 0 water, 1 mud. */
    void setMode(int mode) { params_.mode = mode; }

    /** @brief Update the camera. */
    void setCamera(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up, float fovYDeg);

    /** @brief Write the color buffer as a PPM image (debug artifact). */
    void writePpm(const std::string& path) const;

private:
    void renderCpu();
    bool ensureGpu();
    void uploadParticles();
    void setCommonConstants(gpgpu::ComputeShader* shader, float falloff);

    FluidSurfaceParams params_;
    bool               preferGpu_ = false;
    bool               gpuOk_     = false;

    std::vector<glm::vec3> positions_;
    std::vector<float>     depth_;
    std::vector<float>     thickness_;
    std::vector<glm::vec3> normals_;
    std::vector<uint8_t>   color_;

    eve::gpgpu::Gpgpu*         gpgpu_     = nullptr;
    eve::gpgpu::ComputeShader* shClear_   = nullptr;
    eve::gpgpu::ComputeShader* shSplat_   = nullptr;
    eve::gpgpu::ComputeShader* shSmooth_  = nullptr;
    eve::gpgpu::ComputeShader* shNormal_  = nullptr;
    eve::gpgpu::ComputeShader* shShade_   = nullptr;
    eve::gpgpu::GpuBuffer*     bufParts_  = nullptr;
    eve::gpgpu::GpuBuffer*     bufDepthA_ = nullptr;
    eve::gpgpu::GpuBuffer*     bufDepthB_ = nullptr;
    eve::gpgpu::GpuBuffer*     bufThick_  = nullptr;
    eve::gpgpu::GpuBuffer*     bufNormal_ = nullptr;
    eve::gpgpu::GpuBuffer*     bufColor_  = nullptr;
    eve::gpgpu::GpuBuffer*     stDepth_   = nullptr;
    eve::gpgpu::GpuBuffer*     stThick_   = nullptr;
    eve::gpgpu::GpuBuffer*     stNormal_  = nullptr;
    eve::gpgpu::GpuBuffer*     stColor_   = nullptr;
    eve::gpgpu::Sequence*      seq_       = nullptr;
};

}  // namespace eve::fluids
