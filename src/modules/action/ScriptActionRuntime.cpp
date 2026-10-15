#include "action/ScriptActionRuntime.h"

#include "action/AbilityAsset.h"
#include "action/ActionTimeline.h"
#include "tags/GameplayTag.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace eve::action {

Result<Value> ScriptActionRuntime::registerAbilityJson(const std::string& abilityJson) {
    auto decoded = Value::fromJson(abilityJson);
    if (!decoded) return Result<Value>::failure(decoded.status());
    auto definition = decodeAbilityAsset(decoded.value());
    if (!definition) return Result<Value>::failure(definition.status());
    const std::string definitionId = definition.value().id.format();
    const LogicalId   actionId     = definition.value().action.id;
    auto              registered   = abilities_.registerDefinition(std::move(definition).takeValue());
    if (!registered) return Result<Value>::failure(registered.status());
    abilityActions_[definitionId] = actionId;
    Value::Object result;
    result["definitionId"] = definitionId;
    result["actionId"]     = actionId.format();
    return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::replaceAbilityJson(const std::string& abilityJson) {
    auto decoded = Value::fromJson(abilityJson);
    if (!decoded) return Result<Value>::failure(decoded.status());
    auto definition = decodeAbilityAsset(decoded.value());
    if (!definition) return Result<Value>::failure(definition.status());
    const std::string definitionId = definition.value().id.format();
    const LogicalId   actionId     = definition.value().action.id;
    auto              replaced     = abilities_.replaceDefinition(std::move(definition).takeValue());
    if (!replaced) return Result<Value>::failure(replaced.status());
    abilityActions_[definitionId] = actionId;
    Value::Object result;
    result["definitionId"] = definitionId;
    result["actionId"]     = actionId.format();
    return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::registerTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                          double cooldownSeconds, const std::string& instancingText,
                                          const std::string& groupText, const std::string& triggerTag) {
        return setTimelineAbility(definitionText, timelineJson, cooldownSeconds, instancingText, groupText,
                                  triggerTag, false);
    }

Result<Value> ScriptActionRuntime::replaceTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                         double cooldownSeconds, const std::string& instancingText,
                                         const std::string& groupText, const std::string& triggerTag) {
        return setTimelineAbility(definitionText, timelineJson, cooldownSeconds, instancingText, groupText,
                                  triggerTag, true);
    }

Result<Value> ScriptActionRuntime::grantAbility(const std::string& ownerId, const std::string& definitionText) {
    auto definitionId = LogicalId::parse(definitionText);
    if (!definitionId)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "ability definition id is invalid", "definitionId", {},
                                                        "action.squirrel"));
    if (!abilityActions_.contains(definitionText))
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                        "ability definition is not registered", definitionText, {},
                                                        "action.squirrel"));
    auto granted = abilities_.grant(ownerId, *definitionId);
    if (!granted) return Result<Value>::failure(granted.status());
    return abilityGrantValue(granted.value());
}

Result<Value> ScriptActionRuntime::revokeAbility(std::int64_t grantValue) {
    auto grant = parseGrant(grantValue);
    if (!grant) return Result<Value>::failure(grant.status());
    auto revoked = abilities_.revoke(grant.value());
    if (!revoked) return Result<Value>::failure(revoked.status());
    return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::activateAbility(std::int64_t grantValue, std::int64_t tickValue) {
    auto grant = parseGrant(grantValue);
    if (!grant) return Result<Value>::failure(grant.status());
    if (tickValue < 0)
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick", {}, "action.squirrel"));
    auto grantState = abilities_.findGrant(grant.value());
    if (!grantState) return Result<Value>::failure(grantState.status());
    const auto action = abilityActions_.find(grantState.value().definitionId.format());
    if (action == abilityActions_.end())
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                        "ability definition action is unavailable", "definitionId",
                                                        {}, "action.squirrel"));
    ActionRequest request;
    request.actionId      = action->second;
    request.requestedTick = SimulationTick(static_cast<std::uint64_t>(tickValue));
    const auto tick       = request.requestedTick;
    auto       activated  = abilities_.activate(grant.value(), std::move(request), tick);
    if (!activated) return Result<Value>::failure(activated.status());
    return Result<Value>::success(abilityActivationValue(activated.value()),
                                  Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::advanceAbilities(std::int64_t tickValue, double seconds) {
    if (tickValue < 0)
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick", {}, "action.squirrel"));
    auto delta = Duration::fromSeconds(seconds);
    if (!delta) return Result<Value>::failure(delta.status());
    Value::Array          advances;
    std::optional<Status> firstFailure;
    for (const auto& activation : abilities_.activeActivations()) {
        auto advanced = actions_.advance(activation.executionId,
                                         SimulationTick(static_cast<std::uint64_t>(tickValue)), delta.value());
        if (!advanced) {
            if (!firstFailure) firstFailure = advanced.status();
            continue;
        }
        Value::Object value;
        value["activationId"]   = static_cast<std::int64_t>(activation.id.value());
        value["executionId"]    = static_cast<std::int64_t>(activation.executionId.value());
        value["phase"]          = std::string(actionPhaseName(advanced.value().phase));
        value["elapsedSeconds"] = advanced.value().totalElapsed.seconds();
        advances.emplace_back(std::move(value));
    }
    auto cooled = abilities_.advanceCooldowns(delta.value());
    if (!cooled) return Result<Value>::failure(cooled.status());
    // Always synchronize so terminal (Failed/Cancelled) activations leave the
    // active list even when one advance returned an error; otherwise the next
    // call re-hits the same activation and never advances the rest.
    auto synchronized = abilities_.synchronize();
    if (!synchronized) return Result<Value>::failure(synchronized.status());
    if (firstFailure) return Result<Value>::failure(*firstFailure);
    Value::Object result;
    result["advances"]       = std::move(advances);
    result["completedCount"] = static_cast<std::int64_t>(synchronized.value());
    result["activeCount"]    = static_cast<std::int64_t>(abilities_.activeActivations().size());
    return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::cancelAbility(std::int64_t activationValue, std::int64_t tickValue) {
    if (activationValue <= 0 || tickValue < 0)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "ability activation or tick is invalid", "activation", {},
                                                        "action.squirrel"));
    const AbilityActivationId activationId(static_cast<std::uint64_t>(activationValue));
    const auto                active = abilities_.activeActivations();
    const auto                found =
        std::find_if(active.begin(), active.end(), [&](const auto& item) { return item.id == activationId; });
    if (found == active.end())
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "ability activation was not found", "activationId", {}, "action.squirrel"));
    auto cancelled =
        actions_.cancel(found->executionId, SimulationTick(static_cast<std::uint64_t>(tickValue)));
    if (!cancelled) return Result<Value>::failure(cancelled.status());
    auto synchronized = abilities_.synchronize();
    if (!synchronized) return Result<Value>::failure(synchronized.status());
    Value::Object result;
    result["activationId"] = activationValue;
    result["phase"]        = std::string("cancelled");
    return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::abilityGrant(std::int64_t grantValue) const {
    auto grant = parseGrant(grantValue);
    if (!grant) return Result<Value>::failure(grant.status());
    return abilityGrantValue(grant.value());
}

Result<Value> ScriptActionRuntime::matchingAbilities(const std::string& ownerId, const std::string& eventTag) const {
    if (!tags::isValidGameplayTagName(eventTag))
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "ability event tag is invalid", "eventTag", {}, "action.squirrel"));
    Value::Array result;
    for (const auto grant : abilities_.matchingGrants(ownerId, eventTag))
        result.emplace_back(static_cast<std::int64_t>(grant.value()));
    return Result<Value>::success(Value(std::move(result)));
}

Result<Value> ScriptActionRuntime::submitAction(const std::string& actionIdText, std::int64_t windupNs, std::int64_t activeNs,
                               std::int64_t recoverNs, std::int64_t tickValue) {
        auto actionId = LogicalId::parse(actionIdText);
        if (!actionId)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "action id is invalid", "actionId", {}, "action.squirrel"));
        if (tickValue < 0 || windupNs < 0 || activeNs < 0 || recoverNs < 0)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "action timing or tick is invalid", "timing", {},
                                                            "action.squirrel"));
        ActionDefinition definition;
        definition.id             = *actionId;
        definition.timing.windup  = Duration::fromNanoseconds(static_cast<std::uint64_t>(windupNs));
        definition.timing.active  = Duration::fromNanoseconds(static_cast<std::uint64_t>(activeNs));
        definition.timing.recover = Duration::fromNanoseconds(static_cast<std::uint64_t>(recoverNs));
        ActionRequest request;
        request.actionId      = *actionId;
        request.requestedTick = SimulationTick(static_cast<std::uint64_t>(tickValue));
        auto submitted        = actions_.submit(std::move(definition), std::move(request));
        if (!submitted) return Result<Value>::failure(submitted.status());
        Value::Object result;
        result["executionId"] = static_cast<std::int64_t>(submitted.value().value());
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

Result<Value> ScriptActionRuntime::advanceAction(std::int64_t executionValue, std::int64_t tickValue, double seconds) {
    if (executionValue <= 0 || tickValue < 0)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "action execution or tick is invalid", "executionId", {},
                                                        "action.squirrel"));
    auto delta = Duration::fromSeconds(seconds);
    if (!delta) return Result<Value>::failure(delta.status());
    auto advanced =
        actions_.advance(ActionExecutionId(static_cast<std::uint64_t>(executionValue)),
                         SimulationTick(static_cast<std::uint64_t>(tickValue)), delta.value());
    if (!advanced) return Result<Value>::failure(advanced.status());
    Value::Object result;
    result["executionId"]   = executionValue;
    result["phase"]         = std::string(actionPhaseName(advanced.value().phase));
    result["elapsedSeconds"] = advanced.value().totalElapsed.seconds();
    Value::Array transitions;
    for (const auto& transition : advanced.value().transitions) {
        Value::Object item;
        item["from"] = std::string(actionPhaseName(transition.from));
        item["to"]   = std::string(actionPhaseName(transition.to));
        item["tick"] = static_cast<std::int64_t>(transition.tick.value());
        transitions.emplace_back(std::move(item));
    }
    result["transitions"] = std::move(transitions);
    return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::cancelAction(std::int64_t executionValue, std::int64_t tickValue) {
    if (executionValue <= 0 || tickValue < 0)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "action execution or tick is invalid", "executionId", {},
                                                        "action.squirrel"));
    auto cancelled = actions_.cancel(ActionExecutionId(static_cast<std::uint64_t>(executionValue)),
                                     SimulationTick(static_cast<std::uint64_t>(tickValue)));
    if (!cancelled) return Result<Value>::failure(cancelled.status());
    Value::Object result;
    result["executionId"] = executionValue;
    result["phase"]       = std::string("cancelled");
    return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::findAction(std::int64_t executionValue) const {
    if (executionValue <= 0)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "action execution id must be positive", "executionId", {},
                                                        "action.squirrel"));
    const auto* execution = actions_.find(ActionExecutionId(static_cast<std::uint64_t>(executionValue)));
    if (!execution)
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "action execution was not found", "executionId", {}, "action.squirrel"));
    Value::Object result;
    result["executionId"]    = executionValue;
    result["definitionId"]   = execution->definition().id.format();
    result["phase"]          = std::string(actionPhaseName(execution->phase()));
    result["elapsedSeconds"] = execution->totalElapsed().seconds();
    result["phaseSeconds"]   = execution->phaseElapsed().seconds();
    return Result<Value>::success(Value(std::move(result)));
}

std::int64_t ScriptActionRuntime::executionCount() const { return static_cast<std::int64_t>(actions_.executionCount()); }

Result<Value> ScriptActionRuntime::setTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                     double cooldownSeconds, const std::string& instancingText,
                                     const std::string& groupText, const std::string& triggerTag, bool replace) {
        auto definitionId = LogicalId::parse(definitionText);
        if (!definitionId)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "ability definition id is invalid", "definitionId", {},
                                                            "action.squirrel"));
        auto decoded = Value::fromJson(timelineJson);
        if (!decoded) return Result<Value>::failure(decoded.status());
        auto timeline = ActionTimeline::fromValue(decoded.value());
        if (!timeline) return Result<Value>::failure(timeline.status());
        auto cooldown = Duration::fromSeconds(cooldownSeconds);
        if (!cooldown) return Result<Value>::failure(cooldown.status());

        AbilityDefinition definition;
        definition.id              = *definitionId;
        definition.action.id       = timeline.value().actionId;
        definition.action.timeline = timeline.value();
        definition.cooldown        = cooldown.value();
        if (instancingText == "non-instanced")
            definition.instancing = AbilityInstancingPolicy::NonInstanced;
        else if (instancingText == "per-owner")
            definition.instancing = AbilityInstancingPolicy::PerOwner;
        else if (instancingText == "per-execution")
            definition.instancing = AbilityInstancingPolicy::PerExecution;
        else
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "ability instancing policy is invalid", "instancing", {},
                                                            "action.squirrel"));
        if (groupText == "independent")
            definition.activationGroup = AbilityActivationGroup::Independent;
        else if (groupText == "exclusive-replaceable")
            definition.activationGroup = AbilityActivationGroup::ExclusiveReplaceable;
        else if (groupText == "exclusive-blocking")
            definition.activationGroup = AbilityActivationGroup::ExclusiveBlocking;
        else
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "ability activation group is invalid", "activationGroup",
                                                            {}, "action.squirrel"));
        if (!triggerTag.empty()) {
            if (!tags::isValidGameplayTagName(triggerTag))
                return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "ability trigger tag is invalid", "triggerTag", {},
                                                                "action.squirrel"));
            definition.triggers.push_back({triggerTag, tags::GameplayTagMatch::Exact});
        }

        const auto& splits = timeline.value().splitTimestamps;
        if (splits.empty()) {
            definition.action.timing.active = timeline.value().duration;
        } else if (splits.size() == 1) {
            definition.action.timing.windup = splits[0];
            definition.action.timing.active =
                Duration::fromNanoseconds(timeline.value().duration.nanoseconds() - splits[0].nanoseconds());
        } else {
            definition.action.timing.windup = splits[0];
            definition.action.timing.active =
                Duration::fromNanoseconds(splits[1].nanoseconds() - splits[0].nanoseconds());
            definition.action.timing.recover =
                Duration::fromNanoseconds(timeline.value().duration.nanoseconds() - splits[1].nanoseconds());
        }
        auto changed =
            replace ? abilities_.replaceDefinition(definition) : abilities_.registerDefinition(definition);
        if (!changed) return Result<Value>::failure(changed.status());
        abilityActions_[definitionText] = definition.action.id;
        Value::Object result;
        result["definitionId"]    = definitionText;
        result["actionId"]        = definition.action.id.format();
        result["durationSeconds"] = timeline.value().duration.seconds();
        result["sectionCount"]    = static_cast<std::int64_t>(timeline.value().sectionCount());
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

Result<AbilityGrantId> ScriptActionRuntime::parseGrant(std::int64_t value) {
    if (value <= 0)
        return Result<AbilityGrantId>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                 "ability grant id must be positive", "grantId", {},
                                                                 "action.squirrel"));
    return Result<AbilityGrantId>::success(AbilityGrantId(static_cast<std::uint64_t>(value)));
}

Result<Value> ScriptActionRuntime::abilityGrantValue(AbilityGrantId grantId) const {
    auto state = abilities_.findGrant(grantId);
    if (!state) return Result<Value>::failure(state.status());
    Value::Object result;
    result["grantId"]          = static_cast<std::int64_t>(state.value().grantId.value());
    result["ownerId"]          = state.value().ownerId;
    result["definitionId"]     = state.value().definitionId.format();
    result["cooldownSeconds"]  = state.value().cooldownRemaining.seconds();
    return Result<Value>::success(Value(std::move(result)));
}

Value ScriptActionRuntime::abilityActivationValue(const AbilityActivation& activation) {
    Value::Object result;
    result["activationId"] = static_cast<std::int64_t>(activation.id.value());
    result["grantId"]      = static_cast<std::int64_t>(activation.grantId.value());
    result["instanceId"]   = static_cast<std::int64_t>(activation.instanceId.value());
    result["executionId"]  = static_cast<std::int64_t>(activation.executionId.value());
    result["ownerId"]      = activation.ownerId;
    return Value(std::move(result));
}

}  // namespace eve::action
