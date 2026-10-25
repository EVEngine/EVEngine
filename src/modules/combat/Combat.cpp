#include "combat/Combat.h"

#include "action/ActionDamageBlock.h"
#include "action/ScriptActionRuntime.h"
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
            auto registered = actions_.registerAbilityDefinition(std::move(definition));
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
        lastTick_ = SimulationTick(static_cast<std::uint64_t>(tick));
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
                              double knockbackY, double knockbackZ, bool critical, std::int64_t tick) {
        if (tick < 0)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "negative tick", "tick"));
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
        request.critical     = critical;
        request.tick         = SimulationTick(static_cast<std::uint64_t>(tick));
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
        // Reuse the canonical event payload codec, including schema-owned stage explanations.
        auto payload = Value::fromJson(outcome.value().settlementResult.event->payload);
        if (!payload) return Result<Value>::failure(payload.status());
        result["settlement"] = std::move(payload).takeValue();
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
                           binding.value().knockback.z, false, static_cast<std::int64_t>(lastTick_.value()));
    }

    Result<Value> grantAbility(const std::string& ownerId, const std::string& definitionText) {
        return actions_.grantAbility(ownerId, definitionText);
    }

    Result<Value> registerTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                          double cooldownSeconds, const std::string& instancingText,
                                          const std::string& groupText, const std::string& triggerTag) {
        return actions_.registerTimelineAbility(definitionText, timelineJson, static_cast<float>(cooldownSeconds),
                                                instancingText, groupText, triggerTag);
    }

    Result<Value> replaceTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                         double cooldownSeconds, const std::string& instancingText,
                                         const std::string& groupText, const std::string& triggerTag) {
        return actions_.replaceTimelineAbility(definitionText, timelineJson, static_cast<float>(cooldownSeconds),
                                               instancingText, groupText, triggerTag);
    }

    Result<Value> activateAbility(std::int64_t grantValue, std::int64_t tickValue) {
        return actions_.activateAbility(grantValue, tickValue);
    }

    Result<Value> advanceAbilities(std::int64_t tickValue, double seconds) {
        return actions_.advanceAbilities(tickValue, static_cast<float>(seconds));
    }

    Result<Value> cancelAbility(std::int64_t activationValue, std::int64_t tickValue) {
        return actions_.cancelAbility(activationValue, tickValue);
    }

    Result<Value> abilityGrant(std::int64_t grantValue) const { return actions_.abilityGrant(grantValue); }

    Result<Value> matchingAbilities(const std::string& ownerId, const std::string& eventTag) const {
        return actions_.matchingAbilities(ownerId, eventTag);
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
    DirectCombatNavigationProvider                  navigation_;
    CombatLocomotionRuntime                         locomotion_;
    DamageRuntime                                   damage_;
    SimulationTick                                  lastTick_ = SimulationTick::zero();
    std::map<std::string, CombatState, std::less<>> states_;
    /** @brief Shared action/ability script projection; Combat does not reimplement ability rules. */
    action::ScriptActionRuntime actions_;
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
                                        const std::string& type, float health, float poise, float x, float y, float z,
                                        bool critical, std::int64_t tick) {
        return project(vm, self ? self->applyDamage(source, target, type, health, poise, x, y, z, critical, tick)
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
