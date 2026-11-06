#pragma once

#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadTypes.h"

namespace eve::procgen::road {

/**
 * @brief Build a procedural railway edge style (ballast bed, rails, sleepers).
 * @param tracks Parallel track count in [1,2]; each track uses standard 1.435 m gauge.
 * @return Owning style ready for `RoadNetwork::addEdge` / `setEdgeStyle`, or a validation diagnostic.
 * @note Reuses existing RoadMaterial tags: asphalt=ballast top, deck=ballast toes/sleepers, curb=rails.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<RoadStyle> makeRailwayStyle(int tracks = 1);

/**
 * @brief Rewrite every edge to a railway style while preserving bridge/tunnel intervals.
 * @param network Owning network; mutated only on full success.
 * @param tracks Parallel track count in [1,2] applied as forward lanes (backward cleared).
 * @return Success after reconnecting junction turns, or the first edge/node diagnostic.
 * @cost Linear in edge count plus quadratic turn reconnect per junction; authoring / recipe projection only.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> applyRailwayRoadType(RoadNetwork& network, int tracks = 1);

}  // namespace eve::procgen::road
