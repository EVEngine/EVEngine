#pragma once
#include "common/Export.h"

/**
 * @file CarrierRecipeCodec.h
 * @brief Decode/encode CombatCarrier recipes from canonical eve::Value trees.
 *
 * Script and tools share this codec so JSON and Squirrel tables produce the same
 * CarrierRecipe. Two input shapes are accepted:
 *
 * 1. Full form: motion/triggers/impacts arrays with explicit kinds.
 * 2. Preset form: convenience fields (damage, pierce, bounce, fuse, homing, …)
 *    that expand into a validated default composition.
 */

#include "common/Result.h"
#include "common/Value.h"
#include "weapon/CombatCarrier.h"

namespace eve::weapon {

/**
 * @brief Decode one CarrierRecipe from an owning Value object.
 * @param value Object tree; arrays and scalars must be finite where numeric.
 * @return Owning validated recipe, or a path-aware diagnostic.
 * @ownership Returned recipe owns all fields; input Value is not retained.
 * @thread Owner thread only; no clock or RNG.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<CarrierRecipe> decodeCarrierRecipe(const Value& value);

/**
 * @brief Encode one CarrierRecipe into an owning Value object (full form).
 * @param recipe Validated recipe snapshot.
 * @return Owning object tree suitable for JSON / script projection.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<Value> encodeCarrierRecipe(const CarrierRecipe& recipe);

}  // namespace eve::weapon
