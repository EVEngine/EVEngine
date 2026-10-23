#pragma once

#include "procgen/algorithms/CaveHydrology.h"

#include <cstdint>
#include <vector>

namespace eve::procgen {

class MeshBuild;

/** @brief CaveWetnessRefinement public API. */
struct CaveWetnessRefinement {
    int boundaryTriangles = 0;
    int addedTriangles    = 0;
};

/** @brief Cave wetness field. */
float caveWetnessField(CaveHydrologyVec3 point, const std::vector<CaveHydrologyPoint>& drainageSpine,
                       float fallbackRadius, uint32_t seed);

/** @brief Refine cave wetness boundary. */
CaveWetnessRefinement refineCaveWetnessBoundary(MeshBuild& mesh, const std::vector<CaveHydrologyPoint>& drainageSpine,
                                                float fallbackRadius, uint32_t seed, bool splitBoundary);

}  // namespace eve::procgen
