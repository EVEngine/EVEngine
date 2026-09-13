#include "combat/Combat.h"

#include "action/ActionDamageBlock.h"
#include "combat/CombatLocomotion.h"
#include "combat/Damage.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"

#include <simplesquirrel/simplesquirrel.hpp>

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
    DirectCombatNavigationProvider navigation_;
    CombatLocomotionRuntime        locomotion_;
    DamageRuntime                  damage_;
    std::map<std::string, CombatState, std::less<>> states_;
};

ssq::Table project(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}

ssq::Table newRuntime(HSQUIRRELVM vm) {
    auto object = script::makeOwnedSquirrelInstance<ScriptCombatRuntime>(
        vm, std::make_unique<ScriptCombatRuntime>());
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
