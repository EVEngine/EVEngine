#pragma once

#include "procgen/road/RoadBake.h"
#include "procgen/spline/SplinePath.h"

namespace eve::procgen::road::detail {

[[nodiscard]] Result<SplinePath> edgeSplineForBake(const RoadEdge& edge);
[[nodiscard]] float junctionSocketDistanceForBake(const RoadNetwork& network, const RoadNode& node,
                                                  const RoadEdge& edge, float pathLength);
[[nodiscard]] Result<void> bakeTrafficControlPoints(
    PointSet& placements, const RoadNetwork& network, const RoadBakeOptions& options);

}  // namespace eve::procgen::road::detail
