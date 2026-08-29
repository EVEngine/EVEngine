#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

class MeshRecipeRegistry;

/** @brief Register the `mesh.lsystem` grammar-based plant recipe. @param registry Registry to populate. */
void registerLSystemRecipes(MeshRecipeRegistry& registry);

/** @brief Build a grammar-based tree/plant mesh from Params. */
bool generateLSystemMesh(const Params& params, MeshBuild& out, std::string& error);

}  // namespace eve::procgen