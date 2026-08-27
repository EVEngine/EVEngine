#include "weapon/WeaponEffects.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::weapon {
namespace {

const char* kindTag(WeaponEffectKind kind) {
    switch (kind) {
    case WeaponEffectKind::Heat: return "weapon:heat";
    case WeaponEffectKind::Jam: return "weapon:jam";
    case WeaponEffectKind::Recoil: return "weapon:recoil";
    }
    return "weapon:unknown";
}

bool has(const effects::EffectPeriodicTick& tick, const char* tag) {
    return std::find(tick.tags.begin(), tick.tags.end(), tag) != tick.tags.end();
}

}  // namespace

eve::Result<void> WeaponEffectExecutor::validate(const WeaponEffectTarget& target) const {
    if (!std::isfinite(target.heat) || !std::isfinite(target.maxHeat) || !std::isfinite(target.recoil) ||
        target.maxHeat <= 0.0 || target.heat < 0.0 || target.heat > target.maxHeat || target.recoil < 0.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "weapon effect target state is invalid", "target"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WeaponEffectExecutor::applyImmediate(WeaponEffectTarget& target,
                                                       const effects::EffectInstance& effect) const {
    auto valid = validate(target);
    if (!valid) return valid;
    if (effect.hasTag("weapon:jam")) target.jammed = true;
    if (effect.hasTag("weapon:recoil")) {
        if (effect.magnitude > std::numeric_limits<double>::max() - target.recoil)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "weapon recoil overflows", "target.recoil"));
        target.recoil += effect.magnitude;
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<WeaponEffectUpdate> WeaponEffectExecutor::settle(
    WeaponEffectTarget& target, effects::EffectUpdateSummary lifecycle) const {
    auto valid = validate(target);
    if (!valid) return eve::Result<WeaponEffectUpdate>::failure(valid.status());
    WeaponEffectUpdate result;
    result.lifecycle = std::move(lifecycle);
    for (const auto& tick : result.lifecycle.periodicTicks) {
        if (tick.stackCount == 0 || !std::isfinite(tick.magnitude) || tick.magnitude < 0.0)
            return eve::Result<WeaponEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "weapon periodic effect value is invalid", "periodic"));
        const double amount = tick.magnitude * static_cast<double>(tick.stackCount);
        if (!std::isfinite(amount))
            return eve::Result<WeaponEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "weapon periodic effect overflows", "periodic"));
        if (has(tick, "weapon:recoil")) {
            if (amount > std::numeric_limits<double>::max() - target.recoil)
                return eve::Result<WeaponEffectUpdate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "weapon recoil settlement overflows", "target.recoil"));
            target.recoil += amount;
        } else if (has(tick, "weapon:jam")) {
            target.jammed = true;
        } else {
            target.heat = std::min(target.maxHeat, target.heat + amount);
            if (target.heat >= target.maxHeat) {
                target.jammed = true;
                ++target.blockedShots;
                result.jammed = true;
            }
        }
        ++result.settled;
    }
    result.jammed = result.jammed || target.jammed;
    return eve::Result<WeaponEffectUpdate>::success(std::move(result),
                                                    eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<effects::EffectHandle> WeaponEffectAdapter::apply(
    const WeaponEffectDefinition& definition, eve::SubjectRef subject) {
    if (definition.id.empty() || !subject.isValid())
        return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "weapon effect requires an id and valid subject", "effect"));
    effects::EffectDefinition common;
    common.id = definition.id;
    common.stackKey = definition.id;
    common.duration = definition.duration;
    common.period = definition.period;
    common.magnitude = definition.magnitude;
    common.policy = definition.policy;
    common.tags = definition.tags;
    common.tags.push_back(kindTag(definition.kind));
    auto valid = common.validate();
    if (!valid) return eve::Result<effects::EffectHandle>::failure(valid.status());
    auto candidate = container_.clone();
    auto applied = candidate.apply(common, subject.format(), definition.source);
    if (!applied) return eve::Result<effects::EffectHandle>::failure(applied.status());
    const std::string id = std::move(applied).takeValue();
    const auto* instance = candidate.find(id);
    if (!instance) return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvariantViolation, "weapon effect candidate disappeared", "effect"));
    auto targetCandidate = target_;
    auto immediate = executor_.applyImmediate(targetCandidate, *instance);
    if (!immediate) return eve::Result<effects::EffectHandle>::failure(immediate.status());
    auto handle = candidate.handleFor(id);
    if (!handle) return eve::Result<effects::EffectHandle>::failure(handle.status());
    target_ = std::move(targetCandidate);
    container_ = std::move(candidate);
    return eve::Result<effects::EffectHandle>::success(std::move(handle).takeValue(),
                                                      eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WeaponEffectAdapter::remove(effects::EffectHandle handle) {
    auto resolved = container_.resolve(handle);
    if (!resolved) return eve::Result<void>::failure(resolved.status());
    return container_.remove(handle.instanceId);
}

eve::Result<WeaponEffectUpdate> WeaponEffectAdapter::advance(const eve::SimulationStep& step) {
    auto candidate = container_.clone();
    auto lifecycle = effects::EffectExecutor{}.advance(candidate, step);
    if (!lifecycle) return eve::Result<WeaponEffectUpdate>::failure(lifecycle.status());
    auto targetCandidate = target_;
    auto settled = executor_.settle(targetCandidate, std::move(lifecycle).takeValue());
    if (!settled) return settled;
    auto result = std::move(settled).takeValue();
    target_ = std::move(targetCandidate);
    container_ = std::move(candidate);
    return eve::Result<WeaponEffectUpdate>::success(std::move(result),
                                                    eve::Status::success(eve::StatusCode::Applied));
}

WeaponEffectSnapshot WeaponEffectAdapter::snapshot() const { return {container_.snapshot(), target_}; }

eve::Result<void> WeaponEffectAdapter::restore(const WeaponEffectSnapshot& snapshotValue) {
    auto valid = executor_.validate(snapshotValue.target);
    if (!valid) return valid;
    auto restored = container_.restore(snapshotValue.effects);
    if (!restored) return restored;
    target_ = snapshotValue.target;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<const effects::EffectInstance*> WeaponEffectAdapter::resolve(effects::EffectHandle handle) const {
    return container_.resolve(handle);
}

}  // namespace eve::weapon
