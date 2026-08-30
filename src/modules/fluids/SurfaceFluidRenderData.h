#pragma once

#include "fluids/SurfaceDropletSimulation.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace eve::fluids {

/** @brief Material mapping from simulated wetness to a PBR surface response. */
struct WetSurfaceMaterialParams {
    float dryRoughness = 0.48f;
    float wetRoughness = 0.08f;
    float drySpecular = 0.35f;
    float wetSpecular = 0.92f;
    float wetDarkening = 0.12f;
    float normalStrength = 0.18f;
};

/** @brief Evaluated material values suitable for a wet-surface shader. */
struct WetSurfaceMaterialSample {
    float wetness = 0.f;
    float roughness = 0.48f;
    float specular = 0.35f;
    float darkening = 0.f;
    float normalStrength = 0.f;
};

/** @brief World-space oriented cap instance consumed by droplet instancing shaders. */
struct SurfaceDropletRenderInstance {
    uint64_t  id = 0;
    glm::vec3 position{0.f};
    glm::vec3 normal{0.f, 1.f, 0.f};
    glm::vec3 majorAxis{1.f, 0.f, 0.f};
    glm::vec3 minorAxis{0.f, 0.f, 1.f};
    glm::vec3 velocity{0.f};
    float     capHeight = 0.f;
    float     wetness = 0.f;
};

/** @brief Tuning for converting simulated droplets into render instances. */
struct SurfaceFluidRenderParams {
    float velocityStretch = 0.30f;
    float maxAspectRatio = 2.6f;
    float surfaceOffset = 0.001f;
    WetSurfaceMaterialParams wetMaterial;
};

/**
 * @brief Builds renderer-independent droplet instances and wet PBR parameters.
 *
 * Major/minor axes lie in the current surface tangent plane. Their footprint
 * preserves area while velocity changes aspect ratio, avoiding billboard-like
 * circles and keeping the data usable by Vulkan, WebGPU and CPU references.
 */
class SurfaceFluidRenderData {
public:
    /** @brief Rebuild attached droplet instances from the latest simulation pose. */
    void update(const FluidSurfaceBinding& binding, const SurfaceDropletSimulation& simulation,
                const SurfaceWetnessField* wetness = nullptr,
                const SurfaceFluidRenderParams& params = {});

    /** @brief Evaluate wet PBR values from a normalized wetness input. */
    static WetSurfaceMaterialSample evaluateMaterial(float wetness,
        const WetSurfaceMaterialParams& params = {});

    /** @return current oriented droplet instances. */
    const std::vector<SurfaceDropletRenderInstance>& droplets() const { return droplets_; }

private:
    std::vector<SurfaceDropletRenderInstance> droplets_;
};

}  // namespace eve::fluids
