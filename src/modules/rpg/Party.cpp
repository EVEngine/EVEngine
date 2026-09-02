#include "rpg/Party.h"

#include "rpg/Battle.h"
#include "rpg/VitalsSystem.h"
#include "rpg/RPGActor.h"

#include "common/Value.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::rpg {

namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "rpg.party"));
}

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
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "party member id must be stable, non-empty, and free of controls", "memberId");
    if (!actor)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "party member actor must not be null", "actor");
    const auto handle = ecs::handle_of(actor);
    if (!resolve(handle))
        return failure<void>(eve::DiagnosticCode::StaleHandle,
                             "party member actor is not live", "actor");
    for (const auto &member : members_) {
        if (member.id == memberId)
            return failure<void>(eve::DiagnosticCode::AlreadyExists,
                                 "party member id is already present", "memberId");
        if (sameHandle(member.actor, handle))
            return failure<void>(eve::DiagnosticCode::AlreadyExists,
                                 "actor is already linked to the party", "actor");
    }
    members_.push_back(Member{memberId, handle});
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Party::removeMember(const std::string &memberId) {
    const auto found = std::find_if(members_.begin(), members_.end(), [&](const Member &member) {
        return member.id == memberId;
    });
    if (found == members_.end())
        return failure<void>(eve::DiagnosticCode::NotFound,
                             "party member id is not present", "memberId");
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
        return failure<int>(eve::DiagnosticCode::InvalidArgument,
                            "battle must not be null", "battle");
    if (members_.empty())
        return failure<int>(eve::DiagnosticCode::PreconditionViolation,
                            "party must contain at least one member", "party");
    std::vector<RPGActor *> resolved;
    resolved.reserve(members_.size());
    for (std::size_t index = 0; index < members_.size(); ++index) {
        auto *actor = resolve(members_[index].actor);
        if (!actor)
            return failure<int>(eve::DiagnosticCode::StaleHandle,
                                "party contains a stale member link",
                                "party.members[" + std::to_string(index) + "]");
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
        return failure<int>(eve::DiagnosticCode::PreconditionViolation,
                            "party recovery requires at least one member", "party");
    if (healthResource.empty() || !std::isfinite(healthRatio) || healthRatio <= 0.0 ||
        healthRatio > 1.0)
        return failure<int>(eve::DiagnosticCode::InvalidArgument,
                            "party recovery requires a health resource and ratio in (0, 1]",
                            "healthRatio");
    if ((!secondaryResource.empty() && secondaryResource == healthResource) ||
        !std::isfinite(secondaryRatio) || secondaryRatio < 0.0 || secondaryRatio > 1.0)
        return failure<int>(eve::DiagnosticCode::InvalidArgument,
                            "secondary recovery resource must be distinct and ratio in [0, 1]",
                            "secondaryRatio");
    std::vector<RPGActor *> actors;
    std::vector<double> healthValues;
    std::vector<double> secondaryValues;
    actors.reserve(members_.size());
    healthValues.reserve(members_.size());
    secondaryValues.reserve(members_.size());
    for (std::size_t index = 0; index < members_.size(); ++index) {
        auto *actor = resolve(members_[index].actor);
        if (!actor)
            return failure<int>(eve::DiagnosticCode::StaleHandle,
                                "party recovery found a stale member",
                                "members[" + std::to_string(index) + "]");
        const double healthMax = actor->getMax(healthResource);
        if (!std::isfinite(healthMax) || healthMax <= 0.0)
            return failure<int>(eve::DiagnosticCode::PreconditionViolation,
                                "party member has no positive finite health maximum",
                                "members[" + std::to_string(index) + "]." + healthResource);
        double secondaryMax = 0.0;
        if (!secondaryResource.empty()) {
            secondaryMax = actor->getMax(secondaryResource);
            if (!std::isfinite(secondaryMax) || secondaryMax < 0.0)
                return failure<int>(eve::DiagnosticCode::PreconditionViolation,
                                    "party member has an invalid secondary resource maximum",
                                    "members[" + std::to_string(index) + "]." + secondaryResource);
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

eve::Result<std::string> Party::checkpointJson() const {
    if (members_.empty())
        return failure<std::string>(eve::DiagnosticCode::PreconditionViolation,
                                    "party checkpoint requires at least one member", "party");
    eve::Value::Array memberValues;
    memberValues.reserve(members_.size());
    for (std::size_t index = 0; index < members_.size(); ++index) {
        auto *actor = resolve(members_[index].actor);
        if (!actor)
            return failure<std::string>(eve::DiagnosticCode::StaleHandle,
                                        "party checkpoint found a stale member",
                                        "members[" + std::to_string(index) + "]");
        auto checkpoint = actor->checkpointJson();
        if (!checkpoint.ok()) return eve::Result<std::string>::failure(checkpoint.status());
        auto actorValue = eve::Value::fromJson(checkpoint.value());
        if (!actorValue.ok()) return eve::Result<std::string>::failure(actorValue.status());
        eve::Value::Object member;
        member.emplace("actor", std::move(actorValue).takeValue());
        member.emplace("id", eve::Value(members_[index].id));
        memberValues.emplace_back(eve::Value(std::move(member)));
    }
    eve::Value::Object root;
    root.emplace("members", eve::Value(std::move(memberValues)));
    root.emplace("schema", eve::Value("eve.rpg.party"));
    root.emplace("version", eve::Value(std::int64_t(1)));
    return eve::Value(std::move(root)).toJson();
}

eve::Result<Party::CheckpointCandidate> Party::prepareCheckpointJson(std::string_view json) const {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<CheckpointCandidate>::failure(parsed.status());
    const auto &root = parsed.value();
    const auto *schema = root.isObject() ? root.find("schema") : nullptr;
    const auto *version = root.isObject() ? root.find("version") : nullptr;
    const auto *members = root.isObject() ? root.find("members") : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.rpg.party")
        return failure<CheckpointCandidate>(eve::DiagnosticCode::InvalidArgument,
                                            "checkpoint does not belong to RPG Party", "schema");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return failure<CheckpointCandidate>(eve::DiagnosticCode::UnknownVersion,
                                            "unsupported party checkpoint version", "version");
    if (!members || !members->isArray() || members->arraySize() != members_.size())
        return failure<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                            "checkpoint roster size does not match the live party", "members");
    CheckpointCandidate candidate;
    candidate.actors.reserve(members_.size());
    for (std::size_t index = 0; index < members_.size(); ++index) {
        const auto &member = members->at(index);
        const auto *id = member.isObject() ? member.find("id") : nullptr;
        const auto *actorValue = member.isObject() ? member.find("actor") : nullptr;
        if (!id || !id->isString() || id->asString() != members_[index].id)
            return failure<CheckpointCandidate>(eve::DiagnosticCode::Conflict,
                                                "checkpoint member ID/order does not match the live party",
                                                "members[" + std::to_string(index) + "].id");
        if (!actorValue || !actorValue->isObject())
            return failure<CheckpointCandidate>(eve::DiagnosticCode::ParseError,
                                                "checkpoint member actor must be an object",
                                                "members[" + std::to_string(index) + "].actor");
        auto *actor = resolve(members_[index].actor);
        if (!actor)
            return failure<CheckpointCandidate>(eve::DiagnosticCode::StaleHandle,
                                                "live party contains a stale member",
                                                "members[" + std::to_string(index) + "]");
        auto actorJson = actorValue->toJson();
        if (!actorJson.ok()) return eve::Result<CheckpointCandidate>::failure(actorJson.status());
        auto prepared = actor->prepareCheckpointJson(actorJson.value());
        if (!prepared.ok()) return eve::Result<CheckpointCandidate>::failure(prepared.status());
        candidate.actors.push_back(std::move(prepared).takeValue());
    }
    return eve::Result<CheckpointCandidate>::success(std::move(candidate));
}

void Party::commitCheckpoint(CheckpointCandidate candidate) noexcept {
    for (std::size_t index = 0; index < members_.size(); ++index) {
        if (auto *actor = resolve(members_[index].actor))
            actor->commitCheckpoint(std::move(candidate.actors[index]));
    }
}

eve::Result<void> Party::restoreCheckpointJson(std::string_view json) {
    auto prepared = prepareCheckpointJson(json);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitCheckpoint(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
