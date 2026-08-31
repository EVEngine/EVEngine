#pragma once

#include "procgen/algorithms/CaveHydrology.h"

#include <cstdint>
#include <vector>

namespace eve::procgen {

class MeshBuild;

struct CaveWetnessRefinement {
    int boundaryTriangles = 0;
    int addedTriangles    = 0;
};

float caveWetnessField(CaveHydrologyVec3 point, const std::vector<CaveHydrologyPoint>& drainageSpine,
                       float fallbackRadius, uint32_t seed);

CaveWetnessRefinement refineCaveWetnessBoundary(MeshBuild& mesh, const std::vector<CaveHydrologyPoint>& drainageSpine,
                                                float fallbackRadius, uint32_t seed, bool splitBoundary);

}  // namespace eve::procgen
