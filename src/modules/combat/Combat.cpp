#include "combat/Combat.h"

#include "action/ActionDamageBlock.h"
#include "action/AbilitySystem.h"
#include "combat/CombatLocomotion.h"
#include "combat/Damage.h"
#include "combat/StandardAbilities.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace eve::combat {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "combat.squirrel"));
}

Result<SubjectRef> parseSubject(const std::string& text, std::string path) {
    const auto parsed = PersistentId::parse(text);
    if (!parsed)
        return failure<SubjectRef>(DiagnosticCode::InvalidArgument, "combat subject must be a UUID",
                                   std::move(path));
    return Result<SubjectRef>::success(SubjectRef::fromPersistentId(*parsed));
}

Value vectorValue(CombatVector2 value) {
    Value::Object result;
    result["x"] = value.x;
    result["z"] = value.z;
    return Value(std::move(result));
}

std::string reactionName(HitReaction reaction) {
    switch (reaction) {
        case HitReaction::None: return "none";
        case HitReaction::Flinch: return "flinch";
        case HitReaction::Stagger: return "stagger";
        case HitReaction::Knockdown: return "knockdown";
        case HitReaction::Death: return "death";
    }
    return "none";
}

class ScriptCombatRuntime {
public:
    ScriptCombatRuntime() : locomotion_(navigation_) {}

    Result<void> initializeAbilities() {
        auto definitions = standardCombatAbilities();
        if (!definitions) return Result<void>::failure(definitions.status());
        for (auto& definition : definitions.value()) {
            const std::string definitionId = definition.id.format();
            abilityActions_[definitionId]  = definition.action.id;
            auto registered = abilities_.registerDefinition(std::move(definition));
            if (!registered) return registered;
        }
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    Result<Value> registerFighter(const std::string& subjectText, const std::string& ownerId,
                                  double x, double z, double health, double poise,
                                  double maximumSpeed, double acceleration) {
        auto subject = parseSubject(subjectText, "subject");
        if (!subject) return Result<Value>::failure(subject.status());
        CombatState combatState{subject.value(), health, health, poise, poise};
        auto valid = combatState.validate();
        if (!valid) return Result<Value>::failure(valid.status());
        if (states_.contains(subjectText))
            return failure<Value>(DiagnosticCode::AlreadyExists, "combat fighter already exists", subjectText);
        auto registered = locomotion_.registerSubject(
            {subject.value(), ownerId, {x, z}, maximumSpeed, acceleration});
        if (!registered) return Result<Value>::failure(registered.status());
        states_.emplace(subjectText, std::move(combatState));
        return state(subjectText);
    }

    Result<Value> setMoveIntent(const std::string& subjectText, double x, double z,
                                double speedFraction) {
        auto subject = parseSubject(subjectText, "subject");
        if (!subject) return Result<Value>::failure(subject.status());
        auto changed = locomotion_.setMoveIntent(subject.value(), {x, z}, speedFraction);
        if (!changed) return Result<Value>::failure(changed.status());
        return state(subjectText);
    }

    Result<Value> navigateTo(const std::string& subjectText, double x, double z,
                             double acceptanceRadius) {
        auto subject = parseSubject(subjectText, "subject");
        if (!subject) return Result<Value>::failure(subject.status());
        auto changed = locomotion_.navigateTo(subject.value(), {{x, z}, acceptanceRadius});
        if (!changed) return Result<Value>::failure(changed.status());
        return state(subjectText);
    }

    Result<Value> stop(const std::string& subjectText) {
        auto subject = parseSubject(subjectText, "subject");
        if (!subject) return Result<Value>::failure(subject.status());
        auto stopped = locomotion_.stop(subject.value());
        if (!stopped) return Result<Value>::failure(stopped.status());
        return state(subjectText);
    }

    Result<Value> advance(std::int64_t tick, double seconds) {
        if (tick < 0)
            return failure<Value>(DiagnosticCode::InvalidArgument, "combat tick must be non-negative", "tick");
        auto delta = Duration::fromSeconds(seconds);
        if (!delta) return Result<Value>::failure(delta.status());
        auto advanced = locomotion_.advance(
            {SimulationTick(static_cast<std::uint64_t>(tick)), std::move(delta).takeValue()});
        if (!advanced) return Result<Value>::failure(advanced.status());
        Value::Array events;
        events.reserve(advanced.value().events.size());
        for (const auto& event : advanced.value().events) {
            Value::Object value;
            value["subject"] = event.subject.format();
            value["kind"] = event.kind == CombatLocomotionEventKind::Arrived ? "arrived" : "moved";
            value["position"] = vectorValue(event.position);
            events.emplace_back(std::move(value));
        }
        Value::Object result;
        result["tick"] = tick;
        result["events"] = std::move(events);
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> applyDamage(const std::string& sourceText, const std::string& targetText,
                              const std::string& damageType, double healthDamage,
                              double poiseDamage, double knockbackX, double knockbackY,
                              double knockbackZ) {
        auto source = parseSubject(sourceText, "source");
        if (!source) return Result<Value>::failure(source.status());
        auto target = parseSubject(targetText, "target");
        if (!target) return Result<Value>::failure(target.status());
        const auto found = states_.find(targetText);
        if (found == states_.end())
            return failure<Value>(DiagnosticCode::NotFound, "combat target was not found", targetText);
        DamageRequest request;
        request.source       = source.value();
        request.target       = target.value();
        request.damageType   = damageType;
        request.healthDamage = healthDamage;
        request.poiseDamage  = poiseDamage;
        request.knockback    = {knockbackX, knockbackY, knockbackZ};
        auto outcome = damage_.apply(found->second, request);
        if (!outcome) return Result<Value>::failure(outcome.status());
        Value::Object result;
        result["source"] = outcome.value().source.format();
        result["target"] = outcome.value().target.format();
        result["health"] = outcome.value().health;
        result["poise"] = outcome.value().poise;
        result["appliedHealthDamage"] = outcome.value().appliedHealthDamage;
        result["appliedPoiseDamage"] = outcome.value().appliedPoiseDamage;
        result["reaction"] = reactionName(outcome.value().reaction);
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> applyTimelineDamage(const std::string& sourceText, const std::string& targetText,
                                      const std::string& payloadJson) {
        auto decoded = Value::fromJson(payloadJson);
        if (!decoded) return Result<Value>::failure(decoded.status());
        const auto* payload = decoded.value().getIf<Value::Object>();
        if (!payload)
            return failure<Value>(DiagnosticCode::InvalidArgument,
                                  "combat timeline damage payload must be an object", "payload");
        auto binding = action::ActionDamageBinding::fromPayload(*payload);
        if (!binding) return Result<Value>::failure(binding.status());
        return applyDamage(sourceText, targetText, binding.value().damageType, binding.value().amount,
                           binding.value().poiseAmount, binding.value().knockback.x,
                           binding.value().knockback.y, binding.value().knockback.z);
    }

    Result<Value> grantAbility(const std::string& ownerId, const std::string& definitionText) {
        auto definitionId = LogicalId::parse(definitionText);
        if (!definitionId)
            return failure<Value>(DiagnosticCode::InvalidArgument, "ability definition id is invalid",
                                  "definitionId");
        const auto action = abilityActions_.find(definitionText);
        if (action == abilityActions_.end())
            return failure<Value>(DiagnosticCode::NotFound, "ability definition is not registered",
                                  definitionText);
        auto granted = abilities_.grant(ownerId, *definitionId);
        if (!granted) return Result<Value>::failure(granted.status());
        grantedActions_[granted.value().value()] = action->second;
        return abilityGrantValue(granted.value());
    }

    Result<Value> registerTimelineAbility(const std::string& definitionText,
                                          const std::string& timelineJson, double cooldownSeconds,
                                          const std::string& instancingText,
                                          const std::string& groupText,
                                          const std::string& triggerTag) {
        auto definitionId = LogicalId::parse(definitionText);
        if (!definitionId)
            return failure<Value>(DiagnosticCode::InvalidArgument, "ability definition id is invalid",
                                  "definitionId");
        auto decoded = Value::fromJson(timelineJson);
        if (!decoded) return Result<Value>::failure(decoded.status());
        auto timeline = action::ActionTimeline::fromValue(decoded.value());
        if (!timeline) return Result<Value>::failure(timeline.status());
        auto cooldown = Duration::fromSeconds(cooldownSeconds);
        if (!cooldown) return Result<Value>::failure(cooldown.status());

        action::AbilityDefinition definition;
        definition.id                  = *definitionId;
        definition.action.id           = timeline.value().actionId;
        definition.action.timeline     = timeline.value();
        definition.cooldown            = cooldown.value();
        if (instancingText == "non-instanced")
            definition.instancing = action::AbilityInstancingPolicy::NonInstanced;
        else if (instancingText == "per-owner")
            definition.instancing = action::AbilityInstancingPolicy::PerOwner;
        else if (instancingText == "per-execution")
            definition.instancing = action::AbilityInstancingPolicy::PerExecution;
        else
            return failure<Value>(DiagnosticCode::InvalidArgument, "ability instancing policy is invalid",
                                  "instancing");
        if (groupText == "independent")
            definition.activationGroup = action::AbilityActivationGroup::Independent;
        else if (groupText == "exclusive-replaceable")
            definition.activationGroup = action::AbilityActivationGroup::ExclusiveReplaceable;
        else if (groupText == "exclusive-blocking")
            definition.activationGroup = action::AbilityActivationGroup::ExclusiveBlocking;
        else
            return failure<Value>(DiagnosticCode::InvalidArgument, "ability activation group is invalid",
                                  "activationGroup");
        if (!triggerTag.empty()) {
            if (!tags::isValidGameplayTagName(triggerTag))
                return failure<Value>(DiagnosticCode::InvalidArgument, "ability trigger tag is invalid",
                                      "triggerTag");
            definition.triggers.push_back({triggerTag, tags::GameplayTagMatch::Exact});
        }

        const auto& splits = timeline.value().splitTimestamps;
        if (splits.empty()) {
            definition.action.timing.active = timeline.value().duration;
        } else if (splits.size() == 1) {
            definition.action.timing.windup = splits[0];
            definition.action.timing.active = Duration::fromNanoseconds(
                timeline.value().duration.nanoseconds() - splits[0].nanoseconds());
        } else {
            definition.action.timing.windup = splits[0];
            definition.action.timing.active = Duration::fromNanoseconds(
                splits[1].nanoseconds() - splits[0].nanoseconds());
            definition.action.timing.recover = Duration::fromNanoseconds(
                timeline.value().duration.nanoseconds() - splits[1].nanoseconds());
        }
        auto registered = abilities_.registerDefinition(definition);
        if (!registered) return Result<Value>::failure(registered.status());
        abilityActions_[definitionText] = definition.action.id;
        Value::Object result;
        result["definitionId"] = definitionText;
        result["actionId"] = definition.action.id.format();
        result["durationSeconds"] = timeline.value().duration.seconds();
        result["sectionCount"] = static_cast<std::int64_t>(timeline.value().sectionCount());
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> activateAbility(std::int64_t grantValue, std::int64_t tickValue) {
        auto grant = parseGrant(grantValue);
        if (!grant) return Result<Value>::failure(grant.status());
        if (tickValue < 0)
            return failure<Value>(DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick");
        const auto action = grantedActions_.find(grant.value().value());
        if (action == grantedActions_.end())
            return failure<Value>(DiagnosticCode::NotFound, "ability grant action is unavailable", "grantId");
        action::ActionRequest request;
        request.actionId      = action->second;
        request.requestedTick = SimulationTick(static_cast<std::uint64_t>(tickValue));
        const auto tick = request.requestedTick;
        auto activated = abilities_.activate(grant.value(), std::move(request), tick);
        if (!activated) return Result<Value>::failure(activated.status());
        return Result<Value>::success(abilityActivationValue(activated.value()),
                                      Status::success(StatusCode::Applied));
    }

    Result<Value> advanceAbilities(std::int64_t tickValue, double seconds) {
        if (tickValue < 0)
            return failure<Value>(DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick");
        auto delta = Duration::fromSeconds(seconds);
        if (!delta) return Result<Value>::failure(delta.status());
        Value::Array advances;
        for (const auto& activation : abilities_.activeActivations()) {
            auto advanced = actions_.advance(activation.executionId,
                                             SimulationTick(static_cast<std::uint64_t>(tickValue)), delta.value());
            if (!advanced) return Result<Value>::failure(advanced.status());
            Value::Object value;
            value["activationId"] = static_cast<std::int64_t>(activation.id.value());
            value["executionId"] = static_cast<std::int64_t>(activation.executionId.value());
            value["phase"] = std::string(action::actionPhaseName(advanced.value().phase));
            value["elapsedSeconds"] = advanced.value().totalElapsed.seconds();
            advances.emplace_back(std::move(value));
        }
        auto cooled = abilities_.advanceCooldowns(delta.value());
        if (!cooled) return Result<Value>::failure(cooled.status());
        auto synchronized = abilities_.synchronize();
        if (!synchronized) return Result<Value>::failure(synchronized.status());
        Value::Object result;
        result["advances"] = std::move(advances);
        result["completedCount"] = static_cast<std::int64_t>(synchronized.value());
        result["activeCount"] = static_cast<std::int64_t>(abilities_.activeActivations().size());
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> cancelAbility(std::int64_t activationValue, std::int64_t tickValue) {
        if (activationValue <= 0 || tickValue < 0)
            return failure<Value>(DiagnosticCode::InvalidArgument, "ability activation or tick is invalid",
                                  "activation");
        const action::AbilityActivationId activationId(static_cast<std::uint64_t>(activationValue));
        const auto active = abilities_.activeActivations();
        const auto found = std::find_if(active.begin(), active.end(),
                                        [&](const auto& item) { return item.id == activationId; });
        if (found == active.end())
            return failure<Value>(DiagnosticCode::NotFound, "ability activation was not found", "activationId");
        auto cancelled = actions_.cancel(found->executionId,
                                         SimulationTick(static_cast<std::uint64_t>(tickValue)));
        if (!cancelled) return Result<Value>::failure(cancelled.status());
        auto synchronized = abilities_.synchronize();
        if (!synchronized) return Result<Value>::failure(synchronized.status());
        Value::Object result;
        result["activationId"] = activationValue;
        result["phase"] = std::string("cancelled");
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> abilityGrant(std::int64_t grantValue) const {
        auto grant = parseGrant(grantValue);
        if (!grant) return Result<Value>::failure(grant.status());
        return abilityGrantValue(grant.value());
    }

    Result<Value> matchingAbilities(const std::string& ownerId, const std::string& eventTag) const {
        if (!tags::isValidGameplayTagName(eventTag))
            return failure<Value>(DiagnosticCode::InvalidArgument, "ability event tag is invalid", "eventTag");
        Value::Array result;
        for (const auto grant : abilities_.matchingGrants(ownerId, eventTag))
            result.emplace_back(static_cast<std::int64_t>(grant.value()));
        return Result<Value>::success(Value(std::move(result)));
    }

    Result<Value> state(const std::string& subjectText) const {
        auto subject = parseSubject(subjectText, "subject");
        if (!subject) return Result<Value>::failure(subject.status());
        const auto found = states_.find(subjectText);
        if (found == states_.end())
            return failure<Value>(DiagnosticCode::NotFound, "combat fighter was not found", subjectText);
        auto movement = locomotion_.state(subject.value());
        if (!movement) return Result<Value>::failure(movement.status());
        Value::Object result;
        result["subject"] = subjectText;
        result["ownerId"] = movement.value().ownerId;
        result["position"] = vectorValue(movement.value().position);
        result["velocity"] = vectorValue(movement.value().velocity);
        result["facing"] = vectorValue(movement.value().facing);
        result["health"] = found->second.health;
        result["maxHealth"] = found->second.maxHealth;
        result["poise"] = found->second.poise;
        result["maxPoise"] = found->second.maxPoise;
        result["navigating"] = movement.value().navigationGoal.has_value();
        return Result<Value>::success(Value(std::move(result)));
    }

private:
    static Result<action::AbilityGrantId> parseGrant(std::int64_t value) {
        if (value <= 0)
            return failure<action::AbilityGrantId>(DiagnosticCode::InvalidArgument,
                                                   "ability grant id must be positive", "grantId");
        return Result<action::AbilityGrantId>::success(
            action::AbilityGrantId(static_cast<std::uint64_t>(value)));
    }

    Result<Value> abilityGrantValue(action::AbilityGrantId grantId) const {
        auto state = abilities_.findGrant(grantId);
        if (!state) return Result<Value>::failure(state.status());
        Value::Object result;
        result["grantId"] = static_cast<std::int64_t>(state.value().grantId.value());
        result["ownerId"] = state.value().ownerId;
        result["definitionId"] = state.value().definitionId.format();
        result["cooldownSeconds"] = state.value().cooldownRemaining.seconds();
        return Result<Value>::success(Value(std::move(result)));
    }

    static Value abilityActivationValue(const action::AbilityActivation& activation) {
        Value::Object result;
        result["activationId"] = static_cast<std::int64_t>(activation.id.value());
        result["grantId"] = static_cast<std::int64_t>(activation.grantId.value());
        result["instanceId"] = static_cast<std::int64_t>(activation.instanceId.value());
        result["executionId"] = static_cast<std::int64_t>(activation.executionId.value());
        result["ownerId"] = activation.ownerId;
        return Value(std::move(result));
    }

    DirectCombatNavigationProvider navigation_;
    CombatLocomotionRuntime        locomotion_;
    DamageRuntime                  damage_;
    std::map<std::string, CombatState, std::less<>> states_;
    action::ActionRuntime                         actions_;
    action::AbilityRuntime                        abilities_{actions_};
    std::map<std::string, LogicalId, std::less<>> abilityActions_;
    std::map<std::uint64_t, LogicalId>             grantedActions_;
};

ssq::Table project(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}

ssq::Table newRuntime(HSQUIRRELVM vm) {
    auto runtime = std::make_unique<ScriptCombatRuntime>();
    auto initialized = runtime->initializeAbilities();
    if (!initialized) return script::projectStatusResult(vm, initialized.status(), false, false);
    auto object = script::makeOwnedSquirrelInstance<ScriptCombatRuntime>(vm, std::move(runtime));
    if (!object) return script::projectStatusResult(vm, object.status(), false, false);
    auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
    result.set("value", std::move(object).takeValue());
    result.set("ownership", std::string("owned"));
    return result;
}

}  // namespace

Module_IMPL(Combat, new Combat());

void Combat::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto runtime = table.addClass<ScriptCombatRuntime>(
        "CombatRuntime", std::function<ScriptCombatRuntime*()>([] { return nullptr; }), true);
    runtime.addFunc("ownership", [](ScriptCombatRuntime*) { return std::string("owned"); });
    runtime.addFunc("registerFighter", [vm](ScriptCombatRuntime* self, const std::string& subject,
                                             const std::string& owner, float x, float z, float health,
                                             float poise, float speed, float acceleration) {
        return project(vm, self ? self->registerFighter(subject, owner, x, z, health, poise, speed, acceleration)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("setMoveIntent", [vm](ScriptCombatRuntime* self, const std::string& subject,
                                           float x, float z, float speed) {
        return project(vm, self ? self->setMoveIntent(subject, x, z, speed)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("navigateTo", [vm](ScriptCombatRuntime* self, const std::string& subject,
                                       float x, float z, float radius) {
        return project(vm, self ? self->navigateTo(subject, x, z, radius)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("stop", [vm](ScriptCombatRuntime* self, const std::string& subject) {
        return project(vm, self ? self->stop(subject)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("advance", [vm](ScriptCombatRuntime* self, std::int64_t tick, float seconds) {
        return project(vm, self ? self->advance(tick, seconds)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("applyDamage", [vm](ScriptCombatRuntime* self, const std::string& source,
                                         const std::string& target, const std::string& type,
                                         float health, float poise, float x, float y, float z) {
        return project(vm, self ? self->applyDamage(source, target, type, health, poise, x, y, z)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("applyTimelineDamage",
                    [vm](ScriptCombatRuntime* self, const std::string& source,
                         const std::string& target, const std::string& payloadJson) {
        return project(vm, self ? self->applyTimelineDamage(source, target, payloadJson)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("grantAbility", [vm](ScriptCombatRuntime* self, const std::string& owner,
                                           const std::string& definitionId) {
        return project(vm, self ? self->grantAbility(owner, definitionId)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("registerTimelineAbility",
                    [vm](ScriptCombatRuntime* self, const std::string& definitionId,
                         const std::string& timelineJson, float cooldownSeconds,
                         const std::string& instancing, const std::string& activationGroup,
                         const std::string& triggerTag) {
        return project(vm, self ? self->registerTimelineAbility(definitionId, timelineJson, cooldownSeconds,
                                                                 instancing, activationGroup, triggerTag)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("activateAbility", [vm](ScriptCombatRuntime* self, std::int64_t grantId,
                                              std::int64_t tick) {
        return project(vm, self ? self->activateAbility(grantId, tick)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("advanceAbilities", [vm](ScriptCombatRuntime* self, std::int64_t tick, float seconds) {
        return project(vm, self ? self->advanceAbilities(tick, seconds)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("cancelAbility", [vm](ScriptCombatRuntime* self, std::int64_t activationId,
                                            std::int64_t tick) {
        return project(vm, self ? self->cancelAbility(activationId, tick)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("abilityGrant", [vm](ScriptCombatRuntime* self, std::int64_t grantId) {
        return project(vm, self ? self->abilityGrant(grantId)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("matchingAbilities", [vm](ScriptCombatRuntime* self, const std::string& owner,
                                                const std::string& eventTag) {
        return project(vm, self ? self->matchingAbilities(owner, eventTag)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });
    runtime.addFunc("state", [vm](ScriptCombatRuntime* self, const std::string& subject) {
        return project(vm, self ? self->state(subject)
                                : failure<Value>(DiagnosticCode::InvalidArgument,
                                                 "combat runtime must not be null", "runtime"));
    });

    auto module = table.addClass(name, Combat::create, false);
    module.addFunc("getName", &Combat::getName);
    module.addFunc("newRuntime", [vm](Combat*) { return newRuntime(vm); });
}

void Combat::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Combat::getName);
    cls.addFunc("newRuntime", [vm = cls.getHandle()](Combat*) { return newRuntime(vm); });
}

}  // namespace eve::combat
