#include "combat/Damage.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::combat {
namespace {

Result<void> invalid(std::string message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "combat.settlement"));
}

// Invocation-local policy: the target remains the sole authority. The common
// pipeline owns ordering, validation, event creation and rollback orchestration.
class CombatSettlementPolicy final : public settlement::ISettlementPolicy {
public:
    CombatSettlementPolicy(CombatState& target, const DamageRequest& request, const IDamageRule* rule,
                           HitReactionPolicy policy)
        : target_(target), request_(request), rule_(rule), policy_(policy) {}

    Result<void> validate(settlement::SettlementContext&) override {
        auto valid = target_.validate();
        if (!valid) return valid;
        valid = request_.validate();
        if (!valid) return valid;
        valid = policy_.validate();
        if (!valid) return valid;
        if (target_.subject != request_.target) return invalid("Damage target does not match combat state");
        return Result<void>::success();
    }

    Result<void> sourceModifiers(settlement::SettlementContext& context) override {
        context.setCritical(request_.critical);
        return Result<void>::success();
    }

    Result<void> targetMitigation(settlement::SettlementContext& context) override {
        amounts_ = {context.magnitude(), request_.poiseDamage, 1.0};
        if (rule_) {
            auto modified         = request_;
            modified.healthDamage = context.magnitude();
            auto evaluated        = rule_->evaluate(modified, target_);
            if (!evaluated) return Result<void>::failure(evaluated.status());
            amounts_ = std::move(evaluated).takeValue();
        }
        if (!std::isfinite(amounts_.healthDamage) || amounts_.healthDamage < 0.0 ||
            !std::isfinite(amounts_.poiseDamage) || amounts_.poiseDamage < 0.0 ||
            !std::isfinite(amounts_.knockbackScale) || amounts_.knockbackScale < 0.0)
            return invalid("Damage rule returned invalid amounts");
        auto resisted = context.addResisted(std::max(0.0, context.magnitude() - amounts_.healthDamage));
        if (!resisted) return resisted;
        return context.setMagnitude(amounts_.healthDamage);
    }

    Result<void> armorShield(settlement::SettlementContext&) override { return Result<void>::success(); }
    Result<void> clamp(settlement::SettlementContext& context) override { return context.setClampMax(target_.health); }

    Result<settlement::PreparedApply> prepareApply(const settlement::SettlementContext& context) override {
        before_    = target_;
        candidate_ = target_;
        candidate_.health -= context.magnitude();
        candidate_.poise = std::max(0.0, candidate_.poise - amounts_.poiseDamage);
        auto valid       = candidate_.validate();
        if (!valid) return Result<settlement::PreparedApply>::failure(valid.status());
        outcome.source             = request_.source;
        outcome.target             = request_.target;
        outcome.previousHealth     = before_.health;
        outcome.previousPoise      = before_.poise;
        outcome.health             = candidate_.health;
        outcome.poise              = candidate_.poise;
        outcome.appliedPoiseDamage = before_.poise - candidate_.poise;
        outcome.ruleSource         = rule_ ? DamageRuleSource::Provider : DamageRuleSource::Default;
        outcome.knockback          = {request_.knockback.x * amounts_.knockbackScale,
                                      request_.knockback.y * amounts_.knockbackScale,
                                      request_.knockback.z * amounts_.knockbackScale};
        if (!std::isfinite(outcome.knockback.x) || !std::isfinite(outcome.knockback.y) ||
            !std::isfinite(outcome.knockback.z))
            return Result<settlement::PreparedApply>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Knockback overflow", "knockback"));
        if (candidate_.health == 0.0 && before_.health > 0.0)
            outcome.reaction = HitReaction::Death;
        else if (candidate_.poise == 0.0 && outcome.appliedPoiseDamage > 0.0)
            outcome.reaction = HitReaction::Knockdown;
        else if (before_.maxPoise > 0.0 &&
                 outcome.appliedPoiseDamage >= before_.maxPoise * policy_.staggerPoiseFraction)
            outcome.reaction = HitReaction::Stagger;
        else if (context.magnitude() > 0.0 && context.magnitude() >= policy_.flinchDamageThreshold)
            outcome.reaction = HitReaction::Flinch;
        return Result<settlement::PreparedApply>::success(settlement::PreparedApply(
            [this]() {
                target_    = candidate_;
                committed_ = true;
                return Result<void>::success();
            },
            [this]() {
                if (committed_) target_ = before_;
                committed_ = false;
            }));
    }

    DamageOutcome outcome;
    double        knockbackScale() const { return amounts_.knockbackScale; }

private:
    CombatState&         target_;
    const DamageRequest& request_;
    const IDamageRule*   rule_;
    HitReactionPolicy    policy_;
    DamageAmounts        amounts_;
    CombatState          before_, candidate_;
    bool                 committed_ = false;
};

settlement::SettlementRequest settlementRequest(const DamageRequest& request) {
    settlement::SettlementRequest result;
    result.source    = request.source;
    result.target    = request.target;
    result.kind      = "damage";
    result.magnitude = request.healthDamage;
    result.tags      = {request.damageType};
    result.tick      = request.tick;
    return result;
}

const settlement::SettlementPipeline& canonicalPipeline() {
    static const settlement::SettlementPipeline pipeline;
    return pipeline;
}

}  // namespace

Result<DamageAmounts> DamageRuntime::preview(const CombatState& target, const DamageRequest& request) const {
    CombatState            isolated = target;
    CombatSettlementPolicy adapter(isolated, request, rule_, policy_);
    auto                   settled = canonicalPipeline().settle(settlementRequest(request), adapter);
    if (!settled) return Result<DamageAmounts>::failure(settled.status());
    return Result<DamageAmounts>::success(
        {settled.value().applied, adapter.outcome.appliedPoiseDamage, adapter.knockbackScale()});
}

Result<DamageOutcome> DamageRuntime::apply(CombatState& target, const DamageRequest& request) const {
    return apply(target, request, canonicalPipeline());
}

Result<DamageOutcome> DamageRuntime::apply(CombatState& target, const DamageRequest& request,
                                           const settlement::SettlementPipeline& pipeline,
                                           OptionalRef<game_event::GameEventLog> events) const {
    CombatSettlementPolicy adapter(target, request, rule_, policy_);
    auto settled = pipeline.settle(settlementRequest(request), adapter, events ? &events->get() : nullptr);
    if (!settled) return Result<DamageOutcome>::failure(settled.status());
    adapter.outcome.settlement          = std::move(settled).takeValue();
    adapter.outcome.appliedHealthDamage = adapter.outcome.settlement.applied;
    return Result<DamageOutcome>::success(std::move(adapter.outcome), Status::success(StatusCode::Applied));
}

}  // namespace eve::combat
