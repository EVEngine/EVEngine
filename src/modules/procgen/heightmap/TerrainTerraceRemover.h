#pragma once

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;

/** @brief Portable controls for Pcg HeightmapTerraceRemover's analysis and processing pipeline. */
struct TerrainTerraceRemovalSettings {
    float perlinScale = 0.12F;
    float perlinStrength = 0.0035F;
    float slopeTerraceThreshold = 0.00119F;
    float flatThreshold = 0.00091F;
    float verticalGradientThreshold = 0.00056F;
    float minimumTerraceThreshold = 0.000275F;
    float maximumTerraceThreshold = 0.00053F;
    bool excludeRed = true;
    bool excludeBlack = false;
    bool terrainWorkflow = true;
    int noiseSeed = 1;
};

/** @brief Numeric values written by analyzeTerrainTerraces, matching the source mask colors. */
enum class TerrainTerraceClass { Black = 0, Red = 1, Green = 2, Blue = 3 };

/**
 * @brief Classify a heightmap into Pcg's black/red/green/blue important-detail categories.
 * @param target Exclusively borrowed matching output; aliases with source are rejected.
 * @param source Borrowed finite height samples, never retained.
 * @param settings Finite nonnegative thresholds with minimum not greater than maximum.
 * @return Changed category samples or InvalidArgument; failure preserves target.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous caller-owned access; no callbacks, implicit time, or retained references.
 */
[[nodiscard]] Result<int> analyzeTerrainTerraces(Heightmap& target, const Heightmap& source,
                                                  const TerrainTerraceRemovalSettings& settings);

/**
 * @brief Apply Pcg's adaptive smoothing, gradient noise, 3x3 filter, and fixed 5x5 Gaussian blur.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite height samples.
 * @param classification Borrowed matching TerrainTerraceClass raster, distinct from target.
 * @param settings Valid controls; terrainWorkflow applies the source's 0.00042/0.0035 noise ratio.
 * @return Changed samples or InvalidArgument; all validation and computation precede atomic publication.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous caller-owned access with a named deterministic noise seed and no callbacks.
 */
[[nodiscard]] Result<int> removeTerrainTerraces(Heightmap& target, const Heightmap& source,
                                                 const Heightmap& classification,
                                                 const TerrainTerraceRemovalSettings& settings);
}
