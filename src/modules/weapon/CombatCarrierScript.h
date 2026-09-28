#pragma once
#include "common/Export.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::weapon {

/**
 * @brief Expose CombatCarrierRuntime class bindings under the shared eve table.
 *
 * Adds `CombatCarrierRuntime` owned instances created via
 * `exposeCombatCarrierModuleMethods` on `eve.Weapon`, plus recipe/fragment
 * compile helpers usable from EveScript tables or JSON.
 */
EVENGINE_API_WORLD void exposeCombatCarrierBindings(ssq::Table& table);

/**
 * @brief Register Weapon module methods that create owned carrier runtimes.
 * @param cls Script class produced by `table.addClass("Weapon", ...)`.
 */
EVENGINE_API_WORLD void exposeCombatCarrierModuleMethods(ssq::Class& cls);

}  // namespace eve::weapon
