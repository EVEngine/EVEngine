#pragma once

#include "virtualgeometry/VirtualGeometryAsset.h"

#include <cstdint>
#include <vector>

namespace eve::virtualgeometry {

/**
 * CPU reference implementation of the GPU cluster-DAG LOD selection
 * (mirrors shaders/vg_cull.comp).
 *
 * A cluster is selected iff it is the *coarsest* ancestor whose screen-space
 * geometric error is within `errorPx` (or a leaf when no ancestor is fine
 * enough). Combined with `screenP > errorPx`, this emits exactly one cluster per
 * DAG branch (no overlaps, no gaps), so as the camera moves the visible LOD
 * transitions continuously from fine (near) to coarse (far).
 *
 * screenError(cluster) = errorR * projScale / dist
 */
int selectClusters(const VirtualGeometryAsset &asset, float dist, float projScale, float errorPx,
                   std::vector<std::uint32_t> &outSelected);

/** Per-LOD-level counts of the selected clusters (size = maxLod+1). */
void lodHistogram(const VirtualGeometryAsset &asset, const std::vector<std::uint32_t> &selected,
                  std::vector<int> &perLevelCount);

}  // namespace eve::virtualgeometry
