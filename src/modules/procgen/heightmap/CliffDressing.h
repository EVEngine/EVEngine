#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include "procgen/heightmap/TerrainObjectPlacement.h"

#include <cstdint>
#include <string>

namespace eve::procgen {

/**
 * @brief Fill placement settings for scattering rock props on a cliff fitness mask.
 * @param settings Destination cleared of instances then configured with one rock child.
 * @param originX World origin X of the fitness/height domain.
 * @param originZ World origin Z of the fitness/height domain.
 * @param width World width of the domain.
 * @param depth World depth of the domain.
 * @param heightScale Multiplier applied to normalized height samples when placing Y.
 * @param seed Deterministic placement seed.
 * @param spacing Candidate grid spacing in world units (larger = fewer rocks).
 * @param minimumFitness Cliff fitness threshold in [0,1].
 * @param asset Asset id recorded on each instance (default mesh.rock cliff shape key).
 * @return Instance entry count (1) or InvalidArgument for non-finite / nonpositive domain.
 * @note Does not spawn; call exportTerrainObjectPoints with a fitness + heights raster next.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> configureCliffRockScatterDefaults(
    TerrainObjectPlacementSettings& settings, float originX, float originZ, float width, float depth, float heightScale,
    std::uint32_t seed, float spacing = 10.f, float minimumFitness = 0.55f,
    const std::string& asset = "mesh.rock:cliff");

}  // namespace eve::procgen
