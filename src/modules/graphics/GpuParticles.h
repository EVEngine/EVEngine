#pragma once

#include "graphics/BlendMode.h"

#include <cstdint>

namespace eve::graphics {

class Texture;

/** @brief Sprite orientation supported by the resident GPU particle renderer. */
enum class GpuParticleFacingMode : std::uint32_t {
    ParticleRotation = 0,
    Velocity         = 1,
    Axis             = 2,
};

/** @brief Opaque backend-owned GPU particle emitter handle. */
using GpuParticleHandle = std::uint64_t;

/** @brief Invalid GPU particle handle returned when the backend cannot allocate the resource. */
inline constexpr GpuParticleHandle kInvalidGpuParticleHandle = 0;

/** @brief GPU particle state uploaded only for newly spawned particles. */
struct alignas(16) GpuParticleSpawn {
    float x          = 0.f;
    float y          = 0.f;
    float vx         = 0.f;
    float vy         = 0.f;
    float life       = 0.f;
    float lifetime   = 1.f;
    float size       = 1.f;
    float rotation   = 0.f;
    float spin       = 0.f;
    float frame      = 0.f;
    float radial     = 0.f;
    float tangential = 0.f;
    float ax         = 0.f;
    float ay         = 0.f;
    float noisePhase = 0.f;
    float reserved   = 0.f;
};

static_assert(sizeof(GpuParticleSpawn) == 64, "GPU particle layout must match GLSL std430");

/** @brief Parameters for one resident GPU simulation step. */
struct GpuParticleUpdate {
    float dt             = 0.f;
    float emitterX       = 0.f;
    float emitterY       = 0.f;
    float gravityX       = 0.f;
    float gravityY       = 0.f;
    float damping        = 0.f;
    float velocityLimit  = 0.f;
    float noiseStrength  = 0.f;
    float noiseFrequency = 1.f;
    float noiseSpeed     = 0.f;
    float time           = 0.f;
    float frameRate      = 0.f;
    float localOffsetX   = 0.f;
    float localOffsetY   = 0.f;
};

/** @brief Parameters for rendering a resident GPU particle emitter. */
struct GpuParticleDraw {
    Texture*              texture             = nullptr;
    BlendMode             blend               = BlendMode::Alpha;
    float                 viewportWidth       = 1.f;
    float                 viewportHeight      = 1.f;
    float                 cameraX             = 0.f;
    float                 cameraY             = 0.f;
    float                 cameraZoom          = 1.f;
    bool                  cameraEnabled       = false;
    float                 particleWidth       = 1.f;
    float                 particleHeight      = 1.f;
    float                 sizeStart           = 1.f;
    float                 sizeEnd             = 1.f;
    float                 stretchFactor       = 1.f;
    GpuParticleFacingMode facing              = GpuParticleFacingMode::ParticleRotation;
    float                 axisRotationRadians = 0.f;
    float                 colorStart[4]       = {1.f, 1.f, 1.f, 1.f};
    float                 colorEnd[4]         = {1.f, 1.f, 1.f, 0.f};
    int                   hframes             = 1;
    int                   vframes             = 1;
};

/** @brief Delayed non-blocking counters from a resident GPU emitter. */
struct GpuParticleStats {
    std::uint32_t alive = 0;
    /** @brief Instance count written into the most recently completed indirect command. */
    std::uint32_t instances       = 0;
    std::uint32_t spawned         = 0;
    std::uint32_t killed          = 0;
    std::uint32_t dropped         = 0;
    std::uint64_t submittedFrames = 0;
};

}  // namespace eve::graphics
