#pragma once
#include "common/Export.h"

/** @file CombatCamera.h @brief Lock-on framing math owned by combat, consumed by any camera. */

#include "combat/CombatCharacter.h"
#include "common/Result.h"

#include <optional>

namespace eve::combat {

/** @brief Framing inputs for a two-subject combat camera. */
struct CombatCameraFramingRequest {
    CombatVector3                player;
    CombatVector3                playerFacing{1.0, 0.0, 0.0};
    std::optional<CombatVector3> lockTarget;
    double                       distance   = 6.0;
    double                       height     = 2.0;
    double                       lookHeight = 1.2;

    /** @brief Reject non-finite poses or non-positive distance/height. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning eye/look-at pair; camera modules apply this without owning combat state. */
struct CombatCameraView {
    CombatVector3 eye;
    CombatVector3 lookAt;
};

/**
 * @brief Deterministic lock-on framing: look at the midpoint, stand behind the player.
 *
 * Without a lock target the view falls back to follow-from-facing.
 */
class EVENGINE_API_BACKENDS CombatCameraFraming {
public:
    /** @brief Solve one view from owning request data. */
    [[nodiscard]] static Result<CombatCameraView> solve(const CombatCameraFramingRequest& request);
};

}  // namespace eve::combat
