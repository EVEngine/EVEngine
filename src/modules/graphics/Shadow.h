#pragma once

#include <glm/glm.hpp>

namespace eve::graphics {

struct ShadowConfig {
    static constexpr int kCascades = 3;
    static constexpr int kMapSize = 1024;
    static constexpr float kSplitLambda = 0.7f;
    /** Practical CSM range. Camera far planes for large architecture are often
     *  hundreds of meters; 3×1024² cascades over that range have no resolution. */
    static constexpr float kMaxDistance = 64.f;
};

/** Per-frame / per-draw CSM constants (std140). Binding separate from Mesh3D Frame UBO. */
struct ShadowUBO {
    glm::mat4 lightVP[ShadowConfig::kCascades]{};
    glm::vec4 splits{0.f};  // xyz = view-space +Z split ends (camera-forward distance); w = strength
    glm::vec4 bias{0.002f, 0.f, 1.f, 0.f};  // x=bias, y=enabled, z=receive, w unused
};

struct ShadowUpload {
    ShadowUBO ubo{};
    bool active = false;
};

/**
 * Build 3 cascade light view-proj matrices for a directional light.
 * @param lightDirTowardSurface  world-space direction toward the surface (same as Light3DGpu)
 * @param eye / target / up      camera basis
 * @param fovYRad / aspect / nearZ / farZ  camera clip
 * Splits are camera-forward (view-space +Z) distances. Shaders must select cascades
 * with the same metric (max(-viewPos.z, 0)), not euclidean eye-to-point length.
 * Camera far is clamped to kMaxDistance so large scenes keep usable texel density.
 */
ShadowUpload buildDirectionalCSM(const glm::vec3 &lightDirTowardSurface, const glm::vec3 &eye,
                                 const glm::vec3 &target, const glm::vec3 &up, float fovYRad,
                                 float aspect, float nearZ, float farZ, float bias, float strength);

}  // namespace eve::graphics
