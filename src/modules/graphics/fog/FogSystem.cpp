#include "graphics/fog/FogSystem.h"

#include "common/Diagnostic.h"
#include "common/Status.h"
#include "graphics/AtmosphereVolume.h"
#include "graphics/Volumetric.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace eve::graphics::fog {

Result<void> FogSystem::setQuality(FogQuality quality) {
    quality_ = quality;
    beerCache_.invalidate();
    return Result<void>::success();
}

Result<void> FogSystem::configureDomain(int simWidth, int simHeight, int simDepth,
                                        const FogWorldBounds& bounds, float fixedDt) {
    auto resized = density_.resize(simWidth, simHeight, simDepth, bounds);
    if (!resized.ok()) return resized;
    auto configured = fluid_.configure(density_, fixedDt);
    if (!configured.ok()) return configured;
    beerCache_.invalidate();
    return Result<void>::success();
}

Result<void> FogSystem::seedHeightFog(float baseDensity, float baseHeight, float falloff,
                                      float curlScale, std::uint32_t seed) {
    auto seeded = density_.seedHeightBand(baseDensity, baseHeight, falloff, curlScale, seed);
    if (!seeded.ok()) return seeded;
    auto pulled = fluid_.pullDensity(density_);
    if (!pulled.ok()) return pulled;
    beerCache_.invalidate();
    return Result<void>::success();
}

Result<FogCflReport> FogSystem::stepSimulation(float dt) {
    auto stepped = fluid_.step(dt, wind_, &interactor_, simTime_);
    if (!stepped.ok()) return stepped;
    auto pushed = fluid_.pushDensity(density_);
    if (!pushed.ok()) return Result<FogCflReport>::failure(pushed.status());
    simTime_ += dt;
    ++frameIndex_;
    beerCache_.invalidate();
    return stepped;
}

Result<void> FogSystem::ensureBeerCache(const glm::vec3& lightDir, const glm::vec3& lightColor,
                                        float intensity) {
    const auto budget = budgetFor(quality_);
    if (!budget.beerLightCache) {
        beerCache_.invalidate();
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
    if (beerCache_.matches(density_.revision(),
                           glm::length(lightDir) > 1e-6f ? glm::normalize(lightDir) : glm::vec3(0, 1, 0),
                           intensity)) {
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
    return beerCache_.rebuild(density_, profile_, lightDir, lightColor, intensity, 6);
}

Result<FogRayResult> FogSystem::marchRay(const glm::vec3& origin, const glm::vec3& direction,
                                         const FogVolumeBound& bound, const glm::vec3& lightDir,
                                         const glm::vec3& lightColor, float lightIntensity,
                                         const glm::vec3& skyAmbient, const glm::vec3& groundAmbient,
                                         float sceneDepth) const {
    const auto budget = budgetFor(quality_);
    const BeerLightCache* cache =
        (budget.beerLightCache && beerCache_.valid()) ? &beerCache_ : nullptr;
    return rayMarch_.integrate(density_, profile_, origin, direction, bound, lightDir, lightColor,
                               lightIntensity, skyAmbient, groundAmbient, budget.raySamples, sceneDepth,
                               cache, quality_ != FogQuality::PhysicalReference);
}

Result<int> FogSystem::renderToFroxel(AtmosphereVolume& volume, const glm::mat4& invViewProj,
                                      const glm::vec3& lightDir, const glm::vec3& lightColor,
                                      float intensity) {
    const auto budget = budgetFor(quality_);
    if (volume.getWidth() != budget.froxelWidth || volume.getHeight() != budget.froxelHeight ||
        volume.getDepth() != budget.froxelDepth) {
        volume.resize(budget.froxelWidth, budget.froxelHeight, budget.froxelDepth);
        volume.setDepthRange(0.1f, std::max(density_.bounds().size().z, 40.f));
    } else {
        volume.clear();
    }

    auto cacheReady = ensureBeerCache(lightDir, lightColor, intensity);
    if (!cacheReady.ok()) return Result<int>::failure(cacheReady.status());

    auto injected =
        froxelBridge_.inject(volume, density_, profile_, invViewProj, budget.occupancySkip);
    if (!injected.ok()) return injected;

    const BeerLightCache* cache =
        (budget.beerLightCache && beerCache_.valid()) ? &beerCache_ : nullptr;
    auto integrated =
        froxelBridge_.integrate(volume, density_, profile_, lightDir, lightColor, intensity, cache);
    if (!integrated.ok()) return Result<int>::failure(integrated.status());

    if (budget.temporalHistory && frameIndex_ > 1) {
        // Lightweight self-history: blend with a cleared-then-reintegrated snapshot is
        // intentionally skipped here; callers may keep an external history volume.
        (void)budget.historyWeight;
    }
    return injected;
}

Result<int> FogSystem::syncToVolumetric(Volumetric* volumetric, const glm::vec3& lightDir,
                                        const glm::vec3& lightColor, float intensity) {
    if (!volumetric) {
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "volumetric is null", "volumetric", {}, "graphics.fog"));
    }
    AtmosphereVolume* volume = volumetric->getAtmosphereVolume();
    if (!volume) {
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "volumetric has no AtmosphereVolume; call configureFroxelGrid first",
            "atmosphereVolume", {}, "graphics.fog"));
    }

    // Keep Volumetric atlas metadata in sync with the quality grid. Resizing the
    // AtmosphereVolume alone leaves froxelAtlasCols_/Rows_ stale and corrupts upload.
    const auto budget = budgetFor(quality_);
    float nearDistance = volume->getNearDistance();
    float farDistance = volume->getFarDistance();
    if (!(farDistance > nearDistance)) {
        nearDistance = 0.1f;
        farDistance = std::max(density_.bounds().size().z, 40.f);
    }
    volumetric->configureFroxelGrid(budget.froxelWidth, budget.froxelHeight, budget.froxelDepth,
                                    nearDistance, farDistance);

    auto cacheReady = ensureBeerCache(lightDir, lightColor, intensity);
    if (!cacheReady.ok()) return Result<int>::failure(cacheReady.status());

    auto injected = froxelBridge_.inject(*volume, density_, profile_, volumetric->getInvViewProj(),
                                        budget.occupancySkip);
    if (!injected.ok()) return injected;

    const BeerLightCache* cache =
        (budget.beerLightCache && beerCache_.valid()) ? &beerCache_ : nullptr;
    auto integrated =
        froxelBridge_.integrate(*volume, density_, profile_, lightDir, lightColor, intensity, cache);
    if (!integrated.ok()) return Result<int>::failure(integrated.status());
    return injected;
}

Result<FogRayResult> FogSystem::integrateBeam(const AnalyticBeam& beam, const glm::vec3& origin,
                                              const glm::vec3& direction, float sceneDepth,
                                              float mediumDensity) const {
    const auto budget = budgetFor(quality_);
    return volLight_.integrate(beam, profile_, origin, direction, sceneDepth, budget.volLightSegments,
                               mediumDensity);
}

Result<std::vector<DustParticle>> FogSystem::generateDust(int count, float turbulenceMeters,
                                                          std::uint32_t seed) const {
    return dust_.generate(density_.bounds(), count, simTime_, turbulenceMeters, seed);
}

ContinuousArtOutput FogSystem::stylize(const FogRayResult& physical, float viewZ,
                                       float neighborOpacity) const noexcept {
    return art_.stylize(physical, viewZ, neighborOpacity);
}

}  // namespace eve::graphics::fog
