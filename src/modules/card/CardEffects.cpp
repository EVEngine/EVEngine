#include "card/CardEffects.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::card {
namespace {

eve::Result<void> invalid(const char* message, const char* path = "effect") {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, path));
}

bool hasKind(const effects::EffectInstance& effect, CardEffectKind kind) {
    const char* name = kind == CardEffectKind::Damage ? "card:damage"
                     : kind == CardEffectKind::Heal ? "card:heal"
                                                     : "card:shield";
    return effect.hasTag(name);
}

}  // namespace

eve::Result<void> CardEffectAdapter::initializeTarget(CardEffectTarget target) {
    if (target.maxHealth <= 0 || target.health < 0 || target.health > target.maxHealth ||
        target.barrier < 0)
        return invalid("card effect target state is invalid", "target");
    if (container_.effectCount() != 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict,
            "card effect target cannot be reinitialized while effects are active", "target"));
    target_ = target;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CardEffectExecutor::applyImmediate(CardEffectTarget& target,
                                                     const effects::EffectInstance& effect) const {
    if (target.health < 0 || target.maxHealth <= 0 || target.health > target.maxHealth || target.barrier < 0)
        return invalid("card effect target state is invalid", "target");
    if (!hasKind(effect, CardEffectKind::Shield)) return eve::Result<void>::success();
    if (effect.magnitude < 0.0 || effect.magnitude > static_cast<double>(std::numeric_limits<int>::max()))
        return invalid("card shield magnitude is outside the target range", "magnitude");
    const auto amount = static_cast<int>(effect.magnitude);
    if (amount > std::numeric_limits<int>::max() - target.barrier)
        return invalid("card barrier addition overflows", "target.barrier");
    target.barrier += amount;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<CardEffectUpdate> CardEffectExecutor::settle(CardEffectTarget& target,
                                                          effects::EffectUpdateSummary lifecycle) const {
    if (target.health < 0 || target.maxHealth <= 0 || target.health > target.maxHealth || target.barrier < 0)
        return eve::Result<CardEffectUpdate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card effect target state is invalid", "target"));
    CardEffectUpdate result;
    result.lifecycle = std::move(lifecycle);
    for (const auto& tick : result.lifecycle.periodicTicks) {
        if (tick.magnitude < 0.0 || tick.stackCount == 0)
            return eve::Result<CardEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "card periodic magnitude or stack is invalid", "periodic"));
        const auto amount = tick.magnitude * static_cast<double>(tick.stackCount);
        if (!std::isfinite(amount) || amount > static_cast<double>(std::numeric_limits<int>::max()))
            return eve::Result<CardEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "card periodic settlement overflows", "periodic"));
        if (tick.effectId.empty())
            return eve::Result<CardEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "card periodic trigger has no effect identity", "periodic.effectId"));
        const auto tagged = [&tick](const char* value) {
            return std::find(tick.tags.begin(), tick.tags.end(), value) != tick.tags.end();
        };
        if (tagged("card:shield")) {
            const auto shield = static_cast<int>(amount);
            if (shield > std::numeric_limits<int>::max() - target.barrier)
                return eve::Result<CardEffectUpdate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "card periodic barrier addition overflows", "target.barrier"));
            target.barrier += shield;
        } else if (tagged("card:heal")) {
            target.health = std::min(target.maxHealth, target.health + static_cast<int>(amount));
        } else {
            const auto damage = static_cast<int>(amount);
            const auto absorbed = std::min(target.barrier, damage);
            target.barrier -= absorbed;
            target.health = std::max(0, target.health - (damage - absorbed));
            result.absorbed += static_cast<std::uint32_t>(absorbed);
            if (target.health == 0) {
                ++target.deathTriggers;
                result.deathTriggered = true;
            }
        }
        ++result.settled;
    }
    return eve::Result<CardEffectUpdate>::success(std::move(result),
                                                  eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<effects::EffectHandle> CardEffectAdapter::apply(const CardEffectDefinition& definition,
                                                            eve::SubjectRef subject) {
    if (definition.id.empty())
        return eve::Result<effects::EffectHandle>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card effect id must not be empty", "id"));
    if (!subject.isValid())
        return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "card effect subject must be valid", "subject"));
    effects::EffectDefinition common;
    common.id = definition.id;
    common.stackKey = definition.id;
    common.duration = definition.duration;
    common.period = definition.period;
    common.magnitude = definition.magnitude;
    common.policy = definition.policy;
    common.tags = definition.tags;
    common.tags.push_back(definition.kind == CardEffectKind::Damage ? "card:damage"
                      : definition.kind == CardEffectKind::Heal ? "card:heal" : "card:shield");
    if (definition.deathTrigger) common.tags.push_back("card:death-trigger");
    auto valid = common.validate();
    if (!valid) return eve::Result<effects::EffectHandle>::failure(valid.status());

    auto candidate = container_.clone();
    auto applied = candidate.apply(common, subject.format(), definition.source);
    if (!applied) return eve::Result<effects::EffectHandle>::failure(applied.status());
    const std::string id = std::move(applied).takeValue();
    auto instance = candidate.find(id);
    if (!instance) return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvariantViolation, "card effect candidate disappeared", "effect"));
    CardEffectTarget targetCandidate = target_;
    auto immediate = executor_.applyImmediate(targetCandidate, *instance);
    if (!immediate) return eve::Result<effects::EffectHandle>::failure(immediate.status());
    auto handle = candidate.handleFor(id);
    if (!handle) return eve::Result<effects::EffectHandle>::failure(handle.status());
    target_ = std::move(targetCandidate);
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
    auto settled = executor_.settle(targetCandidate, std::move(lifecycle).takeValue());
    if (!settled) return settled;
    auto result = std::move(settled).takeValue();
    target_ = std::move(targetCandidate);
    container_ = std::move(candidate);
    return eve::Result<CardEffectUpdate>::success(std::move(result),
                                                  eve::Status::success(eve::StatusCode::Applied));
}

std::size_t CardEffectAdapter::count() const noexcept {
    return static_cast<std::size_t>(container_.effectCount());
}

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
