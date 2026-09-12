#pragma once

namespace eve::procgen {

class MeshRecipeRegistry;

/** @brief Register the extended-plane and customizable hex-grid mesh recipes. */
void registerMeshDeformationGeometryRecipes(MeshRecipeRegistry& registry);

}  // namespace eve::procgen
