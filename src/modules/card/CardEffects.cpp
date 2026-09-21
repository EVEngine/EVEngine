#include "card/CardEffects.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::card {
namespace {

eve::Result<void> invalid(const char* message, const char* path = "effect") {
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, path));
}

bool hasKind(const effects::EffectInstance& effect, CardEffectKind kind) {
    const char* name = kind == CardEffectKind::Damage ? "card:damage"
                       : kind == CardEffectKind::Heal ? "card:heal"
                                                      : "card:shield";
    return effect.hasTag(name);
}

class CardSettlementPolicy final : public settlement::ISettlementPolicy {
public:
    explicit CardSettlementPolicy(CardEffectTarget& target) : target_(target) {}

    eve::Result<void> validate(settlement::SettlementContext& context) override {
        if (target_.health < 0 || target_.maxHealth <= 0 || target_.health > target_.maxHealth || target_.barrier < 0)
            return invalid("card effect target state is invalid", "target");
        if (context.request().kind != "damage" && context.request().kind != "healing" &&
            context.request().kind != "shield")
            return invalid("card settlement kind is unsupported", "kind");
        return eve::Result<void>::success();
    }
    eve::Result<void> sourceModifiers(settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(settlement::SettlementContext& context) override {
        if (context.request().kind != "damage") return eve::Result<void>::success();
        const double absorbed = std::min(context.magnitude(), static_cast<double>(target_.barrier));
        barrierAbsorbed_ = absorbed;
        auto recorded = context.addAbsorbed(absorbed);
        if (!recorded) return recorded;
        return context.setMagnitude(context.magnitude() - absorbed);
    }
    eve::Result<void> clamp(settlement::SettlementContext& context) override {
        const double maximum = context.request().kind == "damage"
                                   ? target_.health
                                   : context.request().kind == "healing"
                                         ? target_.maxHealth - target_.health
                                         : std::numeric_limits<int>::max() - target_.barrier;
        return context.setClampMax(std::max(0.0, maximum));
    }
    eve::Result<settlement::PreparedApply> prepareApply(const settlement::SettlementContext& context) override {
        const CardEffectTarget before = target_;
        CardEffectTarget       candidate = target_;
        const int applied = static_cast<int>(context.magnitude());
        const int absorbed = static_cast<int>(barrierAbsorbed_);
        if (context.request().kind == "damage") {
            candidate.barrier -= absorbed;
            candidate.health = std::max(0, candidate.health - applied);
            if (candidate.health == 0 && before.health > 0) ++candidate.deathTriggers;
        } else if (context.request().kind == "healing") {
            candidate.health = std::min(candidate.maxHealth, candidate.health + applied);
        } else {
            candidate.barrier += applied;
        }
        return eve::Result<settlement::PreparedApply>::success(settlement::PreparedApply(
            [this, candidate]() {
                target_ = candidate;
                return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
            },
            [this, before]() { target_ = before; }));
    }

    eve::Result<std::vector<settlement::SettlementRequest>> prepareTrigger(
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
        if (target_.barrier > 0 && barrierAbsorbed_ >= static_cast<double>(target_.barrier)) append("shield_break");
        const bool died = context.request().kind == "damage" && target_.health > 0 &&
                          result.applied >= static_cast<double>(target_.health);
        if (died) {
            append("death");
            if (context.request().source.isValid() && context.request().source != context.request().target)
                append("kill");
        }
        return eve::Result<std::vector<settlement::SettlementRequest>>::success(std::move(transitions));
    }

private:
    CardEffectTarget& target_;
    double            barrierAbsorbed_ = 0.0;
};

}  // namespace

eve::Result<void> CardEffectExecutor::configureSettlementRules(const settlement::SettlementRuleSet& rules) {
    settlement::SettlementPipeline candidate;
    auto installed = rules.install(candidate);
    if (!installed) return installed;
    settlement_ = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CardEffectAdapter::initializeTarget(CardEffectTarget target) {
    if (target.maxHealth <= 0 || target.health < 0 || target.health > target.maxHealth || target.barrier < 0)
        return invalid("card effect target state is invalid", "target");
    if (container_.effectCount() != 0)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                   "card effect target cannot be reinitialized while effects are active", "target"));
    target_ = target;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CardEffectExecutor::applyImmediate(CardEffectTarget&               target,
                                                     const effects::EffectInstance&  effect,
                                                     const effects::EffectContainer& activeEffects) const {
    if (target.health < 0 || target.maxHealth <= 0 || target.health > target.maxHealth || target.barrier < 0)
        return invalid("card effect target state is invalid", "target");
    const bool damage = hasKind(effect, CardEffectKind::Damage);
    const bool heal   = hasKind(effect, CardEffectKind::Heal);
    const bool shield = hasKind(effect, CardEffectKind::Shield);
    if (!shield && ((!damage && !heal) || effect.period > 0.0)) return eve::Result<void>::success();
    if (effect.magnitude < 0.0 || effect.magnitude > static_cast<double>(std::numeric_limits<int>::max()))
        return invalid("card immediate magnitude is outside the target range", "magnitude");
    const auto parsedSubject = eve::PersistentId::parse(effect.subject);
    if (!parsedSubject) return invalid("card shield subject is invalid", "subject");
    settlement::SettlementRequest request;
    request.source = eve::SubjectRef::fromPersistentId(eve::PersistentId::fromUuid(effect.identity));
    request.target = eve::SubjectRef::fromPersistentId(*parsedSubject);
    request.kind = shield ? "shield" : heal ? "healing" : "damage";
    request.magnitude = effect.magnitude;
    request.tags = effect.tags;
    CardSettlementPolicy policy(target);
    auto activePipeline = settlement_;
    auto activeRules = settlement::projectEffectRules(activeEffects, "card.active");
    if (!activeRules) return eve::Result<void>::failure(activeRules.status());
    auto installed = activeRules.value().install(activePipeline);
    if (!installed) return installed;
    auto settled = activePipeline.settle(request, policy);
    if (!settled) return eve::Result<void>::failure(settled.status());
    const auto outcome = std::move(settled).takeValue();
    (void)outcome;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<CardEffectUpdate> CardEffectExecutor::settle(CardEffectTarget&               target,
                                                         effects::EffectUpdateSummary    lifecycle,
                                                         const effects::EffectContainer& activeEffects) const {
    if (target.health < 0 || target.maxHealth <= 0 || target.health > target.maxHealth || target.barrier < 0)
        return eve::Result<CardEffectUpdate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "card effect target state is invalid", "target"));
    CardEffectUpdate result;
    result.lifecycle = std::move(lifecycle);
    auto activePipeline = settlement_;
    auto activeRules = settlement::projectEffectRules(activeEffects, "card.active");
    if (!activeRules) return eve::Result<CardEffectUpdate>::failure(activeRules.status());
    auto installed = activeRules.value().install(activePipeline);
    if (!installed) return eve::Result<CardEffectUpdate>::failure(installed.status());
    for (const auto& tick : result.lifecycle.periodicTicks) {
        if (tick.magnitude < 0.0 || tick.stackCount == 0)
            return eve::Result<CardEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "card periodic magnitude or stack is invalid", "periodic"));
        const auto amount = tick.magnitude * static_cast<double>(tick.stackCount);
        if (!std::isfinite(amount) || amount > static_cast<double>(std::numeric_limits<int>::max()))
            return eve::Result<CardEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "card periodic settlement overflows", "periodic"));
        if (tick.effectId.empty())
            return eve::Result<CardEffectUpdate>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                       "card periodic trigger has no effect identity", "periodic.effectId"));
        const auto tagged = [&tick](const char* value) {
            return std::find(tick.tags.begin(), tick.tags.end(), value) != tick.tags.end();
        };
        {
            const auto parsedSubject = eve::PersistentId::parse(tick.subject);
            if (!parsedSubject)
                return eve::Result<CardEffectUpdate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "card periodic subject is invalid", "periodic.subject"));
            settlement::SettlementRequest request;
            request.source = eve::SubjectRef::fromPersistentId(
                eve::PersistentId::fromUuid(tick.effectIdentity));
            request.target = eve::SubjectRef::fromPersistentId(*parsedSubject);
            request.kind = tagged("card:shield") ? "shield" : tagged("card:heal") ? "healing" : "damage";
            request.magnitude = amount;
            request.tags = tick.tags;
            request.tick = tick.tick;
            CardSettlementPolicy policy(target);
            auto settled = activePipeline.settle(request, policy);
            if (!settled) return eve::Result<CardEffectUpdate>::failure(settled.status());
            const auto status  = settled.status();
            auto       outcome = std::move(settled).takeValue();
            result.absorbed += static_cast<std::uint32_t>(outcome.absorbed);
            if (tagged("card:death-trigger") &&
                std::any_of(outcome.derived.begin(), outcome.derived.end(),
                            [](const auto& derived) { return derived.trigger == "death"; }))
                result.deathTriggered = true;
            settlement::SettlementBatchItemResult audit;
            audit.index   = result.settlements.size();
            audit.request = std::move(request);
            audit.status  = status;
            audit.result  = std::move(outcome);
            result.settlements.push_back(std::move(audit));
        }
        ++result.settled;
    }
    return eve::Result<CardEffectUpdate>::success(std::move(result), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CardEffectAdapter::configureSettlementRules(const settlement::SettlementRuleSet& rules) {
    return executor_.configureSettlementRules(rules);
}

eve::Result<effects::EffectHandle> CardEffectAdapter::apply(const CardEffectDefinition& definition,
                                                            eve::SubjectRef             subject) {
    if (definition.id.empty())
        return eve::Result<effects::EffectHandle>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card effect id must not be empty", "id"));
    if (!subject.isValid())
        return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "card effect subject must be valid", "subject"));
    effects::EffectDefinition common;
    common.id        = definition.id;
    common.stackKey  = definition.id;
    common.duration  = definition.duration;
    common.period    = definition.period;
    common.magnitude = definition.magnitude;
    common.policy    = definition.policy;
    common.payload   = definition.payload;
    common.tags      = definition.tags;
    common.tags.push_back(definition.kind == CardEffectKind::Damage ? "card:damage"
                          : definition.kind == CardEffectKind::Heal ? "card:heal"
                                                                    : "card:shield");
    if (definition.deathTrigger) common.tags.push_back("card:death-trigger");
    auto valid = common.validate();
    if (!valid) return eve::Result<effects::EffectHandle>::failure(valid.status());

    auto candidate = container_.clone();
    auto applied   = candidate.apply(common, subject.format(), definition.source);
    if (!applied) return eve::Result<effects::EffectHandle>::failure(applied.status());
    const std::string id       = std::move(applied).takeValue();
    auto              instance = candidate.find(id);
    if (!instance)
        return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "card effect candidate disappeared", "effect"));
    CardEffectTarget targetCandidate = target_;
    auto             immediate       = executor_.applyImmediate(targetCandidate, *instance, candidate);
    if (!immediate) return eve::Result<effects::EffectHandle>::failure(immediate.status());
    auto handle = candidate.handleFor(id);
    if (!handle) return eve::Result<effects::EffectHandle>::failure(handle.status());
    target_    = std::move(targetCandidate);
    container_ = std::move(candidate);
    return eve::Result<effects::EffectHandle>::success(std::move(handle).takeValue(),
                                                       eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CardEffectAdapter::remove(effects::EffectHandle handle) {
    auto resolved = container_.resolve(handle);
    if (!resolved) return eve::Result<void>::failure(resolved.status());
    return container_.remove(handle.instanceId);
}

eve::Result<CardEffectUpdate> CardEffectAdapter::advance(const eve::SimulationStep& step) {
    auto candidate = container_.clone();
    auto lifecycle = effects::EffectExecutor{}.advance(candidate, step);
    if (!lifecycle) return eve::Result<CardEffectUpdate>::failure(lifecycle.status());
    auto targetCandidate = target_;
    auto settled         = executor_.settle(targetCandidate, std::move(lifecycle).takeValue(), candidate);
    if (!settled) return settled;
    auto result = std::move(settled).takeValue();
    target_     = std::move(targetCandidate);
    container_  = std::move(candidate);
    return eve::Result<CardEffectUpdate>::success(std::move(result), eve::Status::success(eve::StatusCode::Applied));
}

std::size_t CardEffectAdapter::count() const noexcept { return static_cast<std::size_t>(container_.effectCount()); }

CardEffectSnapshot CardEffectAdapter::snapshot() const { return {container_.snapshot(), target_}; }

eve::Result<void> CardEffectAdapter::restore(const CardEffectSnapshot& snapshotValue) {
    if (snapshotValue.target.health < 0 || snapshotValue.target.maxHealth <= 0 ||
        snapshotValue.target.health > snapshotValue.target.maxHealth || snapshotValue.target.barrier < 0)
        return invalid("card effect snapshot target state is invalid", "snapshot.target");
    auto restored = container_.restore(snapshotValue.effects);
    if (!restored) return restored;
    target_ = snapshotValue.target;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<const effects::EffectInstance*> CardEffectAdapter::resolve(effects::EffectHandle handle) const {
    return container_.resolve(handle);
}

}  // namespace eve::card
