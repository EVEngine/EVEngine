#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

class MeshRecipeRegistry;

/**
 * Procedural linear, tileable structures (fences, walls, bridges, the Great Wall,
 * hedges, cheval de frise, ...). Each recipe builds one tileable unit segment
 * from axis-aligned boxes and oriented beams, then repeats it `segments` times
 * along the X axis so units join seamlessly end-to-end.
 *
 * Shared params:
 *   - segments:   int,   repeat count along X (default 6)
 *   - segLength:  float, length of one unit in world units (default 1.0)
 *   - height:     float, overall height override (per-kind default)
 *   - depth:      float, thickness along Z override (per-kind default)
 *   - thickness:  float, plank / rail / beam thickness override (per-kind default)
 *   - scale:      float, uniform world scale (default 1.0)
 *   - uvRepeat:   float, texture repeats per world unit so grain/brick tiling
 *                 stays continuous across unit seams (default 2.0)
 */
bool generateLinearStructure(const std::string &kind, const Params &params, MeshBuild &out,
                             std::string &error);

/** Register all built-in linear structure mesh recipes into a registry. */
void registerLinearStructureRecipes(MeshRecipeRegistry &registry);

}  // namespace eve::procgen
