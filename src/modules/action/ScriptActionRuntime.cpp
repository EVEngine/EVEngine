#include "action/ScriptActionRuntime.h"

#include "action/AbilityAsset.h"
#include "action/ActionTimeline.h"
#include "tags/GameplayTag.h"

#include <utility>

namespace eve::action {
namespace {

constexpr const char* kSrc = "action.squirrel";

Result<Value> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<Value>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kSrc));
}

Result<Value> applied(Value::Object object) {
    return Result<Value>::success(Value(std::move(object)), Status::success(StatusCode::Applied));
}

Result<Value> propagate(const Status& status) { return Result<Value>::failure(status); }

template <class T>
Result<Value> propagate(const Result<T>& result) {
    return Result<Value>::failure(result.status());
}

SimulationTick tickOf(std::int64_t tick) { return SimulationTick(static_cast<std::uint64_t>(tick)); }

ActionExecutionId executionOf(std::int64_t id) { return ActionExecutionId(static_cast<std::uint64_t>(id)); }

Value activationValue(const AbilityActivation& activation) {
    return Value(Value::Object{{"activationId", static_cast<std::int64_t>(activation.id.value())},
                               {"grantId", static_cast<std::int64_t>(activation.grantId.value())},
                               {"instanceId", static_cast<std::int64_t>(activation.instanceId.value())},
                               {"executionId", static_cast<std::int64_t>(activation.executionId.value())},
                               {"ownerId", activation.ownerId}});
}

Value stepReportValue(AbilityStepReport report) {
    Value::Array advances;
    advances.reserve(report.advances.size());
    for (const auto& item : report.advances) {
        advances.emplace_back(Value::Object{{"activationId", static_cast<std::int64_t>(item.activationId.value())},
                                            {"executionId", static_cast<std::int64_t>(item.executionId.value())},
                                            {"phase", std::string(actionPhaseName(item.phase))},
                                            {"elapsedSeconds", item.totalElapsed.seconds()}});
    }
    return Value(Value::Object{{"advances", std::move(advances)},
                               {"completedCount", static_cast<std::int64_t>(report.completedCount)},
                               {"activeCount", static_cast<std::int64_t>(report.activeCount)}});
}

}  // namespace

Result<Value> ScriptActionRuntime::registerAbilityJson(const std::string& abilityJson) {
    return setAbilityJson(abilityJson, false);
}

Result<Value> ScriptActionRuntime::replaceAbilityJson(const std::string& abilityJson) {
    return setAbilityJson(abilityJson, true);
}

Result<Value> ScriptActionRuntime::registerTimelineAbility(const std::string& definitionText,
                                                           const std::string& timelineJson, float cooldownSeconds,
                                                           const std::string& instancingText,
                                                           const std::string& groupText,
                                                           const std::string& triggerTag) {
    return setTimelineAbility(definitionText, timelineJson, cooldownSeconds, instancingText, groupText, triggerTag,
                              false);
}

Result<Value> ScriptActionRuntime::replaceTimelineAbility(const std::string& definitionText,
                                                          const std::string& timelineJson, float cooldownSeconds,
                                                          const std::string& instancingText,
                                                          const std::string& groupText, const std::string& triggerTag) {
    return setTimelineAbility(definitionText, timelineJson, cooldownSeconds, instancingText, groupText, triggerTag,
                              true);
}

Result<void> ScriptActionRuntime::registerAbilityDefinition(AbilityDefinition definition) {
    return abilities_.registerDefinition(std::move(definition));
}

Result<void> ScriptActionRuntime::replaceAbilityDefinition(AbilityDefinition definition) {
    return abilities_.replaceDefinition(std::move(definition));
}

Result<Value> ScriptActionRuntime::setAbilityJson(const std::string& abilityJson, bool replace) {
    auto decoded = Value::fromJson(abilityJson);
    if (!decoded) return propagate(decoded);
    auto definition = decodeAbilityAsset(decoded.value());
    if (!definition) return propagate(definition);
    const std::string definitionId = definition.value().id.format();
    const LogicalId   actionId     = definition.value().action.id;
    auto              changed      = replace ? abilities_.replaceDefinition(std::move(definition).takeValue())
                                             : abilities_.registerDefinition(std::move(definition).takeValue());
    if (!changed) return propagate(changed);
    return applied({{"definitionId", definitionId}, {"actionId", actionId.format()}});
}

Result<Value> ScriptActionRuntime::setTimelineAbility(const std::string& definitionText,
                                                      const std::string& timelineJson, float cooldownSeconds,
                                                      const std::string& instancingText, const std::string& groupText,
                                                      const std::string& triggerTag, bool replace) {
    auto definitionId = LogicalId::parse(definitionText);
    if (!definitionId) return fail(DiagnosticCode::InvalidArgument, "ability definition id is invalid", "definitionId");
    auto decoded = Value::fromJson(timelineJson);
    if (!decoded) return propagate(decoded);
    auto timeline = ActionTimeline::fromValue(decoded.value());
    if (!timeline) return propagate(timeline);
    auto cooldown = Duration::fromSeconds(cooldownSeconds);
    if (!cooldown) return propagate(cooldown);
    auto instancing = parseAbilityInstancingPolicy(instancingText);
    if (!instancing) return propagate(instancing);
    auto group = parseAbilityActivationGroup(groupText);
    if (!group) return propagate(group);
    auto definition = abilityDefinitionFromTimeline(*definitionId, std::move(timeline).takeValue(), cooldown.value(),
                                                    instancing.value(), group.value(), triggerTag);
    if (!definition) return propagate(definition);
    const LogicalId actionId = definition.value().action.id;
    const double    duration =
        definition.value().action.timeline ? definition.value().action.timeline->duration.seconds() : 0.0;
    const auto sectionCount =
        definition.value().action.timeline ? definition.value().action.timeline->sectionCount() : 0u;
    auto changed = replace ? abilities_.replaceDefinition(std::move(definition).takeValue())
                           : abilities_.registerDefinition(std::move(definition).takeValue());
    if (!changed) return propagate(changed);
    return applied({{"definitionId", definitionText},
                    {"actionId", actionId.format()},
                    {"durationSeconds", duration},
                    {"sectionCount", static_cast<std::int64_t>(sectionCount)}});
}

Result<Value> ScriptActionRuntime::grantAbility(const std::string& ownerId, const std::string& definitionText) {
    auto definitionId = LogicalId::parse(definitionText);
    if (!definitionId) return fail(DiagnosticCode::InvalidArgument, "ability definition id is invalid", "definitionId");
    auto granted = abilities_.grant(ownerId, *definitionId);
    if (!granted) return propagate(granted);
    return abilityGrantValue(granted.value());
}

Result<Value> ScriptActionRuntime::revokeAbility(std::int64_t grantValue) {
    auto grant = parseGrant(grantValue);
    if (!grant) return propagate(grant);
    auto revoked = abilities_.revoke(grant.value());
    if (!revoked) return propagate(revoked);
    return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::activateAbility(std::int64_t grantValue, std::int64_t tickValue) {
    auto grant = parseGrant(grantValue);
    if (!grant) return propagate(grant);
    if (tickValue < 0) return fail(DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick");
    auto activated = abilities_.activate(grant.value(), tickOf(tickValue));
    if (!activated) return propagate(activated);
    return Result<Value>::success(activationValue(activated.value()), Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::advanceAbilities(std::int64_t tickValue, float seconds) {
    if (tickValue < 0) return fail(DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick");
    auto delta = Duration::fromSeconds(seconds);
    if (!delta) return propagate(delta);
    auto stepped = abilities_.step(tickOf(tickValue), delta.value());
    if (!stepped) return propagate(stepped);
    return Result<Value>::success(stepReportValue(std::move(stepped).takeValue()),
                                  Status::success(StatusCode::Applied));
}

Result<Value> ScriptActionRuntime::cancelAbility(std::int64_t activationValue, std::int64_t tickValue) {
    if (activationValue <= 0 || tickValue < 0)
        return fail(DiagnosticCode::InvalidArgument, "ability activation or tick is invalid", "activation");
    auto cancelled =
        abilities_.cancel(AbilityActivationId(static_cast<std::uint64_t>(activationValue)), tickOf(tickValue));
    if (!cancelled) return propagate(cancelled);
    return applied({{"activationId", activationValue}, {"phase", std::string("cancelled")}});
}

Result<Value> ScriptActionRuntime::abilityGrant(std::int64_t grantValue) const {
    auto grant = parseGrant(grantValue);
    if (!grant) return propagate(grant);
    return abilityGrantValue(grant.value());
}

Result<Value> ScriptActionRuntime::matchingAbilities(const std::string& ownerId, const std::string& eventTag) const {
    if (!tags::isValidGameplayTagName(eventTag))
        return fail(DiagnosticCode::InvalidArgument, "ability event tag is invalid", "eventTag");
    Value::Array result;
    for (const auto grant : abilities_.matchingGrants(ownerId, eventTag))
        result.emplace_back(static_cast<std::int64_t>(grant.value()));
    return Result<Value>::success(Value(std::move(result)));
}

Result<Value> ScriptActionRuntime::submitAction(const std::string& actionIdText, std::int64_t windupNs,
                                                std::int64_t activeNs, std::int64_t recoverNs, std::int64_t tickValue) {
    auto actionId = LogicalId::parse(actionIdText);
    if (!actionId) return fail(DiagnosticCode::InvalidArgument, "action id is invalid", "actionId");
    if (tickValue < 0 || windupNs < 0 || activeNs < 0 || recoverNs < 0)
        return fail(DiagnosticCode::InvalidArgument, "action timing or tick is invalid", "timing");
    ActionDefinition definition;
    definition.id             = *actionId;
    definition.timing.windup  = Duration::fromNanoseconds(static_cast<std::uint64_t>(windupNs));
    definition.timing.active  = Duration::fromNanoseconds(static_cast<std::uint64_t>(activeNs));
    definition.timing.recover = Duration::fromNanoseconds(static_cast<std::uint64_t>(recoverNs));
    ActionRequest request;
    request.actionId      = *actionId;
    request.requestedTick = tickOf(tickValue);
    auto submitted        = actions_.submit(std::move(definition), std::move(request));
    if (!submitted) return propagate(submitted);
    return applied({{"executionId", static_cast<std::int64_t>(submitted.value().value())}});
}

Result<Value> ScriptActionRuntime::advanceAction(std::int64_t executionValue, std::int64_t tickValue, float seconds) {
    if (executionValue <= 0 || tickValue < 0)
        return fail(DiagnosticCode::InvalidArgument, "action execution or tick is invalid", "executionId");
    auto delta = Duration::fromSeconds(seconds);
    if (!delta) return propagate(delta);
    auto advanced = actions_.advance(executionOf(executionValue), tickOf(tickValue), delta.value());
    if (!advanced) return propagate(advanced);
    Value::Array transitions;
    for (const auto& transition : advanced.value().transitions) {
        transitions.emplace_back(Value::Object{{"from", std::string(actionPhaseName(transition.from))},
                                               {"to", std::string(actionPhaseName(transition.to))},
                                               {"tick", static_cast<std::int64_t>(transition.tick.value())}});
    }
    return applied({{"executionId", executionValue},
                    {"phase", std::string(actionPhaseName(advanced.value().phase))},
                    {"elapsedSeconds", advanced.value().totalElapsed.seconds()},
                    {"transitions", std::move(transitions)}});
}

Result<Value> ScriptActionRuntime::cancelAction(std::int64_t executionValue, std::int64_t tickValue) {
    if (executionValue <= 0 || tickValue < 0)
        return fail(DiagnosticCode::InvalidArgument, "action execution or tick is invalid", "executionId");
    auto cancelled = actions_.cancel(executionOf(executionValue), tickOf(tickValue));
    if (!cancelled) return propagate(cancelled);
    return applied({{"executionId", executionValue}, {"phase", std::string("cancelled")}});
}

Result<Value> ScriptActionRuntime::findAction(std::int64_t executionValue) const {
    if (executionValue <= 0)
        return fail(DiagnosticCode::InvalidArgument, "action execution id must be positive", "executionId");
    const auto* execution = actions_.find(executionOf(executionValue));
    if (!execution) return fail(DiagnosticCode::NotFound, "action execution was not found", "executionId");
    return Result<Value>::success(Value(Value::Object{{"executionId", executionValue},
                                                      {"definitionId", execution->definition().id.format()},
                                                      {"phase", std::string(actionPhaseName(execution->phase()))},
                                                      {"elapsedSeconds", execution->totalElapsed().seconds()},
                                                      {"phaseSeconds", execution->phaseElapsed().seconds()}}));
}

std::int64_t ScriptActionRuntime::executionCount() const {
    return static_cast<std::int64_t>(actions_.executionCount());
}

Result<AbilityGrantId> ScriptActionRuntime::parseGrant(std::int64_t value) {
    if (value <= 0)
        return Result<AbilityGrantId>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "ability grant id must be positive", "grantId", {}, kSrc));
    return Result<AbilityGrantId>::success(AbilityGrantId(static_cast<std::uint64_t>(value)));
}

Result<Value> ScriptActionRuntime::abilityGrantValue(AbilityGrantId grantId) const {
    auto state = abilities_.findGrant(grantId);
    if (!state) return propagate(state);
    return Result<Value>::success(
        Value(Value::Object{{"grantId", static_cast<std::int64_t>(state.value().grantId.value())},
                            {"ownerId", state.value().ownerId},
                            {"definitionId", state.value().definitionId.format()},
                            {"cooldownSeconds", state.value().cooldownRemaining.seconds()}}));
}

}  // namespace eve::action
