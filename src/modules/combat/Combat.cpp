#include "combat/Combat.h"

#include "action/AbilityAsset.h"
#include "action/AbilitySystem.h"
#include "action/ActionDamageBlock.h"
#include "action/ActionTimeline.h"
#include "combat/CombatLocomotion.h"
#include "combat/Damage.h"
#include "combat/StandardAbilities.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "tags/GameplayTag.h"

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

Result<SubjectRef> parseSubject(const std::string& text, std::string path) {
    const auto parsed = PersistentId::parse(text);
    if (!parsed)
        return Result<SubjectRef>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "combat subject must be a UUID", path, {}, "combat.squirrel"));
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
            auto registered = abilities_.registerDefinition(std::move(definition));
            if (!registered) return registered;
        }
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    Result<Value> registerFighter(const std::string& subjectText, const std::string& ownerId, double x, double z,
                                  double health, double poise, double maximumSpeed, double acceleration) {
        auto subject = parseSubject(subjectText, "subject");
        if (!subject) return Result<Value>::failure(subject.status());
        CombatState combatState{subject.value(), health, health, poise, poise};
        auto        valid = combatState.validate();
        if (!valid) return Result<Value>::failure(valid.status());
        if (states_.contains(subjectText))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::AlreadyExists, "combat fighter already exists", subjectText, {}, "combat.squirrel"));
        auto registered = locomotion_.registerSubject({subject.value(), ownerId, {x, z}, maximumSpeed, acceleration});
        if (!registered) return Result<Value>::failure(registered.status());
        states_.emplace(subjectText, std::move(combatState));
        return state(subjectText);
    }

    Result<Value> setMoveIntent(const std::string& subjectText, double x, double z, double speedFraction) {
        auto subject = parseSubject(subjectText, "subject");
        if (!subject) return Result<Value>::failure(subject.status());
        auto changed = locomotion_.setMoveIntent(subject.value(), {x, z}, speedFraction);
        if (!changed) return Result<Value>::failure(changed.status());
        return state(subjectText);
    }

    Result<Value> navigateTo(const std::string& subjectText, double x, double z, double acceptanceRadius) {
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
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "combat tick must be non-negative", "tick", {}, "combat.squirrel"));
        auto delta = Duration::fromSeconds(seconds);
        if (!delta) return Result<Value>::failure(delta.status());
        auto advanced =
            locomotion_.advance({SimulationTick(static_cast<std::uint64_t>(tick)), std::move(delta).takeValue()});
        if (!advanced) return Result<Value>::failure(advanced.status());
        Value::Array events;
        events.reserve(advanced.value().events.size());
        for (const auto& event : advanced.value().events) {
            Value::Object value;
            value["subject"]  = event.subject.format();
            value["kind"]     = event.kind == CombatLocomotionEventKind::Arrived ? "arrived" : "moved";
            value["position"] = vectorValue(event.position);
            events.emplace_back(std::move(value));
        }
        Value::Object result;
        result["tick"]   = tick;
        result["events"] = std::move(events);
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> applyDamage(const std::string& sourceText, const std::string& targetText,
                              const std::string& damageType, double healthDamage, double poiseDamage, double knockbackX,
                              double knockbackY, double knockbackZ) {
        auto source = parseSubject(sourceText, "source");
        if (!source) return Result<Value>::failure(source.status());
        auto target = parseSubject(targetText, "target");
        if (!target) return Result<Value>::failure(target.status());
        const auto found = states_.find(targetText);
        if (found == states_.end())
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound, "combat target was not found",
                                                            targetText, {}, "combat.squirrel"));
        DamageRequest request;
        request.source       = source.value();
        request.target       = target.value();
        request.damageType   = damageType;
        request.healthDamage = healthDamage;
        request.poiseDamage  = poiseDamage;
        request.knockback    = {knockbackX, knockbackY, knockbackZ};
        auto outcome         = damage_.apply(found->second, request);
        if (!outcome) return Result<Value>::failure(outcome.status());
        Value::Object result;
        result["source"]              = outcome.value().source.format();
        result["target"]              = outcome.value().target.format();
        result["health"]              = outcome.value().health;
        result["poise"]               = outcome.value().poise;
        result["appliedHealthDamage"] = outcome.value().appliedHealthDamage;
        result["appliedPoiseDamage"]  = outcome.value().appliedPoiseDamage;
        result["reaction"]            = reactionName(outcome.value().reaction);
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> applyTimelineDamage(const std::string& sourceText, const std::string& targetText,
                                      const std::string& payloadJson) {
        auto decoded = Value::fromJson(payloadJson);
        if (!decoded) return Result<Value>::failure(decoded.status());
        const auto* payload = decoded.value().getIf<Value::Object>();
        if (!payload)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "combat timeline damage payload must be an object",
                                                            "payload", {}, "combat.squirrel"));
        auto binding = action::ActionDamageBinding::fromPayload(*payload);
        if (!binding) return Result<Value>::failure(binding.status());
        return applyDamage(sourceText, targetText, binding.value().damageType, binding.value().amount,
                           binding.value().poiseAmount, binding.value().knockback.x, binding.value().knockback.y,
                           binding.value().knockback.z);
    }

    Result<Value> grantAbility(const std::string& ownerId, const std::string& definitionText) {
        auto definitionId = LogicalId::parse(definitionText);
        if (!definitionId)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "ability definition id is invalid", "definitionId", {},
                                                            "combat.squirrel"));
        auto granted = abilities_.grant(ownerId, *definitionId);
        if (!granted) return Result<Value>::failure(granted.status());
        return abilityGrantValue(granted.value());
    }

    Result<Value> registerTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                          double cooldownSeconds, const std::string& instancingText,
                                          const std::string& groupText, const std::string& triggerTag) {
        return setTimelineAbility(definitionText, timelineJson, cooldownSeconds, instancingText, groupText, triggerTag,
                                  false);
    }

    Result<Value> replaceTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                         double cooldownSeconds, const std::string& instancingText,
                                         const std::string& groupText, const std::string& triggerTag) {
        return setTimelineAbility(definitionText, timelineJson, cooldownSeconds, instancingText, groupText, triggerTag,
                                  true);
    }

    Result<Value> setTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                     double cooldownSeconds, const std::string& instancingText,
                                     const std::string& groupText, const std::string& triggerTag, bool replace) {
        auto definitionId = LogicalId::parse(definitionText);
        if (!definitionId)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "ability definition id is invalid", "definitionId", {},
                                                            "combat.squirrel"));
        auto decoded = Value::fromJson(timelineJson);
        if (!decoded) return Result<Value>::failure(decoded.status());
        auto timeline = action::ActionTimeline::fromValue(decoded.value());
        if (!timeline) return Result<Value>::failure(timeline.status());
        auto cooldown = Duration::fromSeconds(cooldownSeconds);
        if (!cooldown) return Result<Value>::failure(cooldown.status());
        auto instancing = action::parseAbilityInstancingPolicy(instancingText);
        if (!instancing) return Result<Value>::failure(instancing.status());
        auto group = action::parseAbilityActivationGroup(groupText);
        if (!group) return Result<Value>::failure(group.status());
        auto definition =
            action::abilityDefinitionFromTimeline(*definitionId, std::move(timeline).takeValue(), cooldown.value(),
                                                  instancing.value(), group.value(), triggerTag);
        if (!definition) return Result<Value>::failure(definition.status());
        const LogicalId actionId = definition.value().action.id;
        const double    duration =
            definition.value().action.timeline ? definition.value().action.timeline->duration.seconds() : 0.0;
        const auto sectionCount =
            definition.value().action.timeline ? definition.value().action.timeline->sectionCount() : 0u;
        auto changed = replace ? abilities_.replaceDefinition(std::move(definition).takeValue())
                               : abilities_.registerDefinition(std::move(definition).takeValue());
        if (!changed) return Result<Value>::failure(changed.status());
        Value::Object result;
        result["definitionId"]    = definitionText;
        result["actionId"]        = actionId.format();
        result["durationSeconds"] = duration;
        result["sectionCount"]    = static_cast<std::int64_t>(sectionCount);
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> activateAbility(std::int64_t grantValue, std::int64_t tickValue) {
        auto grant = parseGrant(grantValue);
        if (!grant) return Result<Value>::failure(grant.status());
        if (tickValue < 0)
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick", {}, "combat.squirrel"));
        auto activated = abilities_.activate(grant.value(), SimulationTick(static_cast<std::uint64_t>(tickValue)));
        if (!activated) return Result<Value>::failure(activated.status());
        return Result<Value>::success(abilityActivationValue(activated.value()), Status::success(StatusCode::Applied));
    }

    Result<Value> advanceAbilities(std::int64_t tickValue, double seconds) {
        if (tickValue < 0)
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "ability tick must be non-negative", "tick", {}, "combat.squirrel"));
        auto delta = Duration::fromSeconds(seconds);
        if (!delta) return Result<Value>::failure(delta.status());
        auto stepped = abilities_.step(SimulationTick(static_cast<std::uint64_t>(tickValue)), delta.value());
        if (!stepped) return Result<Value>::failure(stepped.status());
        Value::Array advances;
        advances.reserve(stepped.value().advances.size());
        for (const auto& item : stepped.value().advances) {
            Value::Object value;
            value["activationId"]   = static_cast<std::int64_t>(item.activationId.value());
            value["executionId"]    = static_cast<std::int64_t>(item.executionId.value());
            value["phase"]          = std::string(action::actionPhaseName(item.phase));
            value["elapsedSeconds"] = item.totalElapsed.seconds();
            advances.emplace_back(std::move(value));
        }
        Value::Object result;
        result["advances"]       = std::move(advances);
        result["completedCount"] = static_cast<std::int64_t>(stepped.value().completedCount);
        result["activeCount"]    = static_cast<std::int64_t>(stepped.value().activeCount);
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> cancelAbility(std::int64_t activationValue, std::int64_t tickValue) {
        if (activationValue <= 0 || tickValue < 0)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "ability activation or tick is invalid", "activation", {},
                                                            "combat.squirrel"));
        auto cancelled = abilities_.cancel(action::AbilityActivationId(static_cast<std::uint64_t>(activationValue)),
                                           SimulationTick(static_cast<std::uint64_t>(tickValue)));
        if (!cancelled) return Result<Value>::failure(cancelled.status());
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
                DiagnosticCode::InvalidArgument, "ability event tag is invalid", "eventTag", {}, "combat.squirrel"));
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
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound, "combat fighter was not found",
                                                            subjectText, {}, "combat.squirrel"));
        auto movement = locomotion_.state(subject.value());
        if (!movement) return Result<Value>::failure(movement.status());
        Value::Object result;
        result["subject"]    = subjectText;
        result["ownerId"]    = movement.value().ownerId;
        result["position"]   = vectorValue(movement.value().position);
        result["velocity"]   = vectorValue(movement.value().velocity);
        result["facing"]     = vectorValue(movement.value().facing);
        result["health"]     = found->second.health;
        result["maxHealth"]  = found->second.maxHealth;
        result["poise"]      = found->second.poise;
        result["maxPoise"]   = found->second.maxPoise;
        result["navigating"] = movement.value().navigationGoal.has_value();
        return Result<Value>::success(Value(std::move(result)));
    }

private:
    static Result<action::AbilityGrantId> parseGrant(std::int64_t value) {
        if (value <= 0)
            return Result<action::AbilityGrantId>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                             "ability grant id must be positive",
                                                                             "grantId", {}, "combat.squirrel"));
        return Result<action::AbilityGrantId>::success(action::AbilityGrantId(static_cast<std::uint64_t>(value)));
    }

    Result<Value> abilityGrantValue(action::AbilityGrantId grantId) const {
        auto state = abilities_.findGrant(grantId);
        if (!state) return Result<Value>::failure(state.status());
        Value::Object result;
        result["grantId"]         = static_cast<std::int64_t>(state.value().grantId.value());
        result["ownerId"]         = state.value().ownerId;
        result["definitionId"]    = state.value().definitionId.format();
        result["cooldownSeconds"] = state.value().cooldownRemaining.seconds();
        return Result<Value>::success(Value(std::move(result)));
    }

    static Value abilityActivationValue(const action::AbilityActivation& activation) {
        Value::Object result;
        result["activationId"] = static_cast<std::int64_t>(activation.id.value());
        result["grantId"]      = static_cast<std::int64_t>(activation.grantId.value());
        result["instanceId"]   = static_cast<std::int64_t>(activation.instanceId.value());
        result["executionId"]  = static_cast<std::int64_t>(activation.executionId.value());
        result["ownerId"]      = activation.ownerId;
        return Value(std::move(result));
    }

    DirectCombatNavigationProvider                  navigation_;
    CombatLocomotionRuntime                         locomotion_;
    DamageRuntime                                   damage_;
    std::map<std::string, CombatState, std::less<>> states_;
    action::ActionRuntime                           actions_;
    action::AbilityRuntime                          abilities_{actions_};
};

ssq::Table project(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}

ssq::Table newRuntime(HSQUIRRELVM vm) {
    auto runtime     = std::make_unique<ScriptCombatRuntime>();
    auto initialized = runtime->initializeAbilities();
    if (!initialized) return script::projectStatusResult(vm, initialized.status());
    auto object = script::makeOwnedSquirrelInstance<ScriptCombatRuntime>(vm, std::move(runtime));
    if (!object) return script::projectStatusResult(vm, object.status());
    auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied));
    result.set("value", std::move(object).takeValue());
    result.set("ownership", std::string("owned"));
    return result;
}

}  // namespace

Module_IMPL(Combat, new Combat());

void Combat::expose(ssq::Table& table) {
    const HSQUIRRELVM vm      = table.getHandle();
    auto              runtime = table.addClass<ScriptCombatRuntime>(
        "CombatRuntime", std::function<ScriptCombatRuntime*()>([] { return nullptr; }), true);
    runtime.addFunc("ownership", [](ScriptCombatRuntime*) { return std::string("owned"); });
    runtime.addFunc(
        "registerFighter", [vm](ScriptCombatRuntime* self, const std::string& subject, const std::string& owner,
                                float x, float z, float health, float poise, float speed, float acceleration) {
            return project(vm, self ? self->registerFighter(subject, owner, x, z, health, poise, speed, acceleration)
                                    : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                               "combat runtime must not be null",
                                                                               "runtime", {}, "combat.squirrel")));
        });
    runtime.addFunc(
        "setMoveIntent", [vm](ScriptCombatRuntime* self, const std::string& subject, float x, float z, float speed) {
            return project(vm, self ? self->setMoveIntent(subject, x, z, speed)
                                    : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                               "combat runtime must not be null",
                                                                               "runtime", {}, "combat.squirrel")));
        });
    runtime.addFunc(
        "navigateTo", [vm](ScriptCombatRuntime* self, const std::string& subject, float x, float z, float radius) {
            return project(vm, self ? self->navigateTo(subject, x, z, radius)
                                    : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                               "combat runtime must not be null",
                                                                               "runtime", {}, "combat.squirrel")));
        });
    runtime.addFunc("stop", [vm](ScriptCombatRuntime* self, const std::string& subject) {
        return project(vm, self ? self->stop(subject)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc("advance", [vm](ScriptCombatRuntime* self, std::int64_t tick, float seconds) {
        return project(vm, self ? self->advance(tick, seconds)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc("applyDamage", [vm](ScriptCombatRuntime* self, const std::string& source, const std::string& target,
                                        const std::string& type, float health, float poise, float x, float y, float z) {
        return project(vm, self ? self->applyDamage(source, target, type, health, poise, x, y, z)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc("applyTimelineDamage", [vm](ScriptCombatRuntime* self, const std::string& source,
                                                const std::string& target, const std::string& payloadJson) {
        return project(vm, self ? self->applyTimelineDamage(source, target, payloadJson)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc(
        "grantAbility", [vm](ScriptCombatRuntime* self, const std::string& owner, const std::string& definitionId) {
            return project(vm, self ? self->grantAbility(owner, definitionId)
                                    : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                               "combat runtime must not be null",
                                                                               "runtime", {}, "combat.squirrel")));
        });
    runtime.addFunc("registerTimelineAbility", [vm](ScriptCombatRuntime* self, const std::string& definitionId,
                                                    const std::string& timelineJson, float cooldownSeconds,
                                                    const std::string& instancing, const std::string& activationGroup,
                                                    const std::string& triggerTag) {
        return project(vm, self ? self->registerTimelineAbility(definitionId, timelineJson, cooldownSeconds, instancing,
                                                                activationGroup, triggerTag)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc("replaceTimelineAbility", [vm](ScriptCombatRuntime* self, const std::string& definitionId,
                                                   const std::string& timelineJson, float cooldownSeconds,
                                                   const std::string& instancing, const std::string& activationGroup,
                                                   const std::string& triggerTag) {
        return project(vm, self ? self->replaceTimelineAbility(definitionId, timelineJson, cooldownSeconds, instancing,
                                                               activationGroup, triggerTag)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc("activateAbility", [vm](ScriptCombatRuntime* self, std::int64_t grantId, std::int64_t tick) {
        return project(vm, self ? self->activateAbility(grantId, tick)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc("advanceAbilities", [vm](ScriptCombatRuntime* self, std::int64_t tick, float seconds) {
        return project(vm, self ? self->advanceAbilities(tick, seconds)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc("cancelAbility", [vm](ScriptCombatRuntime* self, std::int64_t activationId, std::int64_t tick) {
        return project(vm, self ? self->cancelAbility(activationId, tick)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc("abilityGrant", [vm](ScriptCombatRuntime* self, std::int64_t grantId) {
        return project(vm, self ? self->abilityGrant(grantId)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
    });
    runtime.addFunc(
        "matchingAbilities", [vm](ScriptCombatRuntime* self, const std::string& owner, const std::string& eventTag) {
            return project(vm, self ? self->matchingAbilities(owner, eventTag)
                                    : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                               "combat runtime must not be null",
                                                                               "runtime", {}, "combat.squirrel")));
        });
    runtime.addFunc("state", [vm](ScriptCombatRuntime* self, const std::string& subject) {
        return project(vm, self ? self->state(subject)
                                : Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "combat runtime must not be null", "runtime",
                                                                           {}, "combat.squirrel")));
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
