#pragma once
#include "common/Export.h"

/** @file BodyPartDamage.h @brief IDamageRule that scales damage by body-part tags. */

#include "combat/Damage.h"

#include <map>
#include <string>

namespace eve::combat {

/**
 * @brief Optional damage rule that multiplies health damage by a body-part key.
 *
 * The damage request's damageType suffix after the final '.' may encode a part
 * override, but the preferred path sets `incomingDamageMultiplier` before apply
 * or uses `evaluateForPart`. Missing parts use multiplier 1.0.
 */
class EVENGINE_API_BACKENDS BodyPartDamageRule final : public IDamageRule {
public:
    /** @brief Register or replace one finite non-negative body-part multiplier. */
    [[nodiscard]] Result<void> setMultiplier(std::string bodyPart, double multiplier);
    /** @brief Return the multiplier for a part, or 1.0 when unregistered. */
    [[nodiscard]] double multiplier(std::string_view bodyPart) const noexcept;
    /** @copydoc IDamageRule::evaluate */
    /** @brief Evaluate. */
    [[nodiscard]] Result<DamageAmounts> evaluate(const DamageRequest& request,
                                                 const CombatState&   target) const override;
    /**
     * @brief Evaluate with an explicit body-part key.
     * @remarks Multiplies request.healthDamage by the part multiplier and leaves poise unchanged.
     */
    [[nodiscard]] Result<DamageAmounts> evaluateForPart(const DamageRequest& request, const CombatState& target,
                                                        std::string_view bodyPart) const;

private:
    std::map<std::string, double, std::less<>> multipliers_;
};

}  // namespace eve::combat
