#pragma once

#include "map/TileOrientation.h"

#include <functional>
#include <string>

namespace eve::map {

/** Neighbor connectivity for grid pathfinding (tile-index space). */
enum class PathTopology {
    Ortho4,  ///< 4-connected rectangle
    Ortho8,  ///< 8-connected rectangle (no corner cutting when sides blocked)
    Hex,     ///< 6-connected staggered / hexagonal
};

PathTopology parsePathTopology(const std::string &name, PathTopology fallback = PathTopology::Ortho4);
std::string pathTopologyName(PathTopology t);

/** Choose topology from map orientation; `preferDiagonal` upgrades Ortho4 → Ortho8. */
PathTopology topologyFromOrientation(MapOrientation orientation, bool preferDiagonal);

using NeighborFn = std::function<void(int nx, int ny, float moveCost)>;

/**
 * Enumerate walkable-candidate neighbors (bounds/walkability checked by caller via callback filter
 * or after the fact). `staggerAxisY` / `staggerOdd` only used for Hex.
 */
void forEachNeighbor(PathTopology topology, int x, int y, bool staggerAxisY, bool staggerOdd,
                     NeighborFn fn);

/** Admissible heuristic distance in the same metric as move costs (cardinal=1, diag=√2, hex=1). */
float pathHeuristic(PathTopology topology, int x0, int y0, int x1, int y1);

}  // namespace eve::map
