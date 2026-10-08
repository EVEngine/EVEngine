#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/AnalyticalVolLight.h"
#include "graphics/fog/BeerLightCache.h"
#include "graphics/fog/ContinuousArt.h"
#include "graphics/fog/FogDensityField.h"
#include "graphics/fog/FogFroxelBridge.h"
#include "graphics/fog/FogInteractor.h"
#include "graphics/fog/FogProfile.h"
#include "graphics/fog/FogRayMarch.h"
#include "graphics/fog/FogTypes.h"
#include "graphics/fog/MacFluidGrid.h"
#include "graphics/fog/ProceduralDust.h"
#include "graphics/fog/SceneWind.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics {
class AtmosphereVolume;
class Volumetric;
}

namespace eve::graphics::fog {

/**
 * @brief Orchestrates density, MAC transport, optics, froxel mapping, and art.
 *
 * Data ownership:
 *  - FogDensityField / MacFluidGrid own mutable media state
 *  - FogProfile owns optical constants
 *  - ContinuousArt only reads FogRayResult
 *  - BeerLightCache is a derived acceleration structure
 *
 * @ownership The FogSystem uniquely owns its lattices, caches and art params.
 *            AtmosphereVolume arguments are borrowed for the call only.
 * @lifetime Caller-owned; destroy after the last march/froxel call.
 * @thread Simulation / render thread that created it; not internally synchronized.
 * @reentrancy Public methods are not reentrant and invoke no script callbacks.
 */
class EVENGINE_API_WORLD FogSystem {
public:
    /** @brief Quality. */
    [[nodiscard]] FogQuality quality() const noexcept { return quality_; }
    /** @brief Budget. */
    [[nodiscard]] FogQualityBudget budget() const noexcept { return budgetFor(quality_); }
    /** @brief Simulation time. */
    [[nodiscard]] float simulationTime() const noexcept { return simTime_; }
    /** @brief Frame index. */
    [[nodiscard]] std::uint64_t frameIndex() const noexcept { return frameIndex_; }

    /** @brief Density field. */
    FogDensityField& densityField() noexcept { return density_; }
    /** @brief Density field. */
    const FogDensityField& densityField() const noexcept { return density_; }
    /** @brief Profile. */
    FogProfile& profile() noexcept { return profile_; }
    /** @brief Profile. */
    const FogProfile& profile() const noexcept { return profile_; }
    /** @brief Wind. */
    SceneWind& wind() noexcept { return wind_; }
    /** @brief Wind. */
    const SceneWind& wind() const noexcept { return wind_; }
    /** @brief Fluid. */
    MacFluidGrid& fluid() noexcept { return fluid_; }
    /** @brief Fluid. */
    const MacFluidGrid& fluid() const noexcept { return fluid_; }
    /** @brief Interactor. */
    FogInteractor& interactor() noexcept { return interactor_; }
    /** @brief Interactor. */
    const FogInteractor& interactor() const noexcept { return interactor_; }
    /** @brief Beer cache. */
    BeerLightCache& beerCache() noexcept { return beerCache_; }
    /** @brief Beer cache. */
    const BeerLightCache& beerCache() const noexcept { return beerCache_; }
    /** @brief Art. */
    ContinuousArt& art() noexcept { return art_; }
    /** @brief Art. */
    const ContinuousArt& art() const noexcept { return art_; }

    /** @brief Select Fast / Enhanced / PhysicalReference and invalidate caches. */
    [[nodiscard]] Result<void> setQuality(FogQuality quality);

    /**
     * @brief Allocate density + MAC over world bounds at the quality froxel-ish resolution.
     * Simulation resolution is independent of froxel display resolution.
     */
    [[nodiscard]] Result<void> configureDomain(int simWidth, int simHeight, int simDepth,
                                               const FogWorldBounds& bounds, float fixedDt = 1.f / 60.f);

    /** @brief Seed a height-banded fog volume. */
    [[nodiscard]] Result<void> seedHeightFog(float baseDensity, float baseHeight, float falloff,
                                             float curlScale, std::uint32_t seed = 1u);

    /**
     * @brief Advance wind + MAC fluid by wall-clock dt (fixed substeps inside).
     */
    [[nodiscard]] Result<FogCflReport> stepSimulation(float dt);

    /**
     * @brief Refresh BeerLightCache when quality/light/density require it.
     */
    [[nodiscard]] Result<void> ensureBeerCache(const glm::vec3& lightDir, const glm::vec3& lightColor,
                                               float intensity);

    /** @brief World-space ray march through the live density field. */
    [[nodiscard]] Result<FogRayResult> marchRay(const glm::vec3& origin, const glm::vec3& direction,
                                                const FogVolumeBound& bound, const glm::vec3& lightDir,
                                                const glm::vec3& lightColor, float lightIntensity,
                                                const glm::vec3& skyAmbient, const glm::vec3& groundAmbient,
                                                float sceneDepth) const;

    /** @brief Inject + integrate the density field into a froxel AtmosphereVolume. */
    [[nodiscard]] Result<int> renderToFroxel(AtmosphereVolume& volume, const glm::mat4& invViewProj,
                                             const glm::vec3& lightDir, const glm::vec3& lightColor,
                                             float intensity);

    /**
     * @brief Add MAC density into an existing froxel grid without clearing or integrating.
     *
     * Use after `injectFroxelHeightFog` / local `FogVolume` injection so the fluid
     * is a perturbation on the display path, not a replacement for it.
     * @param volume Borrowed froxel grid; caller owns clear / integrate / upload.
     * @param invViewProj Inverse view-projection of the active camera.
     */
    [[nodiscard]] Result<int> injectToFroxel(AtmosphereVolume& volume, const glm::mat4& invViewProj);

    /**
     * @brief Additive MAC inject into a Volumetric that already holds height fog / volumes.
     * Does not resize, clear, or integrate. Caller still owns uploadFroxel / applyFroxel.
     * @param volumetric Borrowed for the call; not retained.
     * @ownership Borrowed Volumetric; FogSystem does not take ownership.
     * @lifetime Valid only for this call; do not retain across frames via this argument.
     * @thread Simulation / render thread that owns both objects.
     * @reentrancy Not reentrant; invokes no script callbacks.
     */
    [[nodiscard]] Result<int> injectToVolumetric(Volumetric* volumetric);

    /**
     * @brief Map the live density field into a Volumetric froxel grid and integrate lighting.
     * Resizes the atlas to the quality budget and replaces prior media. Prefer
     * injectToVolumetric when the caller already injected frustum height fog.
     * Caller still owns uploadFroxel / applyFroxel. Uses the Volumetric camera matrix.
     * @param volumetric Borrowed for the call; not retained.
     * @ownership Borrowed Volumetric; FogSystem does not take ownership.
     * @lifetime Valid only for this call; do not retain across frames via this argument.
     * @thread Simulation / render thread that owns both objects.
     * @reentrancy Not reentrant; invokes no script callbacks.
     */
    [[nodiscard]] Result<int> syncToVolumetric(Volumetric* volumetric, const glm::vec3& lightDir,
                                               const glm::vec3& lightColor, float intensity);

    /** @brief Analytic beam integration using the active quality segment count. */
    [[nodiscard]] Result<FogRayResult> integrateBeam(const AnalyticBeam& beam, const glm::vec3& origin,
                                                     const glm::vec3& direction, float sceneDepth,
                                                     float mediumDensity) const;

    /** @brief Generate procedural dust for the simulation bounds. */
    [[nodiscard]] Result<std::vector<DustParticle>> generateDust(int count, float turbulenceMeters,
                                                                 std::uint32_t seed = 7u) const;

    /** @brief Apply ContinuousArt to a physical ray result. */
    [[nodiscard]] ContinuousArtOutput stylize(const FogRayResult& physical, float viewZ,
                                              float neighborOpacity = 0.f) const noexcept;

private:
    FogQuality quality_ = FogQuality::Enhanced;
    float simTime_ = 0.f;
    std::uint64_t frameIndex_ = 0;
    FogDensityField density_;
    FogProfile profile_;
    SceneWind wind_;
    MacFluidGrid fluid_;
    FogInteractor interactor_;
    BeerLightCache beerCache_;
    FogRayMarch rayMarch_;
    FogFroxelBridge froxelBridge_;
    AnalyticalVolLight volLight_;
    ProceduralDust dust_;
    ContinuousArt art_;
};

}  // namespace eve::graphics::fog
