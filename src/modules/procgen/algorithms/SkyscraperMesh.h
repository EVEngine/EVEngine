#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

/**
 * @brief Build a deterministic procedural skyscraper. Registered as the `mesh.skyscraper` recipe.
 *
 * The tower is a stack of setback tiers, each a solid box whose footprint shrinks as it rises.
 * Every facade is a subdivided window grid: each cell emits a small window quad raised slightly
 * in front of the wall plane (a thin `windowDepth` lip), so openings read under directional
 * lighting without coplanar z-fighting. An optional spire / antenna cap finishes the roof.
 *
 * Parameters (all optional):
 *   seed          reproducible RNG (default 1)
 *   baseWidth     footprint extent along X  (default 10)
 *   baseDepth     footprint extent along Z  (default 10)
 *   tiers         number of setback levels  (default 5, range [1, 24])
 *   tierHeight    height of one tier        (default 6)
 *   setback       per-tier footprint shrink fraction (default 0.08, range [0, 0.6))
 *   windowCols    window columns per facade (default 6, range [1, 24])
 *   windowRows    window rows per tier      (default 4, range [1, 24])
 *   windowDepth   window lip height above the wall (default 0.04)
 *   spireHeight   antenna / spire height    (default 0, disabled)
 *
 * UV convention: wall quads sample the lower-left texel region (0.25, 0.5), window quads the
 * upper-right texel region (0.75, 0.5), so a tiny 2x2 atlas can tint openings bright vs walls
 * dark, or a full facade texture can be used with the same mapping per-cell.
 */
bool generateSkyscraperMesh(const Params &params, MeshBuild &out, std::string &error);

}  // namespace eve::procgen
