#include "combat/Damage.h"

#include "tags/GameplayTag.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::combat {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<void> invalid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

bool finiteNonNegative(double value) { return std::isfinite(value) && value >= 0.0; }

Result<void> validateAmounts(const DamageAmounts& amounts) {
    if (!finiteNonNegative(amounts.healthDamage)) return invalid("Resolved health damage is invalid", "healthDamage");
    if (!finiteNonNegative(amounts.poiseDamage)) return invalid("Resolved poise damage is invalid", "poiseDamage");
    if (!finiteNonNegative(amounts.knockbackScale))
        return invalid("Resolved knockback scale is invalid", "knockbackScale");
    return Result<void>::success();
}

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

Result<DamageAmounts> DamageRuntime::preview(const CombatState& target, const DamageRequest& request) const {
    auto stateValid = target.validate();
    if (!stateValid) return Result<DamageAmounts>::failure(stateValid.status());
    auto requestValid = request.validate();
    if (!requestValid) return Result<DamageAmounts>::failure(requestValid.status());
    if (target.subject != request.target)
        return invalid<DamageAmounts>("Damage request target does not match combat state", "target");
    DamageAmounts amounts{request.healthDamage, request.poiseDamage, 1.0};
    if (rule_) {
        auto evaluated = rule_->evaluate(request, target);
        if (!evaluated) return Result<DamageAmounts>::failure(evaluated.status());
        amounts = std::move(evaluated).takeValue();
    }
    auto valid = validateAmounts(amounts);
    if (!valid) return Result<DamageAmounts>::failure(valid.status());
    return Result<DamageAmounts>::success(amounts);
}

Result<DamageOutcome> DamageRuntime::apply(CombatState& target, const DamageRequest& request) const {
    auto policyValid = policy_.validate();
    if (!policyValid) return Result<DamageOutcome>::failure(policyValid.status());
    auto previewed = preview(target, request);
    if (!previewed) return Result<DamageOutcome>::failure(previewed.status());
    DamageAmounts amounts = std::move(previewed).takeValue();
    DamageRuleSource source = DamageRuleSource::Default;
    if (rule_) source = DamageRuleSource::Provider;

    CombatState candidate      = target;
    candidate.health           = std::max(0.0, candidate.health - amounts.healthDamage);
    candidate.poise            = std::max(0.0, candidate.poise - amounts.poiseDamage);
    const double appliedHealth = target.health - candidate.health;
    const double appliedPoise  = target.poise - candidate.poise;

    HitReaction reaction = HitReaction::None;
    if (candidate.health == 0.0 && target.health > 0.0) {
        reaction = HitReaction::Death;
    } else if (candidate.poise == 0.0 && appliedPoise > 0.0) {
        reaction = HitReaction::Knockdown;
    } else if (target.maxPoise > 0.0 && appliedPoise >= target.maxPoise * policy_.staggerPoiseFraction) {
        reaction = HitReaction::Stagger;
    } else if (appliedHealth > 0.0 && appliedHealth >= policy_.flinchDamageThreshold) {
        reaction = HitReaction::Flinch;
    }

    DamageOutcome outcome{request.source,
                          request.target,
                          target.health,
                          candidate.health,
                          target.poise,
                          candidate.poise,
                          appliedHealth,
                          appliedPoise,
                          {request.knockback.x * amounts.knockbackScale, request.knockback.y * amounts.knockbackScale,
                           request.knockback.z * amounts.knockbackScale},
                          reaction,
                          source};
    target = candidate;
    return Result<DamageOutcome>::success(std::move(outcome), Status::success(StatusCode::Applied));
}

Result<double> DamageRuntime::recoverPoise(CombatState& target, double amount) const {
    auto valid = target.validate();
    if (!valid) return Result<double>::failure(valid.status());
    if (!finiteNonNegative(amount)) return invalid<double>("Poise recovery amount is invalid", "amount");
    const double recovered = std::min(target.maxPoise, target.poise + amount);
    const bool   changed   = recovered != target.poise;
    target.poise           = recovered;
    return Result<double>::success(recovered, Status::success(changed ? StatusCode::Applied : StatusCode::NoOp));
}

}  // namespace eve::combat
