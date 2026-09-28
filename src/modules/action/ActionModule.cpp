#include "action/ActionModule.h"

#include "action/AbilityAsset.h"
#include "action/AbilitySystem.h"
#include "action/ActionTimeline.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "tags/GameplayTag.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace eve::action {
namespace {

class ScriptActionRuntime {
public:
    Result<Value> registerAbilityJson(const std::string& abilityJson) {
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

    Result<Value> replaceAbilityJson(const std::string& abilityJson) {
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

    Result<Value> registerTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                          double cooldownSeconds, const std::string& instancingText,
                                          const std::string& groupText, const std::string& triggerTag) {
        return setTimelineAbility(definitionText, timelineJson, cooldownSeconds, instancingText, groupText,
                                  triggerTag, false);
    }

    Result<Value> replaceTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                         double cooldownSeconds, const std::string& instancingText,
                                         const std::string& groupText, const std::string& triggerTag) {
        return setTimelineAbility(definitionText, timelineJson, cooldownSeconds, instancingText, groupText,
                                  triggerTag, true);
    }

    Result<Value> grantAbility(const std::string& ownerId, const std::string& definitionText) {
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

    Result<Value> revokeAbility(std::int64_t grantValue) {
        auto grant = parseGrant(grantValue);
        if (!grant) return Result<Value>::failure(grant.status());
        auto revoked = abilities_.revoke(grant.value());
        if (!revoked) return Result<Value>::failure(revoked.status());
        return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
    }

    Result<Value> activateAbility(std::int64_t grantValue, std::int64_t tickValue) {
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

    Result<Value> advanceAbilities(std::int64_t tickValue, double seconds) {
        if (tickValue < 0)
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick", {}, "action.squirrel"));
        auto delta = Duration::fromSeconds(seconds);
        if (!delta) return Result<Value>::failure(delta.status());
        Value::Array advances;
        for (const auto& activation : abilities_.activeActivations()) {
            auto advanced = actions_.advance(activation.executionId,
                                             SimulationTick(static_cast<std::uint64_t>(tickValue)), delta.value());
            if (!advanced) return Result<Value>::failure(advanced.status());
            Value::Object value;
            value["activationId"]   = static_cast<std::int64_t>(activation.id.value());
            value["executionId"]    = static_cast<std::int64_t>(activation.executionId.value());
            value["phase"]          = std::string(actionPhaseName(advanced.value().phase));
            value["elapsedSeconds"] = advanced.value().totalElapsed.seconds();
            advances.emplace_back(std::move(value));
        }
        auto cooled = abilities_.advanceCooldowns(delta.value());
        if (!cooled) return Result<Value>::failure(cooled.status());
        auto synchronized = abilities_.synchronize();
        if (!synchronized) return Result<Value>::failure(synchronized.status());
        Value::Object result;
        result["advances"]       = std::move(advances);
        result["completedCount"] = static_cast<std::int64_t>(synchronized.value());
        result["activeCount"]    = static_cast<std::int64_t>(abilities_.activeActivations().size());
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> cancelAbility(std::int64_t activationValue, std::int64_t tickValue) {
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

    Result<Value> abilityGrant(std::int64_t grantValue) const {
        auto grant = parseGrant(grantValue);
        if (!grant) return Result<Value>::failure(grant.status());
        return abilityGrantValue(grant.value());
    }

    Result<Value> matchingAbilities(const std::string& ownerId, const std::string& eventTag) const {
        if (!tags::isValidGameplayTagName(eventTag))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "ability event tag is invalid", "eventTag", {}, "action.squirrel"));
        Value::Array result;
        for (const auto grant : abilities_.matchingGrants(ownerId, eventTag))
            result.emplace_back(static_cast<std::int64_t>(grant.value()));
        return Result<Value>::success(Value(std::move(result)));
    }

    Result<Value> submitAction(const std::string& actionIdText, std::int64_t windupNs, std::int64_t activeNs,
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

    Result<Value> advanceAction(std::int64_t executionValue, std::int64_t tickValue, double seconds) {
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

    Result<Value> cancelAction(std::int64_t executionValue, std::int64_t tickValue) {
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

    Result<Value> findAction(std::int64_t executionValue) const {
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

    std::int64_t executionCount() const { return static_cast<std::int64_t>(actions_.executionCount()); }

private:
    Result<Value> setTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
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

    static Result<AbilityGrantId> parseGrant(std::int64_t value) {
        if (value <= 0)
            return Result<AbilityGrantId>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "ability grant id must be positive", "grantId", {},
                                                                     "action.squirrel"));
        return Result<AbilityGrantId>::success(AbilityGrantId(static_cast<std::uint64_t>(value)));
    }

    Result<Value> abilityGrantValue(AbilityGrantId grantId) const {
        auto state = abilities_.findGrant(grantId);
        if (!state) return Result<Value>::failure(state.status());
        Value::Object result;
        result["grantId"]          = static_cast<std::int64_t>(state.value().grantId.value());
        result["ownerId"]          = state.value().ownerId;
        result["definitionId"]     = state.value().definitionId.format();
        result["cooldownSeconds"]  = state.value().cooldownRemaining.seconds();
        return Result<Value>::success(Value(std::move(result)));
    }

    static Value abilityActivationValue(const AbilityActivation& activation) {
        Value::Object result;
        result["activationId"] = static_cast<std::int64_t>(activation.id.value());
        result["grantId"]      = static_cast<std::int64_t>(activation.grantId.value());
        result["instanceId"]   = static_cast<std::int64_t>(activation.instanceId.value());
        result["executionId"]  = static_cast<std::int64_t>(activation.executionId.value());
        result["ownerId"]      = activation.ownerId;
        return Value(std::move(result));
    }

    ActionRuntime                             actions_;
    AbilityRuntime                            abilities_{actions_};
    std::map<std::string, LogicalId, std::less<>> abilityActions_;
};

ssq::Table project(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}

ssq::Table nullRuntime(HSQUIRRELVM vm) {
    return project(vm, Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "action runtime must not be null", "runtime", {},
                                                                "action.squirrel")));
}

ssq::Table newRuntime(HSQUIRRELVM vm) {
    auto runtime = std::make_unique<ScriptActionRuntime>();
    auto object  = script::makeOwnedSquirrelInstance<ScriptActionRuntime>(vm, std::move(runtime));
    if (!object) return script::projectStatusResult(vm, object.status());
    auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied));
    result.set("value", std::move(object).takeValue());
    result.set("ownership", std::string("owned"));
    return result;
}

}  // namespace

Module_IMPL(Action, new Action());

void Action::expose(ssq::Table& table) {
    const HSQUIRRELVM vm      = table.getHandle();
    auto              runtime = table.addClass<ScriptActionRuntime>(
        "ActionRuntime", std::function<ScriptActionRuntime*()>([] { return nullptr; }), true);
    runtime.addFunc("ownership", [](ScriptActionRuntime*) { return std::string("owned"); });
    runtime.addFunc("registerAbilityJson", [vm](ScriptActionRuntime* self, const std::string& json) {
        return self ? project(vm, self->registerAbilityJson(json)) : nullRuntime(vm);
    });
    runtime.addFunc("replaceAbilityJson", [vm](ScriptActionRuntime* self, const std::string& json) {
        return self ? project(vm, self->replaceAbilityJson(json)) : nullRuntime(vm);
    });
    runtime.addFunc("registerTimelineAbility",
                    [vm](ScriptActionRuntime* self, const std::string& definitionId, const std::string& timelineJson,
                         float cooldownSeconds, const std::string& instancing, const std::string& activationGroup,
                         const std::string& triggerTag) {
                        return self ? project(vm, self->registerTimelineAbility(definitionId, timelineJson,
                                                                                cooldownSeconds, instancing,
                                                                                activationGroup, triggerTag))
                                    : nullRuntime(vm);
                    });
    runtime.addFunc("replaceTimelineAbility",
                    [vm](ScriptActionRuntime* self, const std::string& definitionId, const std::string& timelineJson,
                         float cooldownSeconds, const std::string& instancing, const std::string& activationGroup,
                         const std::string& triggerTag) {
                        return self ? project(vm, self->replaceTimelineAbility(definitionId, timelineJson,
                                                                               cooldownSeconds, instancing,
                                                                               activationGroup, triggerTag))
                                    : nullRuntime(vm);
                    });
    runtime.addFunc("grantAbility",
                    [vm](ScriptActionRuntime* self, const std::string& owner, const std::string& definitionId) {
                        return self ? project(vm, self->grantAbility(owner, definitionId)) : nullRuntime(vm);
                    });
    runtime.addFunc("revokeAbility", [vm](ScriptActionRuntime* self, std::int64_t grantId) {
        return self ? project(vm, self->revokeAbility(grantId)) : nullRuntime(vm);
    });
    runtime.addFunc("activateAbility", [vm](ScriptActionRuntime* self, std::int64_t grantId, std::int64_t tick) {
        return self ? project(vm, self->activateAbility(grantId, tick)) : nullRuntime(vm);
    });
    runtime.addFunc("advanceAbilities", [vm](ScriptActionRuntime* self, std::int64_t tick, float seconds) {
        return self ? project(vm, self->advanceAbilities(tick, seconds)) : nullRuntime(vm);
    });
    runtime.addFunc("cancelAbility", [vm](ScriptActionRuntime* self, std::int64_t activationId, std::int64_t tick) {
        return self ? project(vm, self->cancelAbility(activationId, tick)) : nullRuntime(vm);
    });
    runtime.addFunc("abilityGrant", [vm](ScriptActionRuntime* self, std::int64_t grantId) {
        return self ? project(vm, self->abilityGrant(grantId)) : nullRuntime(vm);
    });
    runtime.addFunc("matchingAbilities",
                    [vm](ScriptActionRuntime* self, const std::string& owner, const std::string& eventTag) {
                        return self ? project(vm, self->matchingAbilities(owner, eventTag)) : nullRuntime(vm);
                    });
    runtime.addFunc("submitAction", [vm](ScriptActionRuntime* self, const std::string& actionId, std::int64_t windupNs,
                                         std::int64_t activeNs, std::int64_t recoverNs, std::int64_t tick) {
        return self ? project(vm, self->submitAction(actionId, windupNs, activeNs, recoverNs, tick))
                    : nullRuntime(vm);
    });
    runtime.addFunc("advanceAction",
                    [vm](ScriptActionRuntime* self, std::int64_t executionId, std::int64_t tick, float seconds) {
                        return self ? project(vm, self->advanceAction(executionId, tick, seconds)) : nullRuntime(vm);
                    });
    runtime.addFunc("cancelAction", [vm](ScriptActionRuntime* self, std::int64_t executionId, std::int64_t tick) {
        return self ? project(vm, self->cancelAction(executionId, tick)) : nullRuntime(vm);
    });
    runtime.addFunc("findAction", [vm](ScriptActionRuntime* self, std::int64_t executionId) {
        return self ? project(vm, self->findAction(executionId)) : nullRuntime(vm);
    });
    runtime.addFunc("executionCount",
                    [](ScriptActionRuntime* self) { return self ? self->executionCount() : std::int64_t{0}; });

    auto module = table.addClass(name, Action::create, false);
    module.addFunc("getName", &Action::getName);
    module.addFunc("newRuntime", [vm](Action*) { return newRuntime(vm); });
}

void Action::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Action::getName);
    cls.addFunc("newRuntime", [vm = cls.getHandle()](Action*) { return newRuntime(vm); });
}

}  // namespace eve::action
