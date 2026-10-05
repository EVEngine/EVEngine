#include "graphics/fog/FogFroxelBridge.h"

#include "common/Diagnostic.h"
#include "graphics/AtmosphereVolume.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics::fog {
namespace {

[[nodiscard]] glm::vec3 reconstructFroxelCenter(const AtmosphereVolume& volume, int x, int y, int z,
                                                const glm::mat4& invViewProj) {
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(std::max(volume.getWidth(), 1));
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(std::max(volume.getHeight(), 1));
    const float dist = volume.sliceDistance(z);
    // NDC z is unused; place the froxel along the view ray at `dist`.
    const glm::vec4 nearH = invViewProj * glm::vec4(u * 2.f - 1.f, v * 2.f - 1.f, 0.f, 1.f);
    const glm::vec4 farH = invViewProj * glm::vec4(u * 2.f - 1.f, v * 2.f - 1.f, 1.f, 1.f);
    const glm::vec3 nearP = glm::vec3(nearH) / std::max(nearH.w, 1e-6f);
    const glm::vec3 farP = glm::vec3(farH) / std::max(farH.w, 1e-6f);
    const glm::vec3 dir = farP - nearP;
    const float dirLen = glm::length(dir);
    if (dirLen < 1e-6f) return nearP;
    return nearP + dir * (dist / dirLen);
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
                                        const FogProfile& profile, const glm::vec3& lightDir,
                                        const glm::vec3& lightColor, float intensity,
                                        const BeerLightCache* beerCache) {
    if (volume.getWidth() <= 0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "froxel volume empty",
                                                       "integrate", {}, "graphics.fog"));
    }
    const float len = glm::length(lightDir);
    const glm::vec3 dir = len > 1e-6f ? lightDir / len : glm::vec3(0.f, 1.f, 0.f);
    const float phaseScale = profile.phase(1.f);  // forward peak used as scale baseline

    if (beerCache && beerCache->valid()) {
        for (int z = 0; z < volume.getDepth(); ++z) {
            for (int y = 0; y < volume.getHeight(); ++y) {
                for (int x = 0; x < volume.getWidth(); ++x) {
                    // Approximate froxel world using density-field bounds center height.
                    const FogWorldBounds& b = field.bounds();
                    const glm::vec3 world =
                        b.minimum + glm::vec3(((x + 0.5f) / std::max(volume.getWidth(), 1)) * b.size().x,
                                              ((y + 0.5f) / std::max(volume.getHeight(), 1)) * b.size().y,
                                              volume.sliceDistance(z));
                    volume.setLightVisibility(x, y, z, beerCache->sampleTransmittance(field, world));
                }
            }
        }
    }

    volume.integrate(lightColor * intensity, phaseScale);
    (void)dir;
    return Result<void>::success();
}

}  // namespace eve::graphics::fog
