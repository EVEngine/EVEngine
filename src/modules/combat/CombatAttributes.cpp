#include "combat/CombatAttributes.h"

#include "common/Diagnostic.h"
#include "tags/GameplayTag.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::combat {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

Result<void> CombatAttributeDefinition::validate() const {
    if (!tags::isValidGameplayTagName(tag))
        return failure<void>(DiagnosticCode::InvalidArgument, "Combat attribute tag is invalid", "tag");
    if (!std::isfinite(initialValue) || !std::isfinite(minimumValue) || !std::isfinite(maximumValue) ||
        !std::isfinite(regenerationPerSecond))
        return failure<void>(DiagnosticCode::InvalidArgument, "Combat attribute values must be finite", tag);
    if (minimumValue > maximumValue)
        return failure<void>(DiagnosticCode::InvalidArgument, "Combat attribute minimum exceeds maximum", tag);
    if (initialValue < minimumValue || initialValue > maximumValue)
        return failure<void>(DiagnosticCode::InvalidArgument, "Combat attribute initial value is outside bounds", tag);
    if (regenerationPerSecond < 0.0)
        return failure<void>(DiagnosticCode::InvalidArgument, "Combat attribute regeneration must be non-negative",
                             tag);
    return Result<void>::success();
}

Result<void> CombatAttributeRuntime::registerAttribute(CombatAttributeDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return valid;
    if (definitions_.contains(definition.tag))
        return failure<void>(DiagnosticCode::AlreadyExists, "Combat attribute is already registered", definition.tag);
    const std::string tag = definition.tag;
    definitions_.emplace(tag, std::move(definition));
    values_.setBase(tag, definitions_.at(tag).initialValue);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<double> CombatAttributeRuntime::value(std::string_view tag) const {
    const auto definition = definitions_.find(tag);
    if (definition == definitions_.end())
        return failure<double>(DiagnosticCode::NotFound, "Combat attribute is not registered", std::string(tag));
    return Result<double>::success(values_.getBase(definition->first));
}

Result<std::vector<CombatAttributeEvent>> CombatAttributeRuntime::commit(
    const CombatAttributeDefinition& definition, double newValue) {
    if (!std::isfinite(newValue))
        return failure<std::vector<CombatAttributeEvent>>(DiagnosticCode::InvalidArgument,
                                                           "Combat attribute value must be finite", definition.tag);
    const double previous = values_.getBase(definition.tag);
    const double clamped  = std::clamp(newValue, definition.minimumValue, definition.maximumValue);
    if (clamped == previous)
        return Result<std::vector<CombatAttributeEvent>>::success({}, Status::success(StatusCode::NoOp));

    values_.setBase(definition.tag, clamped);
    std::vector<CombatAttributeEvent> events;
    const auto kind = clamped > previous ? CombatAttributeEventKind::Gain : CombatAttributeEventKind::Loss;
    events.push_back({definition.tag, kind, previous, clamped, clamped - previous});
    if (clamped == definition.minimumValue && previous > definition.minimumValue)
        events.push_back({definition.tag, CombatAttributeEventKind::Depleted, previous, clamped, clamped - previous});
    return Result<std::vector<CombatAttributeEvent>>::success(std::move(events),
                                                               Status::success(StatusCode::Applied));
}

Result<std::vector<CombatAttributeEvent>> CombatAttributeRuntime::setValue(std::string_view tag, double value) {
    const auto definition = definitions_.find(tag);
    if (definition == definitions_.end())
        return failure<std::vector<CombatAttributeEvent>>(DiagnosticCode::NotFound,
                                                           "Combat attribute is not registered", std::string(tag));
    return commit(definition->second, value);
}

Result<std::vector<CombatAttributeEvent>> CombatAttributeRuntime::modify(std::string_view tag, double delta) {
    if (!std::isfinite(delta))
        return failure<std::vector<CombatAttributeEvent>>(DiagnosticCode::InvalidArgument,
                                                           "Combat attribute delta must be finite", std::string(tag));
    const auto definition = definitions_.find(tag);
    if (definition == definitions_.end())
        return failure<std::vector<CombatAttributeEvent>>(DiagnosticCode::NotFound,
                                                           "Combat attribute is not registered", std::string(tag));
    return commit(definition->second, values_.getBase(definition->first) + delta);
}

Result<CombatAttributeAdvance> CombatAttributeRuntime::advance(Duration delta) {
    if (delta < Duration::zero())
        return failure<CombatAttributeAdvance>(DiagnosticCode::InvalidArgument,
                                               "Combat attribute delta must be non-negative", "delta");
    if (delta.isZero())
        return Result<CombatAttributeAdvance>::success({}, Status::success(StatusCode::NoOp));

    struct Candidate {
        const CombatAttributeDefinition* definition = nullptr;
        double                           value      = 0.0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(definitions_.size());
    for (const auto& [tag, definition] : definitions_) {
        const double amount = definition.regenerationPerSecond * delta.seconds();
        if (!std::isfinite(amount))
            return failure<CombatAttributeAdvance>(DiagnosticCode::InvalidArgument,
                                                   "Combat attribute regeneration overflowed", tag);
        candidates.push_back({&definition, values_.getBase(tag) + amount});
    }

    CombatAttributeAdvance result;
    for (const Candidate& candidate : candidates) {
        auto committed = commit(*candidate.definition, candidate.value);
        if (!committed) return Result<CombatAttributeAdvance>::failure(committed.status());
        auto events = std::move(committed).takeValue();
        result.events.insert(result.events.end(), std::make_move_iterator(events.begin()),
                             std::make_move_iterator(events.end()));
    }
    return Result<CombatAttributeAdvance>::success(std::move(result));
}

std::vector<CombatAttributeDefinition> CombatAttributeRuntime::definitions() const {
    std::vector<CombatAttributeDefinition> result;
    result.reserve(definitions_.size());
    for (const auto& [tag, definition] : definitions_) result.push_back(definition);
    return result;
}

}  // namespace eve::combat
