#pragma once
#include "common/Export.h"


/** @file Combat.h @brief Script-facing combat composition module. */

#include "common/Module.h"

namespace eve::combat {

/**
 * @brief Factory module for script-owned deterministic combat runtimes.
 *
 * Each runtime uniquely owns its fighter state while delegating movement and
 * damage rules to CombatLocomotionRuntime and DamageRuntime. The module itself
 * stores no arena state.
 */
class EVENGINE_API_BACKENDS Combat final : public Module {
public:
    Module_REG(Combat);
    Combat()           = default;
    ~Combat() override = default;
};

}  // namespace eve::combat
