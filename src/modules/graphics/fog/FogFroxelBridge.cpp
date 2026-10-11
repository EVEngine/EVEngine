#include "graphics/fog/FogFroxelBridge.h"

#include "common/Diagnostic.h"
#include "graphics/AtmosphereVolume.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace eve::graphics::fog {
namespace {

/** Reconstruct a froxel center the same way AtmosphereVolume injects height fog. */
[[nodiscard]] glm::vec3 reconstructFroxelCenter(const AtmosphereVolume& volume, int x, int y, int z,
                                                const glm::mat4& invViewProj) {
    const int width = std::max(volume.getWidth(), 1);
    const int height = std::max(volume.getHeight(), 1);
    const float ndcX = ((static_cast<float>(x) + 0.5f) / static_cast<float>(width)) * 2.f - 1.f;
    const float ndcY = ((static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * 2.f - 1.f;
    auto unproject = [&](float ndcZ) {
        const glm::vec4 homogeneous = invViewProj * glm::vec4(ndcX, ndcY, ndcZ, 1.f);
        return glm::vec3(homogeneous) / homogeneous.w;
    };
    const glm::vec3 nearPoint = unproject(0.f);
    const glm::vec3 farPoint = unproject(1.f);
    const float span = std::max(volume.getFarDistance() - volume.getNearDistance(), 1e-6f);
    const float depth01 = (volume.sliceDistance(z) - volume.getNearDistance()) / span;
    return glm::mix(nearPoint, farPoint, depth01);
}

}  // namespace

Result<int> FogFroxelBridge::inject(AtmosphereVolume& volume, const FogDensityField& field,
                                    const FogProfile& profile, const glm::mat4& invViewProj,
                                    bool occupancySkip) {
    if (volume.getWidth() <= 0 || field.width() <= 0) {
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                      "froxel/density volumes are empty", "inject",
                                                      {}, "graphics.fog"));
    }

    int written = 0;
    for (int z = 0; z < volume.getDepth(); ++z) {
        for (int y = 0; y < volume.getHeight(); ++y) {
            for (int x = 0; x < volume.getWidth(); ++x) {
                const glm::vec3 world = reconstructFroxelCenter(volume, x, y, z, invViewProj);
                const float density = field.sampleDensity(world);
                if (occupancySkip && density < 1e-4f) continue;
                auto& froxel = volume.at(x, y, z);
                const float sigmaT = profile.extinctionAt(density);
                froxel.extinction += sigmaT;
                froxel.scattering += profile.scatteringAt(density);
                froxel.anisotropy = profile.anisotropy();
                froxel.emissive += field.sampleLightAssist(world);
                ++written;
            }
        }
    }
    return Result<int>::success(written);
}

Result<void> FogFroxelBridge::integrate(AtmosphereVolume& volume, const FogDensityField& field,
                                        const FogProfile& profile, const glm::mat4& invViewProj,
                                        const glm::vec3& lightDir, const glm::vec3& lightColor,
                                        float intensity, const BeerLightCache* beerCache) {
    if (volume.getWidth() <= 0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "froxel volume empty",
                                                       "integrate", {}, "graphics.fog"));
    }
    const float len = glm::length(lightDir);
    const glm::vec3 dir = len > 1e-6f ? lightDir / len : glm::vec3(0.f, 1.f, 0.f);
    const float phaseScale = profile.phase(glm::dot(dir, glm::vec3(0.f, 0.f, -1.f)));

    if (beerCache && beerCache->valid()) {
        for (int z = 0; z < volume.getDepth(); ++z) {
            for (int y = 0; y < volume.getHeight(); ++y) {
                for (int x = 0; x < volume.getWidth(); ++x) {
                    const glm::vec3 world = reconstructFroxelCenter(volume, x, y, z, invViewProj);
                    volume.setLightVisibility(x, y, z, beerCache->sampleTransmittance(field, world));
                }
            }
        }
    }

    volume.integrate(lightColor * intensity, phaseScale);
    return Result<void>::success();
}

}  // namespace eve::graphics::fog
