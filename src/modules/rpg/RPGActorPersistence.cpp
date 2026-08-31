#include "rpg/RPGActor.h"

#include "common/Value.h"
#include "rpg/Skill.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace eve::rpg {

namespace {

template <typename T>
eve::Result<T> checkpointFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

double numericValue(const eve::Value &value) {
    return value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
}

}  // namespace

eve::Result<std::string> RPGActor::checkpointJson() const {
    RPGActor *actor = const_cast<RPGActor *>(this);
    auto statusRef = actor->statuses();
    auto skillRef = actor->skills();
    auto attributeRef = actor->attributes();
    auto progressionRef = actor->progression();
    auto vitalRef = actor->vitals();
    auto traitRef = actor->traits();
    auto classRef = actor->classInfo();
    if (statusRef->container.effectCount() != 0 || skillRef->casting.active)
        return checkpointFailure<std::string>(eve::DiagnosticCode::PreconditionViolation,
                                              "actor checkpoint requires a safe point without active effects or casts");

    eve::Value::Array baseAttributes;
    for (int index = 0; index < attributeRef->values.attributeCount(); ++index) {
        const std::string name = attributeRef->values.attributeAt(index);
        eve::Value::Object entry;
        entry.emplace("id", eve::Value(name));
        entry.emplace("value", eve::Value(attributeRef->values.getBase(name)));
        baseAttributes.emplace_back(std::move(entry));
    }

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
    for (const auto &id : skillIds) learnedSkills.emplace_back(id);

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
    root.emplace("baseAttributes", eve::Value(std::move(baseAttributes)));
    root.emplace("class", eve::Value(std::move(classValue)));
    root.emplace("learnedSkills", eve::Value(std::move(learnedSkills)));
    root.emplace("progression", eve::Value(std::move(progressionValue)));
    root.emplace("schema", eve::Value("eve.rpg.actor-checkpoint"));
    root.emplace("traits", eve::Value(std::move(traitValues)));
    root.emplace("version", eve::Value(1));
    root.emplace("vitals", eve::Value(std::move(vitalsValue)));
    return eve::Value(std::move(root)).toJson();
}

eve::Result<RPGActor::CheckpointCandidate> RPGActor::prepareCheckpointJson(std::string_view json) const {
    RPGActor *actor = const_cast<RPGActor *>(this);
    auto statusRef = actor->statuses();
    auto skillRef = actor->skills();
    auto attributeRef = actor->attributes();
    auto progressionRef = actor->progression();
    auto traitRef = actor->traits();
    auto classRef = actor->classInfo();
    if (statusRef->container.effectCount() != 0 || skillRef->casting.active)
        return checkpointFailure<CheckpointCandidate>(
            eve::DiagnosticCode::PreconditionViolation,
            "actor checkpoint restore requires a clean preconfigured destination");
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<CheckpointCandidate>::failure(parsed.status());
    const eve::Value &root = parsed.value();
    if (!root.isObject())
        return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::ParseError,
                                                      "actor checkpoint root must be an object", "$");
    const eve::Value *schema = root.find("schema");
    if (!schema || !schema->isString() || schema->asString() != "eve.rpg.actor-checkpoint")
        return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::InvalidArgument,
                                                      "snapshot does not belong to RPGActor", "$.schema");
    const eve::Value *version = root.find("version");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::UnknownVersion,
                                                      "unsupported RPG actor checkpoint version", "$.version");

    const eve::Value *encodedClass = root.find("class");
    const eve::Value *encodedTraits = root.find("traits");
    const eve::Value *encodedAttributes = root.find("baseAttributes");
    const eve::Value *encodedProgression = root.find("progression");
    const eve::Value *encodedVitals = root.find("vitals");
    const eve::Value *encodedSkills = root.find("learnedSkills");
    if (!encodedClass || !encodedClass->isObject() || !encodedTraits || !encodedTraits->isArray() ||
        !encodedAttributes || !encodedAttributes->isArray() || !encodedProgression || !encodedProgression->isObject() ||
        !encodedVitals || !encodedVitals->isObject() || !encodedSkills || !encodedSkills->isArray())
        return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::ParseError,
                                                      "actor checkpoint participant fields have invalid types", "$");

    const eve::Value *classId = encodedClass->find("id");
    const eve::Value *skillsSynced = encodedClass->find("skillsSyncedUpTo");
    if (!classId || !classId->isString() || !skillsSynced || !skillsSynced->isInt64() ||
        classId->asString() != classRef->classId || skillsSynced->asInt() < 0)
        return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                                      "actor checkpoint class does not match its destination", "$.class");
    if (encodedTraits->arraySize() != traitRef->active.size())
        return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                                      "actor checkpoint traits do not match its destination", "$.traits");
    for (std::size_t index = 0; index < traitRef->active.size(); ++index) {
        const eve::Value &encoded = encodedTraits->at(index);
        const eve::Value *traitId = encoded.isObject() ? encoded.find("traitId") : nullptr;
        const eve::Value *source = encoded.isObject() ? encoded.find("source") : nullptr;
        if (!traitId || !traitId->isString() || !source || !source->isString() ||
            traitId->asString() != traitRef->active[index].traitId || source->asString() != traitRef->active[index].source)
            return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                                          "actor checkpoint trait identity does not match", "$.traits");
    }

    CheckpointCandidate candidate{*attributeRef, {}, *progressionRef, {}, *classRef};
    candidate.classInfo.skillsSyncedUpTo = static_cast<int>(skillsSynced->asInt());
    if (encodedAttributes->arraySize() != static_cast<std::size_t>(candidate.attributes.values.attributeCount()))
        return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                                      "actor checkpoint attribute layout does not match", "$.baseAttributes");
    std::unordered_set<std::string> seenAttributes;
    for (std::size_t index = 0; index < encodedAttributes->arraySize(); ++index) {
        const eve::Value &encoded = encodedAttributes->at(index);
        const eve::Value *id = encoded.isObject() ? encoded.find("id") : nullptr;
        const eve::Value *value = encoded.isObject() ? encoded.find("value") : nullptr;
        if (!id || !id->isString() || !value || !value->isNumeric() || !std::isfinite(numericValue(*value)))
            return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::ParseError,
                                                          "actor base attribute is invalid", "$.baseAttributes");
        if (!seenAttributes.emplace(id->asString()).second || !candidate.attributes.values.has(id->asString()))
            return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                                          "actor base attribute layout does not match", "$.baseAttributes");
        candidate.attributes.values.setBase(id->asString(), numericValue(*value));
    }

    const eve::Value *level = encodedProgression->find("level");
    const eve::Value *xp = encodedProgression->find("xp");
    const eve::Value *xpToNext = encodedProgression->find("xpToNext");
    if (!level || !level->isInt64() || level->asInt() < 1 || !xp || !xp->isNumeric() ||
        !xpToNext || !xpToNext->isNumeric() || !std::isfinite(numericValue(*xp)) ||
        !std::isfinite(numericValue(*xpToNext)) || numericValue(*xp) < 0.0 ||
        numericValue(*xpToNext) <= numericValue(*xp))
        return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::InvalidArgument,
                                                      "actor checkpoint progression is invalid", "$.progression");
    candidate.progression = {static_cast<int>(level->asInt()), numericValue(*xp), numericValue(*xpToNext)};

    for (const auto &name : encodedVitals->keys()) {
        const eve::Value *value = encodedVitals->find(name);
        if (!value || !value->isNumeric() || !std::isfinite(numericValue(*value)) || numericValue(*value) < 0.0 ||
            numericValue(*value) > candidate.attributes.values.getFinal(name))
            return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::InvalidArgument,
                                                          "actor checkpoint vital is outside its attribute bounds", "$.vitals." + name);
        candidate.vitals.current.emplace(name, numericValue(*value));
    }

    std::unordered_set<std::string> seenSkills;
    for (std::size_t index = 0; index < encodedSkills->arraySize(); ++index) {
        const eve::Value &id = encodedSkills->at(index);
        if (!id.isString() || id.asString().empty() || !seenSkills.emplace(id.asString()).second ||
            !SkillRegistry::find(id.asString()))
            return checkpointFailure<CheckpointCandidate>(eve::DiagnosticCode::NotFound,
                                                          "actor checkpoint skill is missing or invalid", "$.learnedSkills");
        candidate.skills.known.emplace(id.asString(), SkillRuntime{});
    }
    candidate.skills.casting = CastingState{};
    return eve::Result<CheckpointCandidate>::success(std::move(candidate));
}

void RPGActor::commitCheckpoint(CheckpointCandidate candidate) noexcept {
    *attributes() = std::move(candidate.attributes);
    *skills() = std::move(candidate.skills);
    *progression() = candidate.progression;
    *vitals() = std::move(candidate.vitals);
    *classInfo() = std::move(candidate.classInfo);
}

eve::Result<void> RPGActor::restoreCheckpointJson(std::string_view json) {
    auto prepared = prepareCheckpointJson(json);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitCheckpoint(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
