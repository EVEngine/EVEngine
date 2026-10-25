#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

class MeshRecipeRegistry;

/**
 * @brief Procedural tileable cable / chain / rope meshes along +X.
 *
 * Recipes:
 *   - `mesh.cable` — multi-strand twisted steel cable (helical tubes)
 *   - `mesh.chain` — interlocking iron links (alternating oval rings)
 *   - `mesh.rope`  — three-strand twisted hemp rope
 *
 * Each recipe builds a unit that tiles along X when `segments` repeats it.
 * Shared knobs: `segments`, `segLength`, `radius`, `thickness`, `scale`,
 * `uvRepeat`, plus kind-specific strand / twist / link parameters.
 */
[[nodiscard]] EVENGINE_API_DOMAINS bool generateCableChainRope(const std::string& kind, const Params& params,
                                                               MeshBuild& out, std::string& error);

/** @brief Register `mesh.cable` / `mesh.chain` / `mesh.rope` mesh recipes. */
EVENGINE_API_DOMAINS void registerCableChainRopeRecipes(MeshRecipeRegistry& registry);

}  // namespace eve::procgen
