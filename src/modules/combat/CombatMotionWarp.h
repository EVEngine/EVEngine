#pragma once
#include "common/Export.h"

/** @file CombatMotionWarp.h @brief Bounded attack translation/facing correction toward a lock target. */

#include "combat/CombatCharacter.h"
#include "common/Result.h"

namespace eve::combat {

/** @brief Input for one deterministic motion-warp solve. */
struct CombatWarpRequest {
    CombatVector3 attacker;
    CombatVector3 target;
    CombatVector3 attackerFacing{1.0, 0.0, 0.0};
    double        desiredDistance = 1.2;
    double        maxTranslation  = 0.15;
    double        remainingBudget = 0.6;
    bool          horizontal      = true;
    bool          facing          = true;

    /** @brief Reject non-finite poses, distances or budgets. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning warp delta that callers add to root motion or position. */
struct CombatWarpResult {
    CombatVector3 translation;
    CombatVector3 facing{1.0, 0.0, 0.0};
    double        budgetConsumed = 0.0;
    bool          budgetExceeded = false;
};

/**
 * @brief Pull the attacker toward a standoff distance without exceeding per-tick or remaining budgets.
 *
 * Exceeding remaining budget returns Conflict without a translation; callers must not snap.
 */
class EVENGINE_API_BACKENDS CombatMotionWarp {
public:
    /** @brief Solve one warp step from owning request data. */
    [[nodiscard]] static Result<CombatWarpResult> solve(const CombatWarpRequest& request);
};

}  // namespace eve::combat
