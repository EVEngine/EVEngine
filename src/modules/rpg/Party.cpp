#include "rpg/Party.h"

#include "rpg/Battle.h"
#include "rpg/RPGActor.h"
#include "rpg/VitalsSystem.h"
#include "rpg_party_checkpoint.pb.h"

#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::rpg {

namespace {

bool validId(const std::string &value) {
    return !value.empty() && value.size() <= 256 &&
           std::none_of(value.begin(), value.end(), [](unsigned char ch) {
               return ch < 0x20u || ch == 0x7fu;
           });
}

bool sameHandle(const ecs::EntityHandle &left, const ecs::EntityHandle &right) noexcept {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

RPGActor *resolve(const ecs::EntityHandle &handle) noexcept {
    return dynamic_cast<RPGActor *>(ecs::try_get(handle));
}

}  // namespace

eve::Result<void> Party::addMember(const std::string &memberId, RPGActor *actor) {
    if (!validId(memberId))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "party member id must be stable, non-empty, and free of controls",
            "memberId", {}, "rpg.party"));
    if (!actor)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "party member actor must not be null", "actor", {}, "rpg.party"));
    const auto handle = ecs::handle_of(actor);
    if (!resolve(handle))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "party member actor is not live", "actor", {}, "rpg.party"));
    for (const auto &member : members_) {
        if (member.id == memberId)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::AlreadyExists, "party member id is already present", "memberId", {}, "rpg.party"));
        if (sameHandle(member.actor, handle))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::AlreadyExists, "actor is already linked to the party", "actor", {}, "rpg.party"));
    }
    members_.push_back(Member{memberId, handle});
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Party::removeMember(const std::string &memberId) {
    const auto found = std::find_if(members_.begin(), members_.end(), [&](const Member &member) {
        return member.id == memberId;
    });
    if (found == members_.end())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "party member id is not present", "memberId", {}, "rpg.party"));
    members_.erase(found);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void Party::clear() { members_.clear(); }
int Party::count() const { return static_cast<int>(members_.size()); }

bool Party::contains(const std::string &memberId) const {
    return std::any_of(members_.begin(), members_.end(), [&](const Member &member) {
        return member.id == memberId;
    });
}

bool Party::hasStaleMembers() const {
    return std::any_of(members_.begin(), members_.end(), [](const Member &member) {
        return resolve(member.actor) == nullptr;
    });
}

std::string Party::getMemberId(int index) const {
    return index < 0 || static_cast<std::size_t>(index) >= members_.size()
               ? std::string{}
               : members_[static_cast<std::size_t>(index)].id;
}

RPGActor *Party::getMemberActor(int index) const {
    return index < 0 || static_cast<std::size_t>(index) >= members_.size()
               ? nullptr
               : resolve(members_[static_cast<std::size_t>(index)].actor);
}

RPGActor *Party::findMemberActor(const std::string &memberId) const {
    const auto found = std::find_if(members_.begin(), members_.end(), [&](const Member &member) {
        return member.id == memberId;
    });
    return found == members_.end() ? nullptr : resolve(found->actor);
}

eve::Result<int> Party::addToBattle(Battle *battle, int side) const {
    if (!battle)
        return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                "battle must not be null", "battle", {}, "rpg.party"));
    if (members_.empty())
        return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                "party must contain at least one member", "party", {},
                                                                "rpg.party"));
    std::vector<RPGActor *> resolved;
    resolved.reserve(members_.size());
    for (std::size_t index = 0; index < members_.size(); ++index) {
        auto *actor = resolve(members_[index].actor);
        if (!actor)
            return eve::Result<int>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "party contains a stale member link",
                                       "party.members[" + std::to_string(index) + "]", {}, "rpg.party"));
        resolved.push_back(actor);
    }
    for (auto *actor : resolved) battle->addActor(actor, side);
    return eve::Result<int>::success(static_cast<int>(resolved.size()));
}

eve::Result<int> Party::recoverAtCheckpoint(const std::string &healthResource,
                                             double healthRatio,
                                             const std::string &secondaryResource,
                                             double secondaryRatio) {
    if (members_.empty())
        return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                "party recovery requires at least one member", "party",
                                                                {}, "rpg.party"));
    if (healthResource.empty() || !std::isfinite(healthRatio) || healthRatio <= 0.0 ||
        healthRatio > 1.0)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "party recovery requires a health resource and ratio in (0, 1]",
            "healthRatio", {}, "rpg.party"));
    if ((!secondaryResource.empty() && secondaryResource == healthResource) ||
        !std::isfinite(secondaryRatio) || secondaryRatio < 0.0 || secondaryRatio > 1.0)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "secondary recovery resource must be distinct and ratio in [0, 1]",
            "secondaryRatio", {}, "rpg.party"));
    std::vector<RPGActor *> actors;
    std::vector<double> healthValues;
    std::vector<double> secondaryValues;
    actors.reserve(members_.size());
    healthValues.reserve(members_.size());
    secondaryValues.reserve(members_.size());
    for (std::size_t index = 0; index < members_.size(); ++index) {
        auto *actor = resolve(members_[index].actor);
        if (!actor)
            return eve::Result<int>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "party recovery found a stale member",
                                       "members[" + std::to_string(index) + "]", {}, "rpg.party"));
        const double healthMax = actor->getMax(healthResource);
        if (!std::isfinite(healthMax) || healthMax <= 0.0)
            return eve::Result<int>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::PreconditionViolation, "party member has no positive finite health maximum",
                "members[" + std::to_string(index) + "]." + healthResource, {}, "rpg.party"));
        double secondaryMax = 0.0;
        if (!secondaryResource.empty()) {
            secondaryMax = actor->getMax(secondaryResource);
            if (!std::isfinite(secondaryMax) || secondaryMax < 0.0)
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::PreconditionViolation,
                    "party member has an invalid secondary resource maximum",
                    "members[" + std::to_string(index) + "]." + secondaryResource, {}, "rpg.party"));
        }
        actors.push_back(actor);
        healthValues.push_back(healthMax * healthRatio);
        secondaryValues.push_back(secondaryMax * secondaryRatio);
    }
    for (std::size_t index = 0; index < actors.size(); ++index) {
        actors[index]->revive(healthResource, healthValues[index]);
        if (!secondaryResource.empty())
            actors[index]->setCurrent(secondaryResource, secondaryValues[index]);
    }
    return eve::Result<int>::success(static_cast<int>(actors.size()));
}

eve::Result<proto::PartyCheckpointSave> Party::captureCheckpointMessage() const {
    if (members_.empty())
        return eve::Result<proto::PartyCheckpointSave>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "party checkpoint requires at least one member", "party", {}, "rpg.party"));
    proto::PartyCheckpointSave message;
    message.set_schema("eve.rpg.party");
    message.set_version(1);
    for (std::size_t index = 0; index < members_.size(); ++index) {
        auto *actor = resolve(members_[index].actor);
        if (!actor)
            return eve::Result<proto::PartyCheckpointSave>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "party checkpoint found a stale member",
                                       "members[" + std::to_string(index) + "]", {}, "rpg.party"));
        auto checkpoint = actor->captureCheckpointMessage();
        if (!checkpoint.ok()) return eve::Result<proto::PartyCheckpointSave>::failure(checkpoint.status());
        auto *member = message.add_members();
        member->set_id(members_[index].id);
        member->mutable_actor()->CopyFrom(checkpoint.value());
    }
    return eve::Result<proto::PartyCheckpointSave>::success(std::move(message));
}

eve::Result<std::string> Party::checkpointBinary() const {
    auto captured = captureCheckpointMessage();
    if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
    std::string bytes;
    if (!captured.value().SerializeToString(&bytes))
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "party checkpoint protobuf serialization failed"));
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<std::string> Party::checkpointJson() const {
    auto captured = captureCheckpointMessage();
    if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(captured.value(), &json).ok())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                        "party checkpoint JSON serialization failed"));
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<Party::CheckpointCandidate> Party::prepareCheckpointMessage(
    const proto::PartyCheckpointSave &message) const {
    if (!message.has_schema() || message.schema() != "eve.rpg.party")
        return eve::Result<CheckpointCandidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "checkpoint does not belong to RPG Party",
                                   "schema", {}, "rpg.party"));
    if (!message.has_version() || message.version() != 1)
        return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::UnknownVersion, "unsupported party checkpoint version", "version", {}, "rpg.party"));
    if (message.members_size() != static_cast<int>(members_.size()))
        return eve::Result<CheckpointCandidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                   "checkpoint roster size does not match the live party", "members", {}, "rpg.party"));
    CheckpointCandidate candidate;
    candidate.actors.reserve(members_.size());
    for (std::size_t index = 0; index < members_.size(); ++index) {
        const auto &member = message.members(static_cast<int>(index));
        if (!member.has_id() || member.id() != members_[index].id)
            return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "checkpoint member ID/order does not match the live party",
                "members[" + std::to_string(index) + "].id", {}, "rpg.party"));
        if (!member.has_actor())
            return eve::Result<CheckpointCandidate>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "checkpoint member actor is missing",
                                       "members[" + std::to_string(index) + "].actor", {}, "rpg.party"));
        auto *actor = resolve(members_[index].actor);
        if (!actor)
            return eve::Result<CheckpointCandidate>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "live party contains a stale member",
                                       "members[" + std::to_string(index) + "]", {}, "rpg.party"));
        auto prepared = actor->prepareCheckpointMessage(member.actor());
        if (!prepared.ok()) return eve::Result<CheckpointCandidate>::failure(prepared.status());
        candidate.actors.push_back(std::move(prepared).takeValue());
    }
    return eve::Result<CheckpointCandidate>::success(std::move(candidate));
}

eve::Result<Party::CheckpointCandidate> Party::prepareCheckpointJson(std::string_view json) const {
    proto::PartyCheckpointSave               message;
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = true;
    if (!google::protobuf::util::JsonStringToMessage(std::string(json), &message, options).ok())
        return eve::Result<CheckpointCandidate>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "party checkpoint JSON is malformed", "$", {}, "rpg.party"));
    return prepareCheckpointMessage(message);
}

void Party::commitCheckpoint(CheckpointCandidate candidate) noexcept {
    for (std::size_t index = 0; index < members_.size(); ++index) {
        if (auto *actor = resolve(members_[index].actor))
            actor->commitCheckpoint(std::move(candidate.actors[index]));
    }
}

eve::Result<void> Party::restoreCheckpointBinary(std::string_view bytes) {
    proto::PartyCheckpointSave message;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "party checkpoint protobuf is malformed", "$", {}, "rpg.party"));
    auto prepared = prepareCheckpointMessage(message);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitCheckpoint(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Party::restoreCheckpointJson(std::string_view json) {
    auto prepared = prepareCheckpointJson(json);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitCheckpoint(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
}  // namespace eve::rpg
