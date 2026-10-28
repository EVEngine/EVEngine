#pragma once

namespace eve::procgen {

class MeshRecipeRegistry;

/**
 * @brief Register the deterministic three-dimensional limestone cave recipe.
 *
 * Registration is idempotent through MeshRecipeRegistry::registerBuiltins().
 * The callback is synchronous, owns no retained state, and derives all
 * randomness from the recipe seed.
 * @param registry Process-wide registry receiving `mesh.cave`.
 */
void registerCaveMeshRecipe(MeshRecipeRegistry& registry);

}  // namespace eve::procgen
