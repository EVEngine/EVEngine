#include "vehicle/VehicleEffects.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::vehicle {
namespace {

const char* kindTag(VehicleEffectKind kind) {
    switch (kind) {
    case VehicleEffectKind::Damage: return "vehicle:damage";
    case VehicleEffectKind::Repair: return "vehicle:repair";
    case VehicleEffectKind::Disable: return "vehicle:disable";
    }
    return "vehicle:unknown";
}

double zoneMultiplier(const VehicleEffectTarget& target, const std::vector<std::string>& tags) {
    const auto has = [&tags](const std::string& tag) {
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    };
    if (has("vehicle:zone:front")) return target.frontArmorMultiplier;
    if (has("vehicle:zone:rear")) return target.rearArmorMultiplier;
    return target.sideArmorMultiplier;
}

}  // namespace

eve::Result<void> VehicleEffectExecutor::validate(const VehicleEffectTarget& target) const {
    if (!std::isfinite(target.hull) || !std::isfinite(target.maxHull) || target.maxHull <= 0.0 ||
        target.hull < 0.0 || target.hull > target.maxHull || target.frontArmorMultiplier < 0.0 ||
        target.sideArmorMultiplier < 0.0 || target.rearArmorMultiplier < 0.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "vehicle effect target state is invalid", "target"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> VehicleEffectExecutor::applyImmediate(VehicleEffectTarget& target,
                                                        const effects::EffectInstance& effect) const {
    auto valid = validate(target);
    if (!valid) return valid;
    if (effect.hasTag("vehicle:disable")) target.disabled = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<VehicleEffectUpdate> VehicleEffectExecutor::settle(
    VehicleEffectTarget& target, effects::EffectUpdateSummary lifecycle) const {
    auto valid = validate(target);
    if (!valid) return eve::Result<VehicleEffectUpdate>::failure(valid.status());
    VehicleEffectUpdate result;
    result.lifecycle = std::move(lifecycle);
    for (const auto& tick : result.lifecycle.periodicTicks) {
        if (tick.stackCount == 0 || tick.magnitude < 0.0 || !std::isfinite(tick.magnitude))
            return eve::Result<VehicleEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "vehicle periodic effect value is invalid", "periodic"));
        const double amount = tick.magnitude * static_cast<double>(tick.stackCount);
        if (!std::isfinite(amount))
            return eve::Result<VehicleEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "vehicle periodic settlement overflows", "periodic"));
        const auto has = [&tick](const char* tag) {
            return std::find(tick.tags.begin(), tick.tags.end(), tag) != tick.tags.end();
        };
        if (has("vehicle:repair")) {
            target.hull = std::min(target.maxHull, target.hull + amount);
        } else if (has("vehicle:disable")) {
            target.disabled = true;
        } else {
            const double damage = amount * zoneMultiplier(target, tick.tags);
            if (!std::isfinite(damage))
                return eve::Result<VehicleEffectUpdate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "vehicle armor settlement overflows", "periodic"));
            target.hull = std::max(0.0, target.hull - damage);
            if (damage >= target.maxHull * 0.5) {
                ++target.criticalHits;
                ++result.criticalHits;
            }
        }
        ++result.settled;
    }
    return eve::Result<VehicleEffectUpdate>::success(std::move(result),
                                                     eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<effects::EffectHandle> VehicleEffectAdapter::apply(
    const VehicleEffectDefinition& definition, eve::SubjectRef subject) {
    if (definition.id.empty() || !subject.isValid() || definition.armorZone.empty())
        return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "vehicle effect requires id, zone and valid subject", "effect"));
    effects::EffectDefinition common;
    common.id = definition.id;
    common.stackKey = definition.id;
    common.duration = definition.duration;
    common.period = definition.period;
    common.magnitude = definition.magnitude;
    common.policy = definition.policy;
    common.tags = definition.tags;
    common.tags.push_back(kindTag(definition.kind));
    common.tags.push_back("vehicle:zone:" + definition.armorZone);
    auto valid = common.validate();
    if (!valid) return eve::Result<effects::EffectHandle>::failure(valid.status());
    auto candidate = container_.clone();
    auto applied = candidate.apply(common, subject.format(), definition.source);
    if (!applied) return eve::Result<effects::EffectHandle>::failure(applied.status());
    const std::string id = std::move(applied).takeValue();
    const auto* instance = candidate.find(id);
    if (!instance) return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvariantViolation, "vehicle effect candidate disappeared", "effect"));
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

eve::Result<void> VehicleEffectAdapter::remove(effects::EffectHandle handle) {
    auto resolved = container_.resolve(handle);
    if (!resolved) return eve::Result<void>::failure(resolved.status());
    return container_.remove(handle.instanceId);
}

eve::Result<VehicleEffectUpdate> VehicleEffectAdapter::advance(const eve::SimulationStep& step) {
    auto candidate = container_.clone();
    auto lifecycle = effects::EffectExecutor{}.advance(candidate, step);
    if (!lifecycle) return eve::Result<VehicleEffectUpdate>::failure(lifecycle.status());
    auto targetCandidate = target_;
    auto settled = executor_.settle(targetCandidate, std::move(lifecycle).takeValue());
    if (!settled) return settled;
    auto result = std::move(settled).takeValue();
    target_ = std::move(targetCandidate);
    container_ = std::move(candidate);
    return eve::Result<VehicleEffectUpdate>::success(std::move(result),
                                                     eve::Status::success(eve::StatusCode::Applied));
}

VehicleEffectSnapshot VehicleEffectAdapter::snapshot() const { return {container_.snapshot(), target_}; }

eve::Result<void> VehicleEffectAdapter::restore(const VehicleEffectSnapshot& snapshotValue) {
    auto valid = executor_.validate(snapshotValue.target);
    if (!valid) return valid;
    auto restored = container_.restore(snapshotValue.effects);
    if (!restored) return restored;
    target_ = snapshotValue.target;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<const effects::EffectInstance*> VehicleEffectAdapter::resolve(effects::EffectHandle handle) const {
    return container_.resolve(handle);
}

}  // namespace eve::vehicle
