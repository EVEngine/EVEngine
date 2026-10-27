#pragma once

/**
 * @file ProcgenRecipes.h
 * @brief Recipe/algorithm schema copies kept off the high-fanout Procgen facade.
 *
 * `RecipeDescriptor` / `ParamSchema` are editor and binding surfaces. Runtime
 * call sites that only need recipe id lists and defaults stay on `Procgen.h`.
 */

#include "procgen/ParamSchema.h"
#include "procgen/Procgen.h"

#include <string>

namespace eve::procgen {

/**
 * @brief Owning schema snapshots for algorithm and recipe families.
 * @remarks Does not require friendship; registries are process-global. Grouped
 *          here so `Procgen.h` includers do not pull `ParamSchema.h`.
 */
struct EVENGINE_API_DOMAINS ProcgenRecipeOps {
    /** @brief Returns an owning copy of an algorithm schema or a diagnostic. */
    [[nodiscard]] static eve::Result<RecipeDescriptor> getAlgorithmSchema(const Procgen&     module,
                                                                          const std::string& algorithmId);
    /** @brief Returns an owning copy of a texture schema or a diagnostic. */
    [[nodiscard]] static eve::Result<RecipeDescriptor> getTextureRecipeSchema(const Procgen&     module,
                                                                              const std::string& recipeId);
    /** @brief Returns an owning copy of a PBR schema or a diagnostic. */
    [[nodiscard]] static eve::Result<RecipeDescriptor> getPbrRecipeSchema(const Procgen&     module,
                                                                          const std::string& recipeId);
    /** @brief Returns an owning copy of a mesh schema or a diagnostic. */
    [[nodiscard]] static eve::Result<RecipeDescriptor> getMeshRecipeSchema(const Procgen&     module,
                                                                           const std::string& recipeId);
};

}  // namespace eve::procgen
