#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>

namespace eve::procgen {
class Heightmap;

/** @brief Named mountain composition presets for bakeMountainRecipe. */
enum class MountainPreset : std::uint8_t {
    AlpineRidge  = 0,  ///< Sharp ridged peaks, moderate warp, cliff-friendly talus.
    Mesa         = 1,  ///< Broad plateaus with steeper rims after terrace-like exponent.
    CoastalCliff = 2   ///< Land toward one edge with steep coastal faces.
};

/**
 * @brief Deterministic mountain height bake: ridged/warp sampler then thermal→hydraulic→fluvial.
 *
 * Heights are written in normalized sampler units (typically [0,1] before amplitude), then
 * scaled by amplitude/base from the preset. Companion masks use the same raster size.
 */
struct EVENGINE_API_DOMAINS MountainRecipeSettings {
    MountainPreset preset        = MountainPreset::AlpineRidge;
    int            width         = 129;
    int            height        = 129;
    std::uint32_t  seed          = 1;
    float          spacingX      = 1.f;
    float          spacingZ      = 1.f;
    float          heightScale   = 1.f;  ///< World units per height sample when building slope masks.
    float          cliffSlopeMin = 0.42f;
    float          cliffSlopeMax = 1.f;
    /** @brief Optional overrides; negative means “use preset default”. */
    float frequencyOverride   = -1.f;
    float ridgeOverride       = -1.f;
    float warpOverride        = -1.f;
    float amplitudeOverride   = -1.f;
    int   thermalIterations   = -1;
    int   hydraulicIterations = -1;
    int   fluvialIterations   = -1;
};

/** @brief Optional companion rasters filled when non-null and matching destination size. */
struct EVENGINE_API_DOMAINS MountainRecipeMasks {
    Heightmap* slopeMask    = nullptr;  ///< deriveTerrainSlope output in [0,1].
    Heightmap* cliffFitness = nullptr;  ///< Smoothstep of slope into [cliffSlopeMin, cliffSlopeMax].
};

/**
 * @brief Bake a mountain preset into an exclusively borrowed heightmap.
 * @param target Destination resized to settings.width/height on success path start; failure
 *        leaves the previous contents unchanged when validation fails before mutation.
 * @param settings Finite positive dimensions (>=2), spacing, and cliff slope bounds.
 * @param masks Optional slope/cliff outputs; each must be null or match the destination size
 *        after resize (callers may pass empty maps; they are resized to match).
 * @return Changed height sample count, or InvalidArgument without partial mask writes.
 * @cost One full-grid sample plus O(iterations) erosion passes; bake offline / once per seed.
 * @thread Synchronous exclusive target; no RNG beyond the seeded sampler, no callbacks retained.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeMountainRecipe(Heightmap&                    target,
                                                                  const MountainRecipeSettings& settings,
                                                                  MountainRecipeMasks           masks = {});

}  // namespace eve::procgen
