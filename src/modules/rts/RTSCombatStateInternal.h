#pragma once
#include "common/Result.h"
namespace eve::rts {
struct RTSCombatSnapshot;
namespace combat_state_internal {
// One canonical state validation shared by native restore and persistence admission.
[[nodiscard]] Result<void> validate(const RTSCombatSnapshot& state);
}  // namespace combat_state_internal
}  // namespace eve::rts
