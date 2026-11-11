#include "rpg/RPGActor.h"

#include "common/Value.h"
#include "rpg/CastingPersistenceInternal.h"
#include "rpg/Skill.h"
#include "rpg/SkillContent.h"
#include "rpg/StatusPersistenceInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace eve::rpg {

namespace {

bool hasCheckpointComponents(RPGActor &actor) {
    auto &manager = actor.getComponentManager();
    auto  present = [&]<typename T>() {
        const auto *buffer = manager.getComponentBuffer<T>();
        return buffer && buffer->has(actor.id);
    };
    return present.template operator()<RPGActor::Attributes>() && present.template operator()<RPGActor::Statuses>() &&
           present.template operator()<RPGActor::Skills>() && present.template operator()<RPGActor::Progression>() &&
           present.template operator()<RPGActor::Vitals>() && present.template operator()<RPGActor::Traits>() &&
           present.template operator()<RPGActor::ClassInfo>() && present.template operator()<ActorExecutionOrder>();
}

double numericValue(const eve::Value &value) {
    return value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
}

eve::Result<void> validateVitalAttribute(const ::eve::attributes::AttributeSet &attributes, const std::string &name) {
    if (!attributes.has(name))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "actor vital has no authoritative attribute", name));
    for (int index = 0; index < attributes.modifierCount(); ++index) {
        const auto *modifier = attributes.modifierAt(index);
        if (modifier->attribute == name && modifier->operation == ::eve::attributes::AttributeOperation::Custom)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "custom vital-bound policies need explicit restore admission", name));
    }
    const double bound = attributes.getFinal(name);
    if (!std::isfinite(bound) || bound < 0)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "actor vital has an invalid derived bound", name));
    return eve::Result<void>::success();
}

}  // namespace

eve::Result<std::string> RPGActor::checkpointJson(std::optional<CastTargetBinding> target) const {
    RPGActor *actor = const_cast<RPGActor *>(this);
    if (!hasCheckpointComponents(*actor))
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "actor checkpoint requires preconfigured components", {}));
    auto statusRef = actor->statuses();
    auto skillRef = actor->skills();
    auto attributeRef = actor->attributes();
    auto progressionRef = actor->progression();
    auto vitalRef = actor->vitals();
    auto traitRef = actor->traits();
    auto classRef = actor->classInfo();
    auto casting        = encodeCastingState(*actor, target);
    if (!casting) return eve::Result<std::string>::failure(casting.status());

    for (const auto &[name, value] : vitalRef->current) {
        auto valid = validateVitalAttribute(attributeRef->values, name);
        if (!valid) return eve::Result<std::string>::failure(valid.status());
        if (!std::isfinite(value) || value < 0 || value > attributeRef->values.getFinal(name))
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "actor vital is outside its attribute bounds", name));
    }
    auto fullAttributes = attributeRef->values.encodeState();
    if (!fullAttributes) return eve::Result<std::string>::failure(fullAttributes.status());
    auto statuses = encodeStatusState(*statusRef, attributeRef->values);
    if (!statuses) return eve::Result<std::string>::failure(statuses.status());

    eve::Value::Object progressionValue;
    progressionValue.emplace("level", eve::Value(progressionRef->level));
    progressionValue.emplace("xp", eve::Value(progressionRef->xp));
    progressionValue.emplace("xpToNext", eve::Value(progressionRef->xpToNext));

    eve::Value::Object vitalsValue;
    for (const auto &[name, value] : vitalRef->current) vitalsValue.emplace(name, eve::Value(value));

    std::vector<std::string> skillIds;
    skillIds.reserve(skillRef->known.size());
    for (const auto &[id, runtime] : skillRef->known) {
        (void)runtime;
        skillIds.push_back(id);
    }
    std::sort(skillIds.begin(), skillIds.end());
    eve::Value::Array learnedSkills;
    eve::Value::Array cooldowns;
    for (const auto &id : skillIds) {
        const auto remaining = skillRef->known.at(id).cooldownRemaining;
        if (!std::isfinite(remaining) || remaining < 0.f)
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "actor skill cooldown is invalid", "$.cooldowns"));
        learnedSkills.emplace_back(id);
        cooldowns.push_back(eve::Value::object({{"id", id}, {"remainingSeconds", static_cast<double>(remaining)}}));
    }

    eve::Value::Array traitValues;
    for (const auto &trait : traitRef->active) {
        eve::Value::Object encoded;
        encoded.emplace("source", eve::Value(trait.source));
        encoded.emplace("traitId", eve::Value(trait.traitId));
        traitValues.emplace_back(std::move(encoded));
    }

    eve::Value::Object classValue;
    classValue.emplace("id", eve::Value(classRef->classId));
    classValue.emplace("skillsSyncedUpTo", eve::Value(classRef->skillsSyncedUpTo));

    eve::Value::Object root;
    root.emplace("attributes", std::move(fullAttributes).takeValue());
    root.emplace("statuses", std::move(statuses).takeValue());
    root.emplace("class", eve::Value(std::move(classValue)));
    root.emplace("cooldowns", eve::Value(std::move(cooldowns)));
    root.emplace("learnedSkills", eve::Value(std::move(learnedSkills)));
    root.emplace("progression", eve::Value(std::move(progressionValue)));
    root.emplace("schema", eve::Value("eve.rpg.actor-checkpoint"));
    root.emplace("traits", eve::Value(std::move(traitValues)));
    const auto order = actor->executionOrder()->key;
    if (order && order->isNil())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Actor execution key cannot be nil", "$.executionOrder"));
    root.emplace("executionOrder", order ? eve::Value(order->format()) : eve::Value{});
    root.emplace("version", eve::Value(5));
    root.emplace("casting", std::move(casting).takeValue());
    root.emplace("vitals", eve::Value(std::move(vitalsValue)));
    return eve::Value(std::move(root)).toJson();
}

eve::Result<RPGActor::CheckpointCandidate> RPGActor::prepareCheckpointJson(
    std::string_view json, std::optional<CastTargetBinding> target) const {
    RPGActor *actor = const_cast<RPGActor *>(this);
    if (!hasCheckpointComponents(*actor))
        return eve::Result<CheckpointCandidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "actor checkpoint restore requires preconfigured components", {}));
    auto statusRef = actor->statuses();
    auto skillRef = actor->skills();
    auto attributeRef = actor->attributes();
    auto traitRef = actor->traits();
    auto classRef = actor->classInfo();
    if (statusRef->container.effectCount() != 0 || skillRef->casting.active)
        return eve::Result<CheckpointCandidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "actor checkpoint restore requires a clean preconfigured destination", {}));
    auto candidate = prepareCheckpointState(json, attributeRef->values, *statusRef, *traitRef, *classRef, nullptr);
    if (candidate) {
        if (candidate.value().sourceVersion < 5) candidate.value().executionOrder = *actor->executionOrder();
        auto bound = bindCastingState(candidate.value().skills.casting, candidate.value().castTarget, *actor, target);
        if (!bound) return eve::Result<CheckpointCandidate>::failure(bound.status());
        candidate.value().skills.conditionBindings = skillRef->conditionBindings;
    }
    return candidate;
}

eve::Result<RPGActor::CheckpointCandidate> RPGActor::prepareCheckpointState(
    std::string_view json, const ::eve::attributes::AttributeSet &baseAttributes, const Statuses &baseStatuses,
    const Traits &traits, const ClassInfo &baseClass, const SkillContent *skills) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<CheckpointCandidate>::failure(parsed.status());
    const eve::Value &root = parsed.value();
    if (!root.isObject())
        return eve::Result<CheckpointCandidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "actor checkpoint root must be an object", "$"));
    const eve::Value *schema = root.find("schema");
    if (!schema || !schema->isString() || schema->asString() != "eve.rpg.actor-checkpoint")
        return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPGActor", "$.schema"));
    const eve::Value *version = root.find("version");
    if (!version || !version->isInt64() || (version->asInt() < 1 || version->asInt() > 5))
        return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::UnknownVersion, "unsupported RPG actor checkpoint version", "$.version"));

    if (version->asInt() < 3 && baseStatuses.container.effectCount() != 0)
        return eve::Result<CheckpointCandidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "legacy actor imports require a clean status prototype", "$.version"));
    if (version->asInt() >= 3) {
        const auto *fields = root.getIf<eve::Value::Object>();
        if (fields->size() != (version->asInt() == 5   ? 12u
                               : version->asInt() == 4 ? 11u
                                                       : 10u) ||
            !fields->contains("attributes") || !fields->contains("statuses") || !fields->contains("cooldowns") ||
            (version->asInt() >= 4 && !fields->contains("casting")) ||
            (version->asInt() == 5 && !fields->contains("executionOrder")))
            return eve::Result<CheckpointCandidate>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "unknown or missing actor fields", "$"));
    }
    const eve::Value *encodedClass = root.find("class");
    const eve::Value *encodedTraits = root.find("traits");
    const eve::Value *encodedAttributes  = root.find(version->asInt() >= 3 ? "attributes" : "baseAttributes");
    const eve::Value *encodedProgression = root.find("progression");
    const eve::Value *encodedVitals = root.find("vitals");
    const eve::Value *encodedSkills = root.find("learnedSkills");
    if (!encodedClass || !encodedClass->isObject() || !encodedTraits || !encodedTraits->isArray() ||
        !encodedAttributes || (version->asInt() < 3 && !encodedAttributes->isArray()) || !encodedProgression ||
        !encodedProgression->isObject() || !encodedVitals || !encodedVitals->isObject() || !encodedSkills ||
        !encodedSkills->isArray())
        return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "actor checkpoint participant fields have invalid types", "$"));

    if (version->asInt() >= 3 && (encodedClass->keys().size() != 2 || encodedProgression->keys().size() != 3))
        return eve::Result<CheckpointCandidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "unknown actor class/progression fields", "$"));
    const eve::Value *classId = encodedClass->find("id");
    const eve::Value *skillsSynced = encodedClass->find("skillsSyncedUpTo");
    if (!classId || !classId->isString() || !skillsSynced || !skillsSynced->isInt64() ||
        classId->asString() != baseClass.classId || skillsSynced->asInt() < 0 ||
        skillsSynced->asInt() > std::numeric_limits<int>::max())
        return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "actor checkpoint class does not match its destination", "$.class"));
    if (encodedTraits->arraySize() != traits.active.size())
        return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "actor checkpoint traits do not match its destination", "$.traits"));
    for (std::size_t index = 0; index < traits.active.size(); ++index) {
        const eve::Value &encoded = encodedTraits->at(index);
        if (version->asInt() >= 3 && (!encoded.isObject() || encoded.keys().size() != 2))
            return eve::Result<CheckpointCandidate>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "unknown actor trait fields", "$.traits"));
        const eve::Value *traitId = encoded.isObject() ? encoded.find("traitId") : nullptr;
        const eve::Value *source = encoded.isObject() ? encoded.find("source") : nullptr;
        if (!traitId || !traitId->isString() || !source || !source->isString() ||
            traitId->asString() != traits.active[index].traitId || source->asString() != traits.active[index].source)
            return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "actor checkpoint trait identity does not match", "$.traits"));
    }

    CheckpointCandidate candidate{baseAttributes.prepareCopy(), {}, {}, {}, baseClass};
    candidate.classInfo.skillsSyncedUpTo = static_cast<int>(skillsSynced->asInt());
    if (version->asInt() >= 3) {
        auto attributes = ::eve::attributes::AttributeSet::prepareState(*encodedAttributes);
        if (!attributes) return eve::Result<CheckpointCandidate>::failure(attributes.status());
        candidate.attributes = std::move(attributes).takeValue();
        for (int index = 0; index < baseAttributes.attributeCount(); ++index)
            if (!candidate.attributes.view().has(baseAttributes.attributeAt(index)))
                return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "actor checkpoint is missing a required attribute", "$.attributes"));
        auto statuses = prepareStatusState(*root.find("statuses"), candidate.attributes.view(), baseStatuses, skills);
        if (!statuses) return eve::Result<CheckpointCandidate>::failure(statuses.status());
        candidate.statuses = std::move(statuses).takeValue();
    } else {
        if (encodedAttributes->arraySize() != static_cast<std::size_t>(candidate.attributes.view().attributeCount()))
            return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "actor checkpoint attribute layout does not match", "$.baseAttributes"));
        std::unordered_set<std::string> seenAttributes;
        for (std::size_t index = 0; index < encodedAttributes->arraySize(); ++index) {
            const eve::Value &encoded = encodedAttributes->at(index);
            const eve::Value *id      = encoded.isObject() ? encoded.find("id") : nullptr;
            const eve::Value *value   = encoded.isObject() ? encoded.find("value") : nullptr;
            if (!id || !id->isString() || !value || !value->isNumeric() || !std::isfinite(numericValue(*value)))
                return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::ParseError, "actor base attribute is invalid", "$.baseAttributes"));
            if (!seenAttributes.emplace(id->asString()).second || !candidate.attributes.view().has(id->asString()))
                return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "actor base attribute layout does not match", "$.baseAttributes"));
            candidate.attributes.setBase(id->asString(), numericValue(*value));
        }

        auto previous = baseStatuses.container.encodeState();
        if (!previous) return eve::Result<CheckpointCandidate>::failure(previous.status());
        candidate.statuses.container = baseStatuses.container;
        auto empty                   = candidate.statuses.container.restore(::eve::effects::EffectContainer{});
        if (!empty) return eve::Result<CheckpointCandidate>::failure(empty.status());
    }

    const eve::Value *level = encodedProgression->find("level");
    const eve::Value *xp = encodedProgression->find("xp");
    const eve::Value *xpToNext = encodedProgression->find("xpToNext");
    if (!level || !level->isInt64() || level->asInt() < 1 || level->asInt() > std::numeric_limits<int>::max() || !xp ||
        !xp->isNumeric() || !xpToNext || !xpToNext->isNumeric() || !std::isfinite(numericValue(*xp)) ||
        !std::isfinite(numericValue(*xpToNext)) || numericValue(*xp) < 0.0 ||
        numericValue(*xpToNext) <= numericValue(*xp))
        return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "actor checkpoint progression is invalid", "$.progression"));
    candidate.progression = {static_cast<int>(level->asInt()), numericValue(*xp), numericValue(*xpToNext)};

    for (const auto &name : encodedVitals->keys()) {
        if (version->asInt() >= 3) {
            auto valid = validateVitalAttribute(candidate.attributes.view(), name);
            if (!valid) return eve::Result<CheckpointCandidate>::failure(valid.status());
        }
        const eve::Value *value = encodedVitals->find(name);
        if (!value || !value->isNumeric() || !std::isfinite(numericValue(*value)) || numericValue(*value) < 0.0 ||
            numericValue(*value) > candidate.attributes.view().getFinal(name))
            return eve::Result<CheckpointCandidate>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                       "actor checkpoint vital is outside its attribute bounds", "$.vitals." + name));
        candidate.vitals.current.emplace(name, numericValue(*value));
    }

    std::unordered_set<std::string> seenSkills;
    for (std::size_t index = 0; index < encodedSkills->arraySize(); ++index) {
        const eve::Value &id = encodedSkills->at(index);
        if (!id.isString() || id.asString().empty() || !seenSkills.emplace(id.asString()).second ||
            !(skills ? skills->contains(id.asString()) : SkillRegistry::find(id.asString()) != nullptr))
            return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "actor checkpoint skill is missing or invalid", "$.learnedSkills"));
        candidate.skills.known.emplace(id.asString(), SkillRuntime{});
    }
    // Explicit N-1 migration: v1 did not encode cooldowns and restored them as zero.
    if (version->asInt() >= 2) {
        const auto *cooldowns = root.find("cooldowns");
        if (!cooldowns || !cooldowns->isArray() || cooldowns->arraySize() != candidate.skills.known.size())
            return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "actor cooldown roster does not match learned skills", "$.cooldowns"));
        std::unordered_set<std::string> seenCooldowns;
        for (std::size_t index = 0; index < cooldowns->arraySize(); ++index) {
            const auto &entry = cooldowns->at(index);
            if (version->asInt() >= 3 && (!entry.isObject() || entry.keys().size() != 2))
                return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::ParseError, "unknown actor cooldown fields", "$.cooldowns"));
            const auto *id        = entry.isObject() ? entry.find("id") : nullptr;
            const auto *remaining = entry.isObject() ? entry.find("remainingSeconds") : nullptr;
            if (!id || !id->isString() || !candidate.skills.known.contains(id->asString()) ||
                !seenCooldowns.emplace(id->asString()).second || !remaining || !remaining->isNumeric())
                return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "actor cooldown identity/value is invalid", "$.cooldowns"));
            const auto seconds = numericValue(*remaining);
            if (!std::isfinite(seconds) || seconds < 0.0 || seconds > std::numeric_limits<float>::max() ||
                static_cast<double>(static_cast<float>(seconds)) != seconds)
                return eve::Result<CheckpointCandidate>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                           "actor cooldown must be a finite nonnegative runtime float", "$.cooldowns"));
            candidate.skills.known.at(id->asString()).cooldownRemaining = static_cast<float>(seconds);
        }
    }
    if (version->asInt() >= 4) {
        auto casting = prepareCastingState(*root.find("casting"), candidate.skills);
        if (!casting) return eve::Result<CheckpointCandidate>::failure(casting.status());
        candidate.skills.casting = std::move(casting.value().state);
        candidate.castTarget     = casting.value().target;
    }
    candidate.sourceVersion = static_cast<std::uint8_t>(version->asInt());
    if (version->asInt() == 5) {
        const auto *order = root.find("executionOrder");
        if (!order->isNull()) {
            auto key = order->isString() ? ActorExecutionKey::parse(order->asString()) : std::nullopt;
            if (!key || key->isNil() || key->format() != order->asString())
                return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "Invalid canonical execution key", "$.executionOrder"));
            candidate.executionOrder.key = *key;
        }
    }
    return eve::Result<CheckpointCandidate>::success(std::move(candidate));
}

void RPGActor::commitCheckpoint(CheckpointCandidate candidate) noexcept {
    attributes()->values.commitPrepared(std::move(candidate.attributes));
    *statuses()       = std::move(candidate.statuses);
    *skills() = std::move(candidate.skills);
    *progression() = candidate.progression;
    *vitals() = std::move(candidate.vitals);
    *classInfo() = std::move(candidate.classInfo);
    *executionOrder() = candidate.executionOrder;
}

eve::Result<void> RPGActor::restoreCheckpointJson(std::string_view json, std::optional<CastTargetBinding> target) {
    auto prepared = prepareCheckpointJson(json, target);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitCheckpoint(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
