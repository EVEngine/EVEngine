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

Result<void> validateAmounts(const DamageAmounts& amounts) {
    if (!finiteNonNegative(amounts.healthDamage)) return invalid("Resolved health damage is invalid", "healthDamage");
    if (!finiteNonNegative(amounts.poiseDamage)) return invalid("Resolved poise damage is invalid", "poiseDamage");
    if (!finiteNonNegative(amounts.knockbackScale))
        return invalid("Resolved knockback scale is invalid", "knockbackScale");
    return Result<void>::success();
}

class DamageSettlementPolicy final : public settlement::ISettlementPolicy {
public:
    DamageSettlementPolicy(CombatState& target, const DamageRequest& request, DamageAmounts amounts,
                           const HitReactionPolicy& reactionPolicy)
        : target_(target), request_(request), reactionPolicy_(reactionPolicy), amounts_(std::move(amounts)) {}

    Result<void> validate(settlement::SettlementContext&) override {
        auto policyValid = reactionPolicy_.validate();
        if (!policyValid) return policyValid;
        return Result<void>::success();
    }

    Result<void> sourceModifiers(settlement::SettlementContext& context) override {
        return context.setMagnitude(amounts_.healthDamage);
    }
    Result<void> targetMitigation(settlement::SettlementContext& context) override {
        const double before = context.magnitude();
        const double after = before * request_.incomingDamageMultiplier;
        if (after < before) {
            auto resisted = context.addResisted(before - after);
            if (!resisted) return resisted;
        }
        return context.setMagnitude(after);
    }
    Result<void> armorShield(settlement::SettlementContext& context) override {
        shieldAbsorbed_ = std::min(context.magnitude(), request_.availableShield);
        auto recorded = context.addAbsorbed(shieldAbsorbed_);
        if (!recorded) return recorded;
        return context.setMagnitude(context.magnitude() - shieldAbsorbed_);
    }
    Result<void> clamp(settlement::SettlementContext& context) override { return context.setClampMax(target_.health); }

    Result<settlement::PreparedApply> prepareApply(const settlement::SettlementContext& context) override {
        CombatState candidate    = target_;
        candidate.health         = std::max(0.0, candidate.health - context.magnitude());
        candidate.poise          = std::max(0.0, candidate.poise - amounts_.poiseDamage);
        const CombatState before = target_;
        return Result<settlement::PreparedApply>::success(settlement::PreparedApply(
            [this, candidate]() {
                target_ = candidate;
                return Result<void>::success(Status::success(StatusCode::Applied));
            },
            [this, before]() { target_ = before; }));
    }

    Result<std::vector<settlement::SettlementRequest>> prepareTrigger(
        const settlement::SettlementContext& context, const settlement::SettlementResult& result) override {
        std::vector<settlement::SettlementRequest> transitions;
        const auto append = [&](std::string key) {
            settlement::SettlementRequest transition = context.request();
            transition.kind                           = "trigger";
            transition.resource.clear();
            transition.magnitude = 0.0;
            transition.decisions.clear();
            transition.trigger = std::move(key);
            transition.chain   = {};
            transition.tags.push_back("trigger:" + transition.trigger);
            transitions.push_back(std::move(transition));
        };

        if (request_.availableShield > 0.0 && shieldAbsorbed_ >= request_.availableShield)
            append("shield_break");
        const bool died = target_.health > 0.0 && result.applied >= target_.health;
        if (died) {
            append("death");
            if (context.request().source.isValid() && context.request().source != context.request().target)
                append("kill");
        }
        return Result<std::vector<settlement::SettlementRequest>>::success(std::move(transitions));
    }

    [[nodiscard]] const DamageAmounts& amounts() const noexcept { return amounts_; }
    [[nodiscard]] double shieldAbsorbed() const noexcept { return shieldAbsorbed_; }

private:
    CombatState&             target_;
    const DamageRequest&     request_;
    const HitReactionPolicy& reactionPolicy_;
    DamageAmounts            amounts_;
    double                   shieldAbsorbed_ = 0.0;
};

class HealingSettlementPolicy final : public settlement::ISettlementPolicy {
public:
    explicit HealingSettlementPolicy(CombatState& target) : target_(target) {}

    Result<void> validate(settlement::SettlementContext&) override { return target_.validate(); }
    Result<void> sourceModifiers(settlement::SettlementContext&) override { return Result<void>::success(); }
    Result<void> targetMitigation(settlement::SettlementContext&) override { return Result<void>::success(); }
    Result<void> armorShield(settlement::SettlementContext&) override { return Result<void>::success(); }
    Result<void> clamp(settlement::SettlementContext& context) override {
        return context.setClampMax(target_.maxHealth - target_.health);
    }
    Result<settlement::PreparedApply> prepareApply(const settlement::SettlementContext& context) override {
        const double before    = target_.health;
        const double candidate = before + context.magnitude();
        return Result<settlement::PreparedApply>::success(settlement::PreparedApply(
            [this, candidate]() {
                target_.health = candidate;
                return Result<void>::success(Status::success(StatusCode::Applied));
            },
            [this, before]() { target_.health = before; }));
    }
    Result<std::vector<settlement::SettlementRequest>> prepareTrigger(
        const settlement::SettlementContext&, const settlement::SettlementResult&) override {
        return Result<std::vector<settlement::SettlementRequest>>::success({});
    }

private:
    CombatState& target_;
};

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
    if (!finiteNonNegative(incomingDamageMultiplier))
        return invalid("Incoming damage multiplier is invalid", "incomingDamageMultiplier");
    if (!finiteNonNegative(availableShield)) return invalid("Available shield is invalid", "availableShield");
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
        return Result<DamageAmounts>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Damage request target does not match combat state", "target"));
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
    auto previewed = preview(target, request);
    if (!previewed) return Result<DamageOutcome>::failure(previewed.status());
    const CombatState             before = target;
    DamageSettlementPolicy        adapter(target, request, std::move(previewed).takeValue(), policy_);
    settlement::SettlementRequest settlementRequest;
    settlementRequest.source    = request.source;
    settlementRequest.target    = request.target;
    settlementRequest.kind      = "damage";
    settlementRequest.magnitude = request.healthDamage;
    settlementRequest.tags      = {request.damageType};
    Value::Object context;
    context["damage_type"]               = request.damageType;
    context["poise_damage"]              = request.poiseDamage;
    context["incoming_damage_multiplier"] = request.incomingDamageMultiplier;
    context["available_shield"]           = request.availableShield;
    context["target_health_ratio"] =
        target.maxHealth > 0.0 ? target.health / target.maxHealth : 0.0;
    context["target_poise_ratio"] = target.maxPoise > 0.0 ? target.poise / target.maxPoise : 0.0;
    settlementRequest.context = Value(std::move(context));
    auto settled              = settlement_.settle(settlementRequest, adapter);
    if (!settled) return Result<DamageOutcome>::failure(settled.status());
    const auto          settlementResult = std::move(settled).takeValue();
    const DamageAmounts amounts          = adapter.amounts();
    const double        appliedHealth    = before.health - target.health;
    const double        appliedPoise     = before.poise - target.poise;

    HitReaction reaction = HitReaction::None;
    if (target.health == 0.0 && before.health > 0.0) {
        reaction = HitReaction::Death;
    } else if (target.poise == 0.0 && appliedPoise > 0.0) {
        reaction = HitReaction::Knockdown;
    } else if (before.maxPoise > 0.0 && appliedPoise >= before.maxPoise * policy_.staggerPoiseFraction) {
        reaction = HitReaction::Stagger;
    } else if (appliedHealth > 0.0 && appliedHealth >= policy_.flinchDamageThreshold) {
        reaction = HitReaction::Flinch;
    }

    DamageOutcome outcome{request.source,
                          request.target,
                          before.health,
                          target.health,
                          before.poise,
                          target.poise,
                          appliedHealth,
                          appliedPoise,
                          adapter.shieldAbsorbed(),
                          {request.knockback.x * amounts.knockbackScale, request.knockback.y * amounts.knockbackScale,
                           request.knockback.z * amounts.knockbackScale},
                          reaction,
                          rule_ ? DamageRuleSource::Provider : DamageRuleSource::Default,
                          std::move(settlementRequest),
                          std::move(settlementResult)};
    return Result<DamageOutcome>::success(std::move(outcome), Status::success(StatusCode::Applied));
}

Result<DamageOutcome> DamageRuntime::previewSettlement(const CombatState& target,
                                                       const DamageRequest& request) const {
    CombatState candidate = target;
    return apply(candidate, request);
}

Result<settlement::SettlementResult> DamageRuntime::heal(CombatState& target, SubjectRef source,
                                                          double amount) const {
    auto valid = target.validate();
    if (!valid) return Result<settlement::SettlementResult>::failure(valid.status());
    if (!finiteNonNegative(amount))
        return Result<settlement::SettlementResult>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Healing amount is invalid", "amount"));
    settlement::SettlementRequest request;
    request.source    = std::move(source);
    request.target    = target.subject;
    request.kind      = "heal";
    request.resource  = "health";
    request.magnitude = amount;
    HealingSettlementPolicy policy(target);
    return settlement_.settle(request, policy);
}

Result<void> DamageRuntime::configureSettlementRules(const settlement::SettlementRuleSet& rules) {
    settlement::SettlementPipeline candidate;
    auto                           installed = rules.install(candidate);
    if (!installed) return installed;
    settlement_ = std::move(candidate);
    return Result<void>::success(Status::success(StatusCode::Applied));
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
