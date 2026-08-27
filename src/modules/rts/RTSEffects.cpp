#include "rts/RTSEffects.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::rts {
namespace {

const char* kindTag(RTSEffectKind kind) {
    switch (kind) {
    case RTSEffectKind::Morale: return "rts:morale";
    case RTSEffectKind::Suppression: return "rts:suppression";
    case RTSEffectKind::ProductionLock: return "rts:production-lock";
    }
    return "rts:unknown";
}

}  // namespace

eve::Result<void> RTSEffectExecutor::validate(const RTSEffectTarget& target) const {
    if (!std::isfinite(target.morale) || target.morale < 0.0 || target.morale > 100.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "RTS morale must be in [0,100]", "target.morale"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RTSEffectExecutor::applyImmediate(RTSEffectTarget& target,
                                                    const effects::EffectInstance& effect) const {
    auto valid = validate(target);
    if (!valid) return valid;
    if (effect.hasTag("rts:production-lock")) target.productionLocked = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<RTSEffectUpdate> RTSEffectExecutor::settle(RTSEffectTarget& target,
                                                       effects::EffectUpdateSummary lifecycle) const {
    auto valid = validate(target);
    if (!valid)
        return eve::Result<RTSEffectUpdate>::failure(valid.status());
    RTSEffectUpdate result;
    result.lifecycle = std::move(lifecycle);
    for (const auto& tick : result.lifecycle.periodicTicks) {
        if (tick.stackCount == 0 || !std::isfinite(tick.magnitude) || tick.magnitude < 0.0)
            return eve::Result<RTSEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "RTS periodic effect value is invalid", "periodic"));
        const auto amount = tick.magnitude * static_cast<double>(tick.stackCount);
        if (!std::isfinite(amount))
            return eve::Result<RTSEffectUpdate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "RTS periodic effect overflows", "periodic"));
        const auto tagged = [&tick](const char* tag) {
            return std::find(tick.tags.begin(), tick.tags.end(), tag) != tick.tags.end();
        };
        if (tagged("rts:morale")) {
            target.morale = std::max(0.0, target.morale - amount);
            if (target.morale == 0.0) ++target.commandInterrupts;
        } else if (tagged("rts:suppression")) {
            if (target.suppression > std::numeric_limits<std::uint32_t>::max() - tick.stackCount)
                return eve::Result<RTSEffectUpdate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "RTS suppression count overflows", "target.suppression"));
            target.suppression += tick.stackCount;
        } else if (tagged("rts:production-lock")) {
            target.productionLocked = true;
            ++result.commandInterrupts;
        }
        ++result.settled;
    }
    return eve::Result<RTSEffectUpdate>::success(std::move(result),
                                                 eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<effects::EffectHandle> RTSEffectAdapter::apply(const RTSEffectDefinition& definition,
                                                           eve::SubjectRef subject) {
    if (definition.id.empty() || !subject.isValid())
        return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "RTS effect requires an id and valid subject", "effect"));
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
        eve::DiagnosticCode::InvariantViolation, "RTS effect candidate disappeared", "effect"));
    auto targetCandidate = target_;
    auto targetValid = executor_.applyImmediate(targetCandidate, *instance);
    if (!targetValid) return eve::Result<effects::EffectHandle>::failure(targetValid.status());
    auto handle = candidate.handleFor(id);
    if (!handle) return eve::Result<effects::EffectHandle>::failure(handle.status());
    target_ = std::move(targetCandidate);
    container_ = std::move(candidate);
    return eve::Result<effects::EffectHandle>::success(std::move(handle).takeValue(),
                                                      eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RTSEffectAdapter::remove(effects::EffectHandle handle) {
    auto resolved = container_.resolve(handle);
    if (!resolved) return eve::Result<void>::failure(resolved.status());
    return container_.remove(handle.instanceId);
}

eve::Result<RTSEffectUpdate> RTSEffectAdapter::advance(const eve::SimulationStep& step) {
    auto candidate = container_.clone();
    auto lifecycle = effects::EffectExecutor{}.advance(candidate, step);
    if (!lifecycle) return eve::Result<RTSEffectUpdate>::failure(lifecycle.status());
    auto targetCandidate = target_;
    auto settled = executor_.settle(targetCandidate, std::move(lifecycle).takeValue());
    if (!settled) return settled;
    auto result = std::move(settled).takeValue();
    target_ = std::move(targetCandidate);
    container_ = std::move(candidate);
    return eve::Result<RTSEffectUpdate>::success(std::move(result),
                                                 eve::Status::success(eve::StatusCode::Applied));
}

RTSEffectSnapshot RTSEffectAdapter::snapshot() const { return {container_.snapshot(), target_}; }

eve::Result<void> RTSEffectAdapter::restore(const RTSEffectSnapshot& snapshotValue) {
    auto valid = executor_.validate(snapshotValue.target);
    if (!valid) return valid;
    auto restored = container_.restore(snapshotValue.effects);
    if (!restored) return restored;
    target_ = snapshotValue.target;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::size_t RTSEffectAdapter::count() const noexcept {
    return static_cast<std::size_t>(container_.effectCount());
}

eve::Result<const effects::EffectInstance*> RTSEffectAdapter::resolve(effects::EffectHandle handle) const {
    return container_.resolve(handle);
}

eve::Result<void> RTSEffectComponent::bindSubject(eve::SubjectRef subject) {
    if (!subject.isValid())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "RTS effect component requires a valid subject", "subject"));
    if (subject_.isValid() && subject_ != subject && adapter_.count() != 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict,
            "RTS effect component cannot change subject while effects are active", "subject"));
    subject_ = subject;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<effects::EffectHandle> RTSEffectComponent::apply(
    const RTSEffectDefinition& definition) {
    if (!subject_.isValid())
        return eve::Result<effects::EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "RTS effect component has no bound subject", "subject"));
    return adapter_.apply(definition, subject_);
}

eve::Result<void> RTSEffectComponent::remove(effects::EffectHandle handle) {
    return adapter_.remove(handle);
}

eve::Result<RTSEffectUpdate> RTSEffectComponent::advance(
    const eve::SimulationStep& step) {
    return adapter_.advance(step);
}

}  // namespace eve::rts
