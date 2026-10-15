#pragma once
#include "common/Export.h"


#include <cstdint>
#include <glm/glm.hpp>

namespace eve::graphics {

/**
 * @brief How a Light3D produces real-time shadows.
 * `Auto` resolves per light type (dir→CSM, spot→perspective, point→cube).
 */
enum class ShadowMethod : std::uint8_t {
    Auto = 0,
    None,
    CascadedDirectional,
    PerspectiveSpot,
    CubePoint,
};

/** @brief ShadowConfig public API. */
struct ShadowConfig {
    static constexpr int kCascades = 3;
    /** @brief Extra depth-array layers for spot (perspective) local shadows. */
    static constexpr int kLocalSlots = 4;
    static constexpr int kTotalLayers = kCascades + kLocalSlots;
    static constexpr int kMapSize = 2048;
    static constexpr float kSplitLambda = 0.7f;
    /** Practical CSM range. Camera far planes for large architecture are often
     *  hundreds of meters; 3×2048² cascades over that range have no resolution. */
    static constexpr float kMaxDistance = 64.f;
};

/** @brief Per-frame / per-draw CSM + local-spot constants (std140). */
struct ShadowUBO {
    glm::mat4 lightVP[ShadowConfig::kCascades]{};
    glm::vec4 splits{0.f};  // xyz = view-space +Z split ends (camera-forward distance); w = strength
    glm::vec4 bias{0.002f, 0.f, 1.f, 0.02f};  // w=directional source angular radius
    glm::vec4 cascadeBias{0.f};             // xyz = per-cascade NDC compare bias
    glm::vec4 cascadeTexel{0.f};            // xyz = world units per shadow texel
    /** @brief Perspective VPs for local spot slots (layers kCascades .. kTotalLayers-1). */
    glm::mat4 localVP[ShadowConfig::kLocalSlots]{};
    /** @brief Per packed-light local slot index (0..3) or -1; eight floats across two vec4s. */
    glm::vec4 localSlot01{-1.f, -1.f, -1.f, -1.f};
    glm::vec4 localSlot23{-1.f, -1.f, -1.f, -1.f};
    /** @brief xyzw = per-local-slot NDC bias. */
    glm::vec4 localBias{0.002f};
    /** @brief x = local slot count; y = local strength scale; zw unused. */
    glm::vec4 localMeta{0.f, 1.f, 0.f, 0.f};
};

/** @brief ShadowUpload public API. */
struct ShadowUpload {
    ShadowUBO ubo{};
    bool active = false;
};

/**
 * @brief Build 3 cascade light view-proj matrices for a directional light.
 * @param lightDirTowardSurface  world-space direction toward the surface (same as Light3DGpu)
 * @param eye / target / up      camera basis
 * @param fovYRad / aspect / nearZ / farZ  camera clip
 * Splits are camera-forward (view-space +Z) distances. Shaders must select cascades
 * with the same metric (max(-viewPos.z, 0)), not euclidean eye-to-point length.
 * Camera far is clamped to kMaxDistance so large scenes keep usable texel density.
 */
EVENGINE_API_BACKENDS ShadowUpload buildDirectionalCSM(const glm::vec3 &lightDirTowardSurface, const glm::vec3 &eye,
                                                       const glm::vec3 &target, const glm::vec3 &up, float fovYRad,
                                                       float aspect, float nearZ, float farZ, float bias,
                                                       float strength);

}  // namespace eve::graphics
