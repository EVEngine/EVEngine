#include "combat/Damage.h"

#include "tags/GameplayTag.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::combat {
namespace {

Result<void> invalid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

bool finiteNonNegative(double value) { return std::isfinite(value) && value >= 0.0; }


}  // namespace

Result<void> CombatState::validate() const {
    if (!subject.isValid()) return invalid("Combat subject is invalid", "subject");
    if (!finiteNonNegative(maxHealth) || !finiteNonNegative(health) || health > maxHealth)
        return invalid("Combat health is outside [0,maxHealth]", "health");
    if (!finiteNonNegative(maxPoise) || !finiteNonNegative(poise) || poise > maxPoise)
        return invalid("Combat poise is outside [0,maxPoise]", "poise");
    return Result<void>::success();
}

Result<void> DamageRequest::validate() const {
    if (!target.isValid()) return invalid("Damage target is invalid", "target");
    if (!tags::isValidGameplayTagName(damageType)) return invalid("Damage type must be a gameplay tag", "damageType");
    if (!finiteNonNegative(healthDamage)) return invalid("Health damage is invalid", "healthDamage");
    if (!finiteNonNegative(poiseDamage)) return invalid("Poise damage is invalid", "poiseDamage");
    if (!std::isfinite(knockback.x) || !std::isfinite(knockback.y) || !std::isfinite(knockback.z))
        return invalid("Knockback impulse must be finite", "knockback");
    return Result<void>::success();
}

Result<void> HitReactionPolicy::validate() const {
    if (!finiteNonNegative(flinchDamageThreshold))
        return invalid("Flinch threshold is invalid", "flinchDamageThreshold");
    if (!std::isfinite(staggerPoiseFraction) || staggerPoiseFraction < 0.0 || staggerPoiseFraction > 1.0)
        return invalid("Stagger poise fraction must be in [0,1]", "staggerPoiseFraction");
    return Result<void>::success();
}

Result<double> DamageRuntime::recoverPoise(CombatState& target, double amount) const {
    auto valid = target.validate();
    if (!valid) return Result<double>::failure(valid.status());
    if (!finiteNonNegative(amount))
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Poise recovery amount is invalid", "amount"));
    const double recovered = std::min(target.maxPoise, target.poise + amount);
    const bool   changed   = recovered != target.poise;
    target.poise           = recovered;
    return Result<double>::success(recovered, Status::success(changed ? StatusCode::Applied : StatusCode::NoOp));
}

}  // namespace eve::combat
