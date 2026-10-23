#pragma once
#include "common/Export.h"

/**
 * @file SpellFragmentCompiler.h
 * @brief Compile Noita-style spell/modifier fragment sequences into carrier recipes.
 *
 * Fragments are scanned left-to-right. Modifier fragments accumulate into a
 * stack; a projectile fragment consumes the stack and emits one CarrierRecipe
 * (plus optional Fan/Ring volley). This is a pure data transform: no runtime
 * pool mutation, no clock, no RNG.
 */

#include "common/Result.h"
#include "common/Value.h"
#include "weapon/CombatCarrier.h"

#include <optional>
#include <vector>

namespace eve::weapon {

/** @brief One compiled cast unit ready to register and spawn. */
struct SpellCastPlan {
    CarrierRecipe                  recipe;
    std::optional<CarrierVolleySpec> volley;
    std::vector<CarrierRecipe>     dependentRecipes; /**< Child recipes referenced by SpawnChild. */
};

/**
 * @brief Compile a fragment array Value into one cast plan.
 * @param fragments Array of fragment objects (see docs in CombatCarrierScript).
 * @return Owning plan with a validated primary recipe, or a path-aware failure.
 * @ownership Returned plan owns all recipes; input is not retained.
 * @thread Owner thread only.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<SpellCastPlan> compileSpellFragments(const Value& fragments);

/**
 * @brief Compile a JSON array string into one cast plan.
 * @param json UTF-8 JSON array of fragment objects.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<SpellCastPlan> compileSpellFragmentsJson(const std::string& json);

}  // namespace eve::weapon
