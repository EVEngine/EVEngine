#include "fluids/SurfaceFluidRenderData.h"

#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>

namespace eve::fluids {

void SurfaceFluidRenderData::update(const FluidSurfaceBinding& binding,
                                    const SurfaceDropletSimulation& simulation,
                                    const SurfaceWetnessField* wetness,
                                    const SurfaceFluidRenderParams& params) {
    droplets_.clear();
    droplets_.reserve(simulation.droplets().size());
    const float stretch = std::max(0.f, params.velocityStretch);
    const float maximumAspect = std::max(1.f, params.maxAspectRatio);
    const float theta = glm::radians(std::clamp(
        simulation.params().contactAngleDegrees, 5.f, 175.f));

    for (const SurfaceDroplet& droplet : simulation.droplets()) {
        const SurfaceSample surface = binding.evaluate(droplet.location, 0.f);
        const float radius = simulation.dropletRadius(droplet.volume);
        const float speed = glm::length(droplet.relativeVelocity);
        const float aspect = std::clamp(1.f + speed * stretch, 1.f, maximumAspect);
        const float axisScale = std::sqrt(aspect);

        glm::vec3 direction = droplet.relativeVelocity;
        direction -= surface.normal * glm::dot(direction, surface.normal);
        if (glm::length2(direction) > 1e-12f)
            direction = glm::normalize(direction);
        else
            direction = surface.tangent;
        glm::vec3 across = glm::cross(surface.normal, direction);
        if (glm::length2(across) > 1e-12f)
            across = glm::normalize(across);
        else
            across = surface.bitangent;

        SurfaceDropletRenderInstance instance;
        instance.id = droplet.id;
        instance.position = surface.position + surface.normal * std::max(0.f, params.surfaceOffset);
        instance.normal = surface.normal;
        instance.majorAxis = direction * (radius * axisScale);
        instance.minorAxis = across * (radius / axisScale);
        instance.velocity = surface.velocity + droplet.relativeVelocity;
        instance.capHeight = radius * std::tan(theta * 0.5f);
        instance.wetness = wetness ? std::clamp(wetness->sample(droplet.location), 0.f, 1.f) : 0.f;
        droplets_.push_back(instance);
    }
}

WetSurfaceMaterialSample SurfaceFluidRenderData::evaluateMaterial(
    float wetness, const WetSurfaceMaterialParams& params) {
    WetSurfaceMaterialSample sample;
    sample.wetness = std::clamp(wetness, 0.f, 1.f);
    const float response = sample.wetness * sample.wetness * (3.f - 2.f * sample.wetness);
    sample.roughness = glm::mix(params.dryRoughness, params.wetRoughness, response);
    sample.specular = glm::mix(params.drySpecular, params.wetSpecular, response);
    sample.darkening = std::max(0.f, params.wetDarkening) * response;
    sample.normalStrength = std::max(0.f, params.normalStrength) * response;
    return sample;
}

}  // namespace eve::fluids
