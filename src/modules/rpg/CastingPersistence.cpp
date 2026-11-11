#include <cmath>
#include <limits>
#include "rpg/CastingPersistenceInternal.h"
namespace eve::rpg {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "$.casting"));
}
bool sameHandle(const ecs::EntityHandle& a, const ecs::EntityHandle& b) {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
bool validClocks(const CastingState& state) {
    return std::isfinite(state.remaining) && std::isfinite(state.totalCastTime) && state.remaining > 0 &&
           state.totalCastTime > 0 && state.remaining <= state.totalCastTime;
}
}  // namespace
Result<Value> encodeCastingState(const RPGActor& actor, std::optional<CastTargetBinding> binding) {
    const auto& skills = *const_cast<RPGActor&>(actor).skills();
    const auto& state  = skills.casting;
    if (!state.active) {
        if (binding) return fail<Value>(DiagnosticCode::Conflict, "Inactive cast has a target binding");
        return Result<Value>::success(Value{});
    }
    if (state.skillId.empty() || !skills.known.contains(state.skillId) || !validClocks(state))
        return fail<Value>(DiagnosticCode::InvalidArgument, "Invalid active cast skill or clocks");
    std::string kind;
    Value       subject;
    switch (state.target.intent) {
        case CastTargetIntent::DefaultSelf:
        case CastTargetIntent::Retired:
            if (state.target.entity.table || binding)
                return fail<Value>(DiagnosticCode::Conflict, "Non-linked cast carries a target link or binding");
            kind = state.target.intent == CastTargetIntent::DefaultSelf ? "self" : "retired";
            break;
        case CastTargetIntent::Linked: {
            const auto& target = state.target.entity;
            if (!target.table || target.table != actor.table)
                return fail<Value>(DiagnosticCode::Conflict, "Cast target belongs to another physical world");
            auto* liveTarget = dynamic_cast<RPGActor*>(ecs::try_get(target));
            if (!liveTarget) {
                if (binding) return fail<Value>(DiagnosticCode::StaleHandle, "Retired cast target cannot be rebound");
                kind = "retired";
                break;
            }
            if (sameHandle(target, ecs::handle_of(const_cast<RPGActor*>(&actor)))) {
                if (binding && (!binding->subject.isValid() || !sameHandle(binding->entity, target) ||
                                actorSubject(*liveTarget) != std::optional<SubjectRef>{binding->subject}))
                    return fail<Value>(DiagnosticCode::Conflict, "Explicit self binding differs from the caster");
                kind = "explicitSelf";
                break;
            }
            if (!binding || !binding->subject.isValid() || !sameHandle(binding->entity, target))
                return fail<Value>(DiagnosticCode::Unsupported,
                                   "Live cast target requires its admitted stable identity");
            if (actorSubject(*liveTarget) != std::optional<SubjectRef>{binding->subject})
                return fail<Value>(DiagnosticCode::Conflict, "Cast target registration differs from its binding");
            kind    = "linked";
            subject = Value(binding->subject.persistentId().format());
            break;
        }
        default: return fail<Value>(DiagnosticCode::Unsupported, "Unknown cast target intent");
    }
    return Result<Value>::success(
        Value::object({{"skill", state.skillId},
                       {"remaining", double(state.remaining)},
                       {"total", double(state.totalCastTime)},
                       {"target", Value::object({{"kind", kind}, {"subject", std::move(subject)}})}}));
}
Result<PreparedCastingState> prepareCastingState(const Value& value, const RPGActor::Skills& skills) {
    PreparedCastingState result;
    if (value.isNull()) return Result<PreparedCastingState>::success(std::move(result));
    if (!value.isObject() || value.keys().size() != 4)
        return fail<PreparedCastingState>(DiagnosticCode::ParseError, "Unknown or missing casting fields");
    const auto* skill     = value.find("skill");
    const auto* remaining = value.find("remaining");
    const auto* total     = value.find("total");
    const auto* target    = value.find("target");
    if (!skill || !skill->isString() || !skills.known.contains(skill->asString()) || !remaining ||
        !remaining->isNumeric() || !total || !total->isNumeric() || !target || !target->isObject() ||
        target->keys().size() != 2)
        return fail<PreparedCastingState>(DiagnosticCode::ParseError, "Invalid casting participants");
    auto       number = [](const Value& v) { return v.isInt64() ? double(v.asInt()) : v.asDouble(); };
    const auto r = number(*remaining), t = number(*total);
    if (!std::isfinite(r) || !std::isfinite(t) || r <= 0 || t <= 0 || r > t || t > std::numeric_limits<float>::max() ||
        double(float(r)) != r || double(float(t)) != t)
        return fail<PreparedCastingState>(DiagnosticCode::InvalidArgument,
                                          "Cast clocks must be exact positive runtime floats");
    const auto* kind    = target->find("kind");
    const auto* subject = target->find("subject");
    if (!kind || !kind->isString() || !subject)
        return fail<PreparedCastingState>(DiagnosticCode::ParseError, "Cast target fields are missing");
    if (kind->asString() == "linked") {
        const auto id = subject->isString() ? PersistentId::parse(subject->asString()) : std::nullopt;
        if (!id || id->isNil() || id->format() != subject->asString())
            return fail<PreparedCastingState>(DiagnosticCode::InvalidArgument,
                                              "Cast target requires canonical stable identity");
        result.target              = SubjectRef::fromPersistentId(*id);
        result.state.target.intent = CastTargetIntent::Linked;
    } else if (kind->asString() == "self" || kind->asString() == "retired" || kind->asString() == "explicitSelf") {
        if (!subject->isNull())
            return fail<PreparedCastingState>(DiagnosticCode::Conflict, "Non-linked target has an identity");
        result.state.target.intent = kind->asString() == "self"           ? CastTargetIntent::DefaultSelf
                                     : kind->asString() == "explicitSelf" ? CastTargetIntent::Linked
                                                                          : CastTargetIntent::Retired;
    } else
        return fail<PreparedCastingState>(DiagnosticCode::Unsupported, "Unknown persistent cast target intent");
    result.state.active        = true;
    result.state.skillId       = skill->asString();
    result.state.remaining     = float(r);
    result.state.totalCastTime = float(t);
    return Result<PreparedCastingState>::success(std::move(result));
}
Result<void> bindCastingState(CastingState& state, std::optional<SubjectRef> subject, RPGActor& actor,
                              std::optional<CastTargetBinding> binding) {
    if (!subject) {
        if (binding) return fail<void>(DiagnosticCode::Conflict, "Checkpoint has no linked target to bind");
        if (state.active && state.target.intent == CastTargetIntent::Linked)
            state.target.entity = ecs::handle_of(&actor);
        return Result<void>::success();
    }
    if (!binding || binding->subject != *subject)
        return fail<void>(DiagnosticCode::NotFound, "Saved cast target binding is absent or has another identity");
    if (binding->entity.table != actor.table)
        return fail<void>(DiagnosticCode::StaleHandle, "Cast target requires a live same-world RPG incarnation");
    auto* target = dynamic_cast<RPGActor*>(ecs::try_get(binding->entity));
    if (!target)
        return fail<void>(DiagnosticCode::StaleHandle, "Cast target requires a live same-world RPG incarnation");
    if (actorSubject(*target) != std::optional<SubjectRef>{*subject})
        return fail<void>(DiagnosticCode::Conflict,
                          "Restored cast target registration differs from the saved identity");
    state.target = {CastTargetIntent::Linked, binding->entity};
    return Result<void>::success();
}
}  // namespace eve::rpg
