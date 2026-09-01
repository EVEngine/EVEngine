#pragma once

/** @file StandardAbilities.h @brief Registerable standard action-combat ability archetypes. */

#include "action/AbilitySystem.h"

#include <vector>

namespace eve::combat {

/**
 * @brief Build twenty validated, registerable standard combat ability definitions.
 *
 * The catalogue supplies identities, timings, cooldown/instancing/group policy
 * and adapter metadata. Projects attach animation, movement, damage and other
 * presentation/gameplay behavior through Action timeline events and executors.
 * @return Owning definitions, or a structured internal-definition failure.
 */
[[nodiscard]] Result<std::vector<action::AbilityDefinition>> standardCombatAbilities();

}  // namespace eve::combat
