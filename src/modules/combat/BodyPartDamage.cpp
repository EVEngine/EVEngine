#include "combat/BodyPartDamage.h"

#include <cmath>
#include <utility>

namespace eve::combat {
  // namespace

Result<void> BodyPartDamageRule::setMultiplier(std::string bodyPart, double multiplier) {
    if (bodyPart.empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("body part is empty"), std::move("bodyPart")));
    if (!std::isfinite(multiplier) || multiplier < 0.0) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("body-part multiplier is invalid"), std::move("multiplier")));
    multipliers_[std::move(bodyPart)] = multiplier;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

double BodyPartDamageRule::multiplier(std::string_view bodyPart) const noexcept {
    const auto found = multipliers_.find(std::string(bodyPart));
    return found == multipliers_.end() ? 1.0 : found->second;
}

Result<DamageAmounts> BodyPartDamageRule::evaluate(const DamageRequest& request, const CombatState& target) const {
    (void)target;
    DamageAmounts amounts;
    amounts.healthDamage = request.healthDamage * request.incomingDamageMultiplier;
    amounts.poiseDamage = request.poiseDamage;
    amounts.knockbackScale = 1.0;
    if (!std::isfinite(amounts.healthDamage) || amounts.healthDamage < 0.0 ||
        !std::isfinite(amounts.poiseDamage) || amounts.poiseDamage < 0.0)
        return Result<DamageAmounts>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "body-part damage amounts are invalid", "amounts"));
    return Result<DamageAmounts>::success(amounts);
}

Result<DamageAmounts> BodyPartDamageRule::evaluateForPart(const DamageRequest& request, const CombatState& target,
                                                          std::string_view bodyPart) const {
    DamageRequest scaled = request;
    scaled.incomingDamageMultiplier = request.incomingDamageMultiplier * multiplier(bodyPart);
    return evaluate(scaled, target);
}

}  // namespace eve::combat
