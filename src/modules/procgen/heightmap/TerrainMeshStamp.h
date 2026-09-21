#pragma once

#include "common/Result.h"

#include <cstdint>
#include <vector>

namespace eve::procgen {
class MeshBuild;
class Heightmap;

/**
 * @brief Owning CPU mesh-to-heightfield builder; topmost Y wins regardless of winding.
 * @ownership Owns copied positions and indices; never retains source/output references.
 * @thread Serialize mutations; bake may run concurrently with other read-only bakes
 * on distinct outputs. No callbacks, locks, RNG, renderer or ECS dependencies.
 * @note Apply model transforms before setSource. Undercuts, UVs and materials are discarded.
 * Repeated bakes are deterministic on one toolchain; compare platforms with float tolerance.
 */
class TerrainMeshStampBuilder {
public:
    /**
     * @brief Validate and copy a CPU triangle mesh; failure preserves the prior source.
     * @param mesh Borrowed immutable mesh in the bake coordinate system, Y up.
     * @return Copied triangle count, or InvalidArgument for malformed/nonfinite/oversized data.
     * @cost Linear in vertices and indices, with owning copies; reuse the builder across bakes.
     * @throws std::bad_alloc Allocation failure leaves the prior source unchanged.
     */
    [[nodiscard]] Result<int> setSource(const MeshBuild& mesh);

    /**
     * @brief Bake the highest surface into two preallocated matching rasters, atomically.
     * @param heights Exclusive output, at least 2x2; no-hit samples become zero.
     * @param coverage Exclusive distinct output; zero outside, one inside unless feathered.
     * @param minX First column's source-space X.
     * @param minZ First row's source-space Z.
     * @param width Positive finite X span from first to last column.
     * @param depth Positive finite Z span from first to last row.
     * @param feather Nonnegative source-space distance from missing samples or raster boundary;
     * uses grid Manhattan distance and cubic smoothstep. Zero disables feathering.
     * @return Covered sample count before feathering; zero means no hits. InvalidArgument
     * leaves both outputs unchanged, including when the work budget is exceeded.
     * @cost Expensive: sum of triangle projected bounding-box sample counts plus raster size;
     * bounded to 128M sample tests and 4M output samples. Bake once per mesh/resolution,
     * then reuse heights/coverage for stamping. Vertical/degenerate triangles are ignored.
     * @throws std::bad_alloc Allocation failure leaves both outputs unchanged.
     */
    [[nodiscard]] Result<int> bake(Heightmap& heights, Heightmap& coverage, double minX, double minZ, double width,
                                   double depth, double feather) const;

private:
    std::vector<float>         positions_;
    std::vector<std::uint32_t> indices_;
};
}  // namespace eve::procgen
