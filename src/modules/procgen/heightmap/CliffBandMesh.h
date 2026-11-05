#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>

namespace eve::procgen {
class Heightmap;
class MeshBuild;

/**
 * @brief Settings for extracting steep heightfield edges into vertical cliff wall strips.
 *
 * Edge slope uses the same metric as deriveTerrainSlope (`1 - run/hypot(run, relief)`).
 * When cliffFitness is supplied it must match the heightmap size; otherwise slope is
 * derived inline and used as a soft gate alongside geometric edge steepness.
 */
struct EVENGINE_API_DOMAINS CliffBandMeshSettings {
    float         originX         = 0.f;
    float         originZ         = 0.f;
    float         spacingX        = 1.f;
    float         spacingZ        = 1.f;
    float         heightScale     = 1.f;
    float         slopeMin        = 0.42f;
    float         slopeMax        = 1.f;
    float         minRelief       = 0.5f;  ///< Skip edges whose world-space |Δh| is below this.
    int           subdivisions    = 2;     ///< Extra splits along wall U/V in [0, 4].
    float         strataAmplitude = 0.28f;
    float         strataScale     = 0.55f;  ///< World XZ frequency for rock noise.
    float         strataVertical  = 2.4f;   ///< World Y frequency (horizontal bedding).
    float         ledgeHeight     = 0.f;    ///< >0 quantizes world Y into ledges before displace.
    float         outwardBias     = 0.85f;  ///< Prefer horizontal outward rock displace.
    std::uint32_t seed            = 1;
    int           maxTriangles    = 500000;
};

/**
 * @brief Emit vertical wall quads along steep grid edges, subdivide, and strata-displace.
 * @param out Cleared then filled; published only after a complete candidate succeeds.
 * @param heights Finite heightmap with both dimensions at least 2.
 * @param settings Finite positive spacing/scale, slope range, subdivision and triangle budget.
 * @param cliffFitness Optional matching fitness raster in [0,1]; null derives slope inline.
 * @return Emitted triangle count, or InvalidArgument / Failed (budget) without mutating out.
 * @cost Linear in grid edges times subdivision^2; keep maxTriangles bounded for gameplay meshes.
 * @thread Synchronous exclusive out; immutable inputs; deterministic for a fixed seed.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> generateCliffBandMesh(MeshBuild& out, const Heightmap& heights,
                                                                     const CliffBandMeshSettings& settings,
                                                                     const Heightmap* cliffFitness = nullptr);

/**
 * @brief Build a cliff fitness mask from height slopes (1-normal.y) and a smoothstep range.
 * @param target Matching-size destination; may alias heights.
 * @return Changed sample count or InvalidArgument.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> generateCliffFitnessMask(Heightmap& target, const Heightmap& heights,
                                                                        float spacingX, float spacingZ,
                                                                        float heightScale, float slopeMin,
                                                                        float slopeMax);

}  // namespace eve::procgen
