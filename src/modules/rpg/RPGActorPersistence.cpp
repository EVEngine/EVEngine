#include "rpg/RPGActor.h"

#include "rpg/Skill.h"
#include "rpg_actor_checkpoint.pb.h"

#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace eve::rpg {

namespace {

template <typename T>
eve::Result<T> fail(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

eve::Result<proto::ActorCheckpointSave> RPGActor::captureCheckpointMessage() const {
    RPGActor *actor = const_cast<RPGActor *>(this);
    auto statusRef = actor->statuses();
    auto skillRef = actor->skills();
    auto attributeRef = actor->attributes();
    auto progressionRef = actor->progression();
    auto vitalRef = actor->vitals();
    auto traitRef = actor->traits();
    auto classRef = actor->classInfo();
    if (statusRef->container.effectCount() != 0 || skillRef->casting.active)
        return fail<proto::ActorCheckpointSave>(
            eve::DiagnosticCode::PreconditionViolation,
            "actor checkpoint requires a safe point without active effects or casts");
    proto::ActorCheckpointSave message;
    message.set_schema("eve.rpg.actor-checkpoint");
    message.set_version(1);
    for (int index = 0; index < attributeRef->values.attributeCount(); ++index) {
        const std::string name = attributeRef->values.attributeAt(index);
        auto             *entry = message.add_base_attributes();
        entry->set_id(name);
        entry->set_value(attributeRef->values.getBase(name));
    }
    auto *progression = message.mutable_progression();
    progression->set_level(progressionRef->level);
    progression->set_xp(progressionRef->xp);
    progression->set_xp_to_next(progressionRef->xpToNext);
    for (const auto &[name, value] : vitalRef->current) (*message.mutable_vitals())[name] = value;
    std::vector<std::string> skillIds;
    skillIds.reserve(skillRef->known.size());
    for (const auto &[id, runtime] : skillRef->known) {
        (void)runtime;
        skillIds.push_back(id);
    }
    std::sort(skillIds.begin(), skillIds.end());
    for (const auto &id : skillIds) message.add_learned_skills(id);
    for (const auto &trait : traitRef->active) {
        auto *entry = message.add_traits();
        entry->set_source(trait.source);
        entry->set_trait_id(trait.traitId);
    }
    auto *classInfo = message.mutable_class_info();
    classInfo->set_id(classRef->classId);
    classInfo->set_skills_synced_up_to(classRef->skillsSyncedUpTo);
    return eve::Result<proto::ActorCheckpointSave>::success(std::move(message));
}

eve::Result<std::string> RPGActor::checkpointBinary() const {
    auto captured = captureCheckpointMessage();
    if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
    std::string bytes;
    if (!captured.value().SerializeToString(&bytes))
        return fail<std::string>(eve::DiagnosticCode::InvariantViolation,
                                 "actor checkpoint protobuf serialization failed");
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<std::string> RPGActor::checkpointJson() const {
    auto captured = captureCheckpointMessage();
    if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(captured.value(), &json).ok())
        return fail<std::string>(eve::DiagnosticCode::InvariantViolation, "actor checkpoint JSON serialization failed");
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<RPGActor::CheckpointCandidate> RPGActor::prepareCheckpointMessage(
    const proto::ActorCheckpointSave &message) const {
    RPGActor *actor = const_cast<RPGActor *>(this);
    auto statusRef = actor->statuses();
    auto skillRef = actor->skills();
    auto attributeRef = actor->attributes();
    auto progressionRef = actor->progression();
    auto traitRef = actor->traits();
    auto classRef = actor->classInfo();
    if (statusRef->container.effectCount() != 0 || skillRef->casting.active)
        return fail<CheckpointCandidate>(eve::DiagnosticCode::PreconditionViolation,
                                         "actor checkpoint restore requires a clean preconfigured destination");
    if (!message.has_schema() || message.schema() != "eve.rpg.actor-checkpoint")
        return fail<CheckpointCandidate>(eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPGActor",
                                         "$.schema");
    if (!message.has_version() || message.version() != 1)
        return fail<CheckpointCandidate>(eve::DiagnosticCode::UnknownVersion,
                                         "unsupported RPG actor checkpoint version", "$.version");
    if (!message.has_class_info() || !message.class_info().has_id() ||
        !message.class_info().has_skills_synced_up_to() || message.class_info().id() != classRef->classId ||
        message.class_info().skills_synced_up_to() < 0)
        return fail<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                         "actor checkpoint class does not match its destination", "$.class");
    if (message.traits_size() != static_cast<int>(traitRef->active.size()))
        return fail<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                         "actor checkpoint traits do not match its destination", "$.traits");
    for (int index = 0; index < message.traits_size(); ++index) {
        const auto &encoded  = message.traits(index);
        const auto &expected = traitRef->active[static_cast<std::size_t>(index)];
        if (!encoded.has_trait_id() || !encoded.has_source() || encoded.trait_id() != expected.traitId ||
            encoded.source() != expected.source)
            return fail<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                             "actor checkpoint trait identity does not match", "$.traits");
    }
    CheckpointCandidate candidate{*attributeRef, {}, *progressionRef, {}, *classRef};
    candidate.classInfo.skillsSyncedUpTo = message.class_info().skills_synced_up_to();
    if (message.base_attributes_size() != candidate.attributes.values.attributeCount())
        return fail<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                         "actor checkpoint attribute layout does not match", "$.baseAttributes");
    std::unordered_set<std::string> seenAttributes;
    for (const auto &encoded : message.base_attributes()) {
        if (!encoded.has_id() || !encoded.has_value() || !std::isfinite(encoded.value()))
            return fail<CheckpointCandidate>(eve::DiagnosticCode::ParseError, "actor base attribute is invalid",
                                             "$.baseAttributes");
        if (!seenAttributes.emplace(encoded.id()).second || !candidate.attributes.values.has(encoded.id()))
            return fail<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                             "actor base attribute layout does not match", "$.baseAttributes");
        candidate.attributes.values.setBase(encoded.id(), encoded.value());
    }
    if (!message.has_progression() || !message.progression().has_level() || !message.progression().has_xp() ||
        !message.progression().has_xp_to_next() || message.progression().level() < 1 ||
        !std::isfinite(message.progression().xp()) || !std::isfinite(message.progression().xp_to_next()) ||
        message.progression().xp() < 0.0 || message.progression().xp_to_next() <= message.progression().xp())
        return fail<CheckpointCandidate>(eve::DiagnosticCode::InvalidArgument,
                                         "actor checkpoint progression is invalid", "$.progression");
    candidate.progression = {message.progression().level(), message.progression().xp(),
                             message.progression().xp_to_next()};
    for (const auto &[name, value] : message.vitals()) {
        if (!std::isfinite(value) || value < 0.0 || value > candidate.attributes.values.getFinal(name))
            return fail<CheckpointCandidate>(eve::DiagnosticCode::InvalidArgument,
                                             "actor checkpoint vital is outside its attribute bounds",
                                             "$.vitals." + name);
        candidate.vitals.current.emplace(name, value);
    }
    std::unordered_set<std::string> seenSkills;
    for (const auto &id : message.learned_skills()) {
        if (id.empty() || !seenSkills.emplace(id).second || !SkillRegistry::find(id))
            return fail<CheckpointCandidate>(eve::DiagnosticCode::NotFound,
                                             "actor checkpoint skill is missing or invalid", "$.learnedSkills");
        candidate.skills.known.emplace(id, SkillRuntime{});
    }
    candidate.skills.casting = CastingState{};
    return eve::Result<CheckpointCandidate>::success(std::move(candidate));
}

eve::Result<RPGActor::CheckpointCandidate> RPGActor::prepareCheckpointJson(std::string_view json) const {
    proto::ActorCheckpointSave               message;
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = true;
    if (!google::protobuf::util::JsonStringToMessage(std::string(json), &message, options).ok())
        return fail<CheckpointCandidate>(eve::DiagnosticCode::ParseError, "actor checkpoint JSON is malformed", "$");
    return prepareCheckpointMessage(message);
}

void RPGActor::commitCheckpoint(CheckpointCandidate candidate) noexcept {
    *attributes() = std::move(candidate.attributes);
    *skills() = std::move(candidate.skills);
    *progression() = candidate.progression;
    *vitals() = std::move(candidate.vitals);
    *classInfo() = std::move(candidate.classInfo);
}

eve::Result<void> RPGActor::restoreCheckpointBinary(std::string_view bytes) {
    proto::ActorCheckpointSave message;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return fail<void>(eve::DiagnosticCode::ParseError, "actor checkpoint protobuf is malformed");
    auto prepared = prepareCheckpointMessage(message);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitCheckpoint(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGActor::restoreCheckpointJson(std::string_view json) {
    auto prepared = prepareCheckpointJson(json);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitCheckpoint(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
