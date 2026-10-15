#include "rpg/RPGSaveSession.h"
#include "rpg/Party.h"

#include "common/Assert.h"
#include "common/Snapshot.h"
#include "common/Value.h"
#include "inventory/InventorySaveSession.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/Tracker.h"
#include "rpg_save_session.pb.h"

#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace eve::rpg {

namespace {

bool validContentVersion(std::string_view contentVersion) {
    return !contentVersion.empty() && contentVersion.size() <= 256 &&
           std::none_of(contentVersion.begin(), contentVersion.end(), [](unsigned char value) {
               return value < 0x20u || value == 0x7fu;
           });
}

eve::LogicalId saveSchema() {
    const auto schema = eve::LogicalId::parse("rpg:save-session");
    return schema ? *schema : eve::LogicalId{};
}

/** Deterministic non-cryptographic corruption detector; input framing is caller-owned. */
eve::ContentId saveDigest(std::string_view input) {
    std::uint64_t left  = 14695981039346656037ull;
    std::uint64_t right = 1099511628211ull;
    for (const unsigned char byte : input) {
        left ^= byte;
        left *= 1099511628211ull;
        right ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull;
        right *= 14029467366897019727ull;
    }
    eve::ContentId::Bytes bytes{};
    for (int index = 0; index < 8; ++index) {
        bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
        bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
    }
    return eve::ContentId(bytes);
}

eve::SnapshotHashProvider saveIntegrityProvider() {
    return [](std::string_view canonicalJson) -> eve::Result<eve::ContentId> {
        return eve::Result<eve::ContentId>::success(saveDigest(canonicalJson));
    };
}

std::size_t renameStringValue(eve::Value *value, std::string_view oldId, std::string_view newId) {
    if (!value || !value->isString() || value->asString() != oldId) return 0;
    *value = eve::Value(std::string(newId));
    return 1;
}

template <typename Visitor>
std::size_t visitArray(eve::Value *value, Visitor visitor) {
    if (!value || !value->isArray()) return 0;
    std::size_t changed = 0;
    for (std::size_t index = 0; index < value->arraySize(); ++index) changed += visitor(value->at(index));
    return changed;
}

std::size_t applyIdRename(eve::Value &payload, RPGSaveIdDomain domain, std::string_view oldId,
                          std::string_view newId) {
    eve::Value *actor = payload.find("actor");
    eve::Value *tracker = payload.find("questTracker");
    eve::Value *inventory = payload.find("inventory");
    switch (domain) {
        case RPGSaveIdDomain::Item: {
            std::size_t changed = 0;
            eve::Value *bag = inventory ? inventory->find("bag") : nullptr;
            changed += visitArray(bag ? bag->find("slots") : nullptr, [&](eve::Value &stack) {
                return renameStringValue(stack.find("itemId"), oldId, newId);
            });
            eve::Value *equipment = inventory ? inventory->find("equipment") : nullptr;
            changed += visitArray(equipment ? equipment->find("slots") : nullptr, [&](eve::Value &slot) {
                eve::Value *stack = slot.find("stack");
                return renameStringValue(stack ? stack->find("itemId") : nullptr, oldId, newId);
            });
            return changed;
        }
        case RPGSaveIdDomain::Quest:
            return visitArray(tracker ? tracker->find("entries") : nullptr, [&](eve::Value &entry) {
                return renameStringValue(entry.find("id"), oldId, newId);
            });
        case RPGSaveIdDomain::QuestObjective:
            return visitArray(tracker ? tracker->find("entries") : nullptr, [&](eve::Value &entry) {
                return visitArray(entry.find("objectives"), [&](eve::Value &objective) {
                    return renameStringValue(objective.find("id"), oldId, newId);
                });
            });
        case RPGSaveIdDomain::Skill:
            return visitArray(actor ? actor->find("learnedSkills") : nullptr, [&](eve::Value &skill) {
                return renameStringValue(&skill, oldId, newId);
            });
        case RPGSaveIdDomain::Class: {
            eve::Value *classValue = actor ? actor->find("class") : nullptr;
            return renameStringValue(classValue ? classValue->find("id") : nullptr, oldId, newId);
        }
        case RPGSaveIdDomain::Trait:
            return visitArray(actor ? actor->find("traits") : nullptr, [&](eve::Value &trait) {
                return renameStringValue(trait.find("traitId"), oldId, newId);
            });
        case RPGSaveIdDomain::TraitSource:
            return visitArray(actor ? actor->find("traits") : nullptr, [&](eve::Value &trait) {
                return renameStringValue(trait.find("source"), oldId, newId);
            });
        case RPGSaveIdDomain::EquipmentSlot: {
            eve::Value *equipment = inventory ? inventory->find("equipment") : nullptr;
            return visitArray(equipment ? equipment->find("slots") : nullptr, [&](eve::Value &slot) {
                return renameStringValue(slot.find("name"), oldId, newId);
            });
        }
    }
    return 0;
}

eve::Result<void> applyQuestAdditions(eve::Value &payload, const std::set<std::string> &questIds) {
    eve::Value *tracker = payload.find("questTracker");
    eve::Value *entries = tracker ? tracker->find("entries") : nullptr;
    if (!tracker || !tracker->isObject() || !entries || !entries->isArray())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "quest-addition migration requires a tracker entries array",
            "payload.questTracker.entries"));

    Tracker baseline;
    auto baselineJson = baseline.snapshotJson();
    if (!baselineJson.ok()) return eve::Result<void>::failure(baselineJson.status());
    auto parsedBaseline = eve::Value::fromJson(baselineJson.value());
    if (!parsedBaseline.ok()) return eve::Result<void>::failure(parsedBaseline.status());
    eve::Value baselineRoot = std::move(parsedBaseline).takeValue();
    eve::Value *baselineEntries = baselineRoot.find("entries");
    if (!baselineEntries || !baselineEntries->isArray())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                 "current tracker did not produce an entries array",
                                                                 "questTracker.entries"));

    std::unordered_map<std::string, eve::Value> oldEntries;
    for (std::size_t index = 0; index < entries->arraySize(); ++index) {
        const eve::Value &entry = entries->at(index);
        const eve::Value *id = entry.isObject() ? entry.find("id") : nullptr;
        if (!id || !id->isString())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "quest-addition migration found an invalid old tracker entry",
                "payload.questTracker.entries[" + std::to_string(index) + "].id"));
        if (!oldEntries.emplace(id->asString(), entry).second)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "quest-addition migration found a duplicate old quest id",
                "payload.questTracker.entries[" + std::to_string(index) + "].id"));
    }
    for (const auto &questId : questIds) {
        if (oldEntries.contains(questId))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "quest-addition migration target already exists in the old tracker",
                "payload.questTracker.entries." + questId));
    }

    eve::Value::Array rebuilt;
    rebuilt.reserve(baselineEntries->arraySize());
    std::set<std::string> appliedAdditions;
    for (std::size_t index = 0; index < baselineEntries->arraySize(); ++index) {
        const eve::Value &initial = baselineEntries->at(index);
        const eve::Value *id = initial.find("id");
        if (!id || !id->isString())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                     "current tracker produced an invalid quest id",
                                                                     "questTracker.entries"));
        auto old = oldEntries.find(id->asString());
        if (old != oldEntries.end()) {
            rebuilt.push_back(std::move(old->second));
            oldEntries.erase(old);
        } else if (questIds.contains(id->asString())) {
            rebuilt.push_back(initial);
            appliedAdditions.emplace(id->asString());
        } else {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "old tracker is missing a quest without an additive migration",
                "payload.questTracker.entries." + id->asString()));
        }
    }
    if (!oldEntries.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "old tracker contains a quest absent from the current registry",
            "payload.questTracker.entries." + oldEntries.begin()->first));
    if (appliedAdditions != questIds)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "quest-addition migration target is absent from the current registry",
            "questTracker.entries"));
    entries->operator=(eve::Value(std::move(rebuilt)));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> applyIdRename(proto::RPGSavePayload &payload, RPGSaveIdDomain domain, std::string_view oldId,
                                std::string_view newId) {
    if (domain == RPGSaveIdDomain::Item || domain == RPGSaveIdDomain::EquipmentSlot) {
        const auto kind = domain == RPGSaveIdDomain::Item ? inventory::InventorySaveIdKind::Item
                                                          : inventory::InventorySaveIdKind::EquipmentSlot;
        auto       rewritten =
            inventory::InventorySaveSession::rewriteSnapshotBinaryId(payload.inventory().value(), kind, oldId, newId);
        if (!rewritten.ok()) return eve::Result<void>::failure(rewritten.status());
        payload.mutable_inventory()->set_value(std::move(rewritten).takeValue());
        return eve::Result<void>::success();
    }
    if (domain == RPGSaveIdDomain::Quest || domain == RPGSaveIdDomain::QuestObjective) {
        for (auto &entry : *payload.mutable_quest_tracker()->mutable_entries()) {
            if (domain == RPGSaveIdDomain::Quest && entry.id() == oldId) entry.set_id(std::string(newId));
            if (domain == RPGSaveIdDomain::QuestObjective) {
                for (auto &objective : *entry.mutable_objectives()) {
                    if (objective.id() == oldId) objective.set_id(std::string(newId));
                }
            }
        }
        return eve::Result<void>::success();
    }
    std::vector<proto::ActorCheckpointSave *> actors;
    if (payload.has_actor()) actors.push_back(payload.mutable_actor());
    if (payload.has_party()) {
        for (auto &member : *payload.mutable_party()->mutable_members()) actors.push_back(member.mutable_actor());
    }
    for (auto *actor : actors) {
        switch (domain) {
            case RPGSaveIdDomain::Skill:
                for (int index = 0; index < actor->learned_skills_size(); ++index) {
                    if (actor->learned_skills(index) == oldId) actor->set_learned_skills(index, std::string(newId));
                }
                break;
            case RPGSaveIdDomain::Class:
                if (actor->class_info().id() == oldId) actor->mutable_class_info()->set_id(std::string(newId));
                break;
            case RPGSaveIdDomain::Trait:
            case RPGSaveIdDomain::TraitSource:
                for (auto &trait : *actor->mutable_traits()) {
                    if (domain == RPGSaveIdDomain::Trait && trait.trait_id() == oldId)
                        trait.set_trait_id(std::string(newId));
                    if (domain == RPGSaveIdDomain::TraitSource && trait.source() == oldId)
                        trait.set_source(std::string(newId));
                }
                break;
            default: break;
        }
    }
    return eve::Result<void>::success();
}

eve::Result<void> applyQuestAdditions(proto::RPGSavePayload &payload, const std::set<std::string> &questIds) {
    Tracker baseline;
    auto    baselineBytes = baseline.snapshotBinary();
    if (!baselineBytes.ok()) return eve::Result<void>::failure(baselineBytes.status());
    proto::TrackerSave current;
    if (!current.ParseFromString(baselineBytes.value()))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                 "current tracker produced invalid Protobuf bytes"));
    std::unordered_map<std::string, proto::QuestProgress> oldEntries;
    for (const auto &entry : payload.quest_tracker().entries()) {
        if (!entry.has_id() || !oldEntries.emplace(entry.id(), entry).second)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "quest-addition migration found a duplicate old quest id",
                "payload.questTracker.entries"));
    }
    for (const auto &id : questIds) {
        if (oldEntries.contains(id))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "quest-addition target already exists in the old tracker",
                "payload.questTracker.entries." + id));
    }
    auto *entries = payload.mutable_quest_tracker()->mutable_entries();
    entries->Clear();
    std::set<std::string> appliedAdditions;
    for (const auto &initial : current.entries()) {
        auto old = oldEntries.find(initial.id());
        if (old != oldEntries.end()) {
            entries->Add()->CopyFrom(old->second);
            oldEntries.erase(old);
        } else if (questIds.contains(initial.id())) {
            entries->Add()->CopyFrom(initial);
            appliedAdditions.emplace(initial.id());
        } else {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "old tracker is missing a quest without an additive migration",
                "payload.questTracker.entries." + initial.id()));
        }
    }
    if (!oldEntries.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "old tracker contains a quest absent from the current registry",
            "payload.questTracker.entries." + oldEntries.begin()->first));
    if (appliedAdditions != questIds)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "quest-addition target is absent from the current registry",
            "payload.questTracker.entries"));
    return eve::Result<void>::success();
}

}  // namespace

struct RPGSaveSession::PreparedRestore::Impl {
    RPGSaveSession                                  *owner = nullptr;
    GameState                                        gameState;
    Tracker                                          tracker;
    inventory::InventorySaveSession::PreparedRestore inventory;
    std::optional<RPGActor::CheckpointCandidate>     actor;
    std::optional<Party::CheckpointCandidate>        party;

    Impl(RPGSaveSession *session, GameState &&gameStateCandidate, Tracker &&trackerCandidate,
         inventory::InventorySaveSession::PreparedRestore &&inventoryCandidate,
         std::optional<RPGActor::CheckpointCandidate>     &&actorCandidate,
         std::optional<Party::CheckpointCandidate>        &&partyCandidate)
        : owner(session),
          gameState(std::move(gameStateCandidate)),
          tracker(std::move(trackerCandidate)),
          inventory(std::move(inventoryCandidate)),
          actor(std::move(actorCandidate)),
          party(std::move(partyCandidate)) {}
};

RPGSaveSession::PreparedRestore::PreparedRestore(std::unique_ptr<Impl> state) : state_(std::move(state)) {}
RPGSaveSession::PreparedRestore::~PreparedRestore()                                                      = default;
RPGSaveSession::PreparedRestore::PreparedRestore(PreparedRestore &&) noexcept                            = default;
RPGSaveSession::PreparedRestore &RPGSaveSession::PreparedRestore::operator=(PreparedRestore &&) noexcept = default;

void RPGSaveSession::bind(GameState &gameState, Tracker &tracker, RPGActor &actor, inventory::Bag &bag,
                          inventory::EquipmentSet &equipment) noexcept {
    gameState_ = &gameState;
    tracker_ = &tracker;
    actor_ = &actor;
    party_ = nullptr;
    bag_ = &bag;
    equipment_ = &equipment;
}

void RPGSaveSession::bindParty(GameState &gameState, Tracker &tracker, Party &party,
                               inventory::Bag &bag, inventory::EquipmentSet &equipment) noexcept {
    gameState_ = &gameState;
    tracker_ = &tracker;
    actor_ = nullptr;
    party_ = &party;
    bag_ = &bag;
    equipment_ = &equipment;
}

eve::Result<void> RPGSaveSession::setContentVersion(std::string_view contentVersion) {
    if (!validContentVersion(contentVersion))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "RPG save content version must be a non-empty identifier without controls", "contentVersion"));
    if (!contentVersion_.empty() && contentVersion_ != contentVersion &&
        (!compatibleContentVersions_.empty() || !idRenameMigrations_.empty() ||
         !questAdditionMigrations_.empty() || !singleActorPartyMigrations_.empty()))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "RPG content version cannot change after migration routes are registered",
            "contentVersion"));
    contentVersion_ = std::string(contentVersion);
    compatibleContentVersions_.erase(contentVersion_);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::allowCompatibleContentVersion(std::string_view contentVersion) {
    if (!validContentVersion(contentVersion))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "compatible RPG content version must be a non-empty identifier without controls", "contentVersion"));
    if (contentVersion == contentVersion_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "current RPG content version must not be registered as an older version",
            "contentVersion"));
    const bool hasMigration = std::any_of(idRenameMigrations_.begin(), idRenameMigrations_.end(),
                                          [&](const auto &entry) {
                                              return entry.first.fromContentVersion == contentVersion;
                                          }) ||
                              questAdditionMigrations_.contains(std::string(contentVersion)) ||
                              singleActorPartyMigrations_.contains(std::string(contentVersion));
    if (hasMigration)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "RPG content version already has an ID migration route", "contentVersion"));
    if (!compatibleContentVersions_.emplace(contentVersion).second)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::AlreadyExists,
                                                                 "compatible RPG content version is already registered",
                                                                 "contentVersion"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::addIdRenameMigration(std::string_view fromContentVersion,
                                                        RPGSaveIdDomain domain, std::string_view oldId,
                                                        std::string_view newId) {
    if (contentVersion_.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "current RPG content version must be configured before migration rules", "contentVersion"));
    if (!validContentVersion(fromContentVersion) || !validContentVersion(oldId) ||
        !validContentVersion(newId) || oldId == newId)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "RPG ID migration requires distinct valid source and destination identifiers", "migration"));
    if (fromContentVersion == contentVersion_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "RPG ID migration source must differ from the current content version",
            "fromContentVersion"));
    if (compatibleContentVersions_.contains(std::string(fromContentVersion)))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "RPG content version is already admitted as directly compatible",
            "fromContentVersion"));
    MigrationKey key{std::string(fromContentVersion), domain, std::string(oldId)};
    if (!idRenameMigrations_.emplace(std::move(key), std::string(newId)).second)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::AlreadyExists, "RPG ID migration source is already registered", "migration"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::addQuestAdditionMigration(std::string_view fromContentVersion,
                                                             std::string_view questId) {
    if (contentVersion_.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "current RPG content version must be configured before migration rules", "contentVersion"));
    if (!validContentVersion(fromContentVersion) || !validContentVersion(questId))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "quest-addition migration requires valid source and quest identifiers", "migration"));
    if (fromContentVersion == contentVersion_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict,
            "quest-addition migration source must differ from the current content version", "fromContentVersion"));
    if (compatibleContentVersions_.contains(std::string(fromContentVersion)))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "RPG content version is already admitted as directly compatible",
            "fromContentVersion"));
    if (!QuestRegistry::find(std::string(questId)))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "quest-addition migration target is not registered", "questId"));
    auto &quests = questAdditionMigrations_[std::string(fromContentVersion)];
    if (!quests.emplace(questId).second)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::AlreadyExists, "quest-addition migration is already registered", "migration"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::allowSingleActorPartyMigration(
    std::string_view fromContentVersion) {
    if (contentVersion_.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "current RPG content version must be configured before migration rules", "contentVersion"));
    if (!validContentVersion(fromContentVersion) || fromContentVersion == contentVersion_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "single-actor party migration requires a distinct valid source version", "fromContentVersion"));
    if (compatibleContentVersions_.contains(std::string(fromContentVersion)))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "RPG content version is already admitted as directly compatible",
            "fromContentVersion"));
    if (!singleActorPartyMigrations_.emplace(fromContentVersion).second)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::AlreadyExists,
                                                                 "single-actor party migration is already registered",
                                                                 "fromContentVersion"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> RPGSaveSession::snapshotJson() const {
    auto binary = snapshotBinary();
    if (!binary.ok()) return eve::Result<std::string>::failure(binary.status());
    proto::RPGSaveEnvelope envelope;
    if (!envelope.ParseFromString(binary.value()))
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                        "RPG save produced invalid Protobuf envelope"));
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(envelope, &json).ok())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::SerializationError,
                                                                        "RPG save Protobuf JSON conversion failed"));
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<std::string> RPGSaveSession::snapshotBinary() const {
    if (!gameState_ || !tracker_ || (!actor_ && !party_) || !bag_ || !equipment_)
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "RPG save session requires bound participants", {}));
    if (contentVersion_.empty())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "RPG save session requires a content version", {}));

    proto::RPGSavePayload payload;
    payload.set_content_version(contentVersion_);
    auto state = gameState_->captureMessage(*payload.mutable_game_state());
    if (!state.ok()) return eve::Result<std::string>::failure(state.status());
    auto trackerBytes = tracker_->snapshotBinary();
    if (!trackerBytes.ok()) return eve::Result<std::string>::failure(trackerBytes.status());
    if (!payload.mutable_quest_tracker()->ParseFromString(trackerBytes.value()))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "tracker produced invalid protobuf bytes"));
    inventory::InventorySaveSession inventorySession;
    inventorySession.bind(*bag_, *equipment_);
    auto inventoryBytes = inventorySession.snapshotBinary();
    if (!inventoryBytes.ok()) return eve::Result<std::string>::failure(inventoryBytes.status());
    payload.mutable_inventory()->set_type_url("type.googleapis.com/eve.inventory.proto.InventorySave");
    payload.mutable_inventory()->set_value(std::move(inventoryBytes).takeValue());
    if (actor_) {
        auto captured = actor_->captureCheckpointMessage();
        if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
        payload.mutable_actor()->CopyFrom(captured.value());
    } else {
        auto captured = party_->captureCheckpointMessage();
        if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
        payload.mutable_party()->CopyFrom(captured.value());
    }
    std::string payloadBytes;
    if (!payload.SerializeToString(&payloadBytes))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "RPG save payload serialization failed"));
    const auto             digestBytes = saveDigest(payloadBytes).bytes();
    proto::RPGSaveEnvelope envelope;
    envelope.set_type("rpg.save-session");
    envelope.set_schema("rpg:save-session");
    envelope.set_version(3);
    envelope.set_payload_wire(std::move(payloadBytes));
    envelope.set_digest(std::string(reinterpret_cast<const char *>(digestBytes.data()), digestBytes.size()));
    std::string bytes;
    if (!envelope.SerializeToString(&bytes))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "RPG save envelope serialization failed"));
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<void> RPGSaveSession::validateSnapshotJson(std::string_view json) {
    auto prepared = prepareRestoreSnapshotJson(json);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::restoreSnapshotJson(std::string_view json) {
    auto prepared = prepareRestoreSnapshotJson(json);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitPrepared(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<RPGSaveSession::PreparedRestore> RPGSaveSession::prepareRestoreSnapshotJson(std::string_view json) {
    if (!gameState_ || !tracker_ || (!actor_ && !party_) || !bag_ || !equipment_)
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "RPG save session requires bound participants", {}));
    if (contentVersion_.empty())
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "RPG save session requires a content version", {}));
    proto::RPGSaveEnvelope typedEnvelope;
    if (google::protobuf::util::JsonStringToMessage(std::string(json), &typedEnvelope).ok()) {
        std::string bytes;
        if (!typedEnvelope.SerializeToString(&bytes))
            return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::SerializationError, "RPG save Protobuf JSON conversion failed"));
        return prepareRestoreSnapshotBinary(bytes);
    }
    auto parsed = eve::parseSnapshotEnvelope(json, saveIntegrityProvider());
    if (!parsed.ok()) return eve::Result<PreparedRestore>::failure(parsed.status());
    const eve::SnapshotEnvelope &envelope = parsed.value();
    if (envelope.type != "rpg.save-session")
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPGSaveSession", "type"));
    if (envelope.schema != saveSchema())
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPGSaveSession", "schema"));
    eve::Value root = envelope.payload;
    if (!root.isObject())
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "RPG save session payload must be an object", "payload"));
    eve::Value *contentVersion = root.find("contentVersion");
    if (!contentVersion || !contentVersion->isString())
        return eve::Result<PreparedRestore>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                   "RPG save-session content version is missing or invalid", "payload.contentVersion"));
    const std::string sourceContentVersion = contentVersion->asString();
    const bool migrateSingleActor = party_ && envelope.schemaVersion == eve::SchemaVersion(1) &&
                                    singleActorPartyMigrations_.contains(sourceContentVersion);
    const eve::SchemaVersion expectedVersion(actor_ ? 1 : 2);
    if (envelope.schemaVersion != expectedVersion && !migrateSingleActor)
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::UnknownVersion, "unsupported RPG save-session snapshot version", "schemaVersion"));
    if (migrateSingleActor) {
        const eve::Value *oldActor = root.find("actor");
        if (!oldActor || !oldActor->isObject())
            return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "single-actor migration requires an actor payload", "payload.actor"));
        auto baselineJson = party_->checkpointJson();
        if (!baselineJson.ok()) return eve::Result<PreparedRestore>::failure(baselineJson.status());
        auto baseline = eve::Value::fromJson(baselineJson.value());
        if (!baseline.ok()) return eve::Result<PreparedRestore>::failure(baseline.status());
        eve::Value migratedParty = std::move(baseline).takeValue();
        eve::Value *members = migratedParty.find("members");
        if (!members || !members->isArray() || members->arraySize() == 0)
            return eve::Result<PreparedRestore>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                       "single-actor migration requires a non-empty live party", "party"));
        members->at(0).set("actor", *oldActor);
        root.set("party", std::move(migratedParty));
    }
    if (sourceContentVersion != contentVersion_ &&
        !compatibleContentVersions_.contains(sourceContentVersion)) {
        bool hasMigration = false;
        for (const auto &[key, destination] : idRenameMigrations_) {
            if (key.fromContentVersion != sourceContentVersion) continue;
            hasMigration = true;
            applyIdRename(root, key.domain, key.oldId, destination);
        }
        const auto questAdditions = questAdditionMigrations_.find(sourceContentVersion);
        if (questAdditions != questAdditionMigrations_.end()) {
            hasMigration = true;
            auto added = applyQuestAdditions(root, questAdditions->second);
            if (!added.ok()) return eve::Result<PreparedRestore>::failure(added.status());
        }
        if (migrateSingleActor) hasMigration = true;
        if (!hasMigration)
            return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "RPG save-session content version has no compatible or migration route",
                "payload.contentVersion"));
        root.set("contentVersion", eve::Value(contentVersion_));
    }
    const eve::Value *gameStateValue = root.find("gameState");
    const eve::Value *trackerValue = root.find("questTracker");
    const eve::Value *actorValue = root.find(actor_ ? "actor" : "party");
    const eve::Value *inventoryValue = root.find("inventory");
    if (!gameStateValue || !gameStateValue->isObject() || !trackerValue || !trackerValue->isObject() ||
        !actorValue || !actorValue->isObject() || !inventoryValue || !inventoryValue->isObject())
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "RPG save-session participants must be objects", "payload"));
    auto gameStateJson = gameStateValue->toJson();
    if (!gameStateJson.ok()) return eve::Result<PreparedRestore>::failure(gameStateJson.status());
    auto trackerJson = trackerValue->toJson();
    if (!trackerJson.ok()) return eve::Result<PreparedRestore>::failure(trackerJson.status());
    auto actorJson = actorValue->toJson();
    if (!actorJson.ok()) return eve::Result<PreparedRestore>::failure(actorJson.status());
    auto inventoryJson = inventoryValue->toJson();
    if (!inventoryJson.ok()) return eve::Result<PreparedRestore>::failure(inventoryJson.status());

    GameState candidateGameState = *gameState_;
    Tracker candidateTracker = *tracker_;
    auto gameStateRestore = candidateGameState.restoreSnapshotJson(gameStateJson.value());
    if (!gameStateRestore.ok()) return eve::Result<PreparedRestore>::failure(gameStateRestore.status());
    auto trackerRestore = candidateTracker.restoreSnapshotJson(trackerJson.value());
    if (!trackerRestore.ok()) return eve::Result<PreparedRestore>::failure(trackerRestore.status());
    inventory::InventorySaveSession inventorySession;
    inventorySession.bind(*bag_, *equipment_);
    auto inventoryCandidate = inventorySession.prepareRestoreSnapshotJson(inventoryJson.value());
    if (!inventoryCandidate.ok()) return eve::Result<PreparedRestore>::failure(inventoryCandidate.status());

    std::optional<RPGActor::CheckpointCandidate> actorCandidate;
    std::optional<Party::CheckpointCandidate> partyCandidate;
    if (actor_) {
        auto prepared = actor_->prepareCheckpointJson(actorJson.value());
        if (!prepared.ok()) return eve::Result<PreparedRestore>::failure(prepared.status());
        actorCandidate.emplace(std::move(prepared).takeValue());
    } else {
        auto prepared = party_->prepareCheckpointJson(actorJson.value());
        if (!prepared.ok()) return eve::Result<PreparedRestore>::failure(prepared.status());
        partyCandidate.emplace(std::move(prepared).takeValue());
    }

    auto state = std::make_unique<PreparedRestore::Impl>(
        this, std::move(candidateGameState), std::move(candidateTracker), std::move(inventoryCandidate).takeValue(),
        std::move(actorCandidate), std::move(partyCandidate));
    return eve::Result<PreparedRestore>::success(PreparedRestore(std::move(state)));
}

eve::Result<RPGSaveSession::PreparedRestore> RPGSaveSession::prepareRestoreSnapshotBinary(std::string_view bytes) {
    if (!gameState_ || !tracker_ || (!actor_ && !party_) || !bag_ || !equipment_)
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "RPG save session requires bound participants", {}));
    if (contentVersion_.empty())
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "RPG save session requires a content version", {}));
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return eve::Result<PreparedRestore>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "RPG save snapshot is too large"));
    proto::RPGSaveEnvelope envelope;
    if (!envelope.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return eve::Result<PreparedRestore>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "RPG save Protobuf envelope is malformed"));
    if (!envelope.GetReflection()->GetUnknownFields(envelope).empty())
        return eve::Result<PreparedRestore>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "RPG save envelope has unknown fields"));
    if (!envelope.has_type() || envelope.type() != "rpg.save-session" || !envelope.has_schema() ||
        envelope.schema() != "rpg:save-session")
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPGSaveSession", "schema"));
    if (!envelope.has_version() || envelope.version() != 3)
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::UnknownVersion, "unsupported RPG save-session snapshot version", "version"));
    if (envelope.digest().size() != 16)
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "RPG save digest is missing or malformed", "digest"));
    const auto digestBytes = saveDigest(envelope.payload_wire()).bytes();
    if (envelope.digest() != std::string(reinterpret_cast<const char *>(digestBytes.data()), digestBytes.size()))
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::HashMismatch, "RPG save payload hash does not match", "digest"));
    proto::RPGSavePayload payload;
    if (!payload.ParseFromString(envelope.payload_wire()) ||
        !payload.GetReflection()->GetUnknownFields(payload).empty())
        return eve::Result<PreparedRestore>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "RPG save payload is malformed", "payload"));
    if (!payload.has_content_version() || !validContentVersion(payload.content_version()) ||
        !payload.has_game_state() || !payload.has_quest_tracker() || !payload.has_inventory() ||
        !payload.inventory().GetReflection()->GetUnknownFields(payload.inventory()).empty() ||
        payload.inventory().type_url() != "type.googleapis.com/eve.inventory.proto.InventorySave")
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "RPG save participants are missing or invalid", "payload"));
    const std::string sourceContentVersion = payload.content_version();
    const bool        migrateSingleActor =
        party_ && payload.has_actor() && singleActorPartyMigrations_.contains(sourceContentVersion);
    if (migrateSingleActor) {
        const proto::ActorCheckpointSave oldActor = payload.actor();
        auto                             baseline = party_->captureCheckpointMessage();
        if (!baseline.ok()) return eve::Result<PreparedRestore>::failure(baseline.status());
        if (baseline.value().members_size() == 0)
            return eve::Result<PreparedRestore>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                       "single-actor migration requires a non-empty live party", "party"));
        payload.mutable_party()->CopyFrom(baseline.value());
        payload.mutable_party()->mutable_members(0)->mutable_actor()->CopyFrom(oldActor);
    }
    if (sourceContentVersion != contentVersion_ && !compatibleContentVersions_.contains(sourceContentVersion)) {
        bool hasMigration = migrateSingleActor;
        for (const auto &[key, destination] : idRenameMigrations_) {
            if (key.fromContentVersion != sourceContentVersion) continue;
            hasMigration = true;
            auto renamed = applyIdRename(payload, key.domain, key.oldId, destination);
            if (!renamed.ok()) return eve::Result<PreparedRestore>::failure(renamed.status());
        }
        const auto additions = questAdditionMigrations_.find(sourceContentVersion);
        if (additions != questAdditionMigrations_.end()) {
            hasMigration = true;
            auto added   = applyQuestAdditions(payload, additions->second);
            if (!added.ok()) return eve::Result<PreparedRestore>::failure(added.status());
        }
        if (!hasMigration)
            return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "RPG save content version has no compatible or migration route",
                "payload.contentVersion"));
        payload.set_content_version(contentVersion_);
    }
    if (actor_ ? !payload.has_actor() : !payload.has_party())
        return eve::Result<PreparedRestore>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "RPG save roster mode does not match the destination", "payload"));

    GameState candidateGameState = *gameState_;
    Tracker   candidateTracker   = *tracker_;
    auto      restoredState      = candidateGameState.restoreMessage(payload.game_state());
    if (!restoredState.ok()) return eve::Result<PreparedRestore>::failure(restoredState.status());
    std::string trackerBytes;
    if (!payload.quest_tracker().SerializeToString(&trackerBytes))
        return eve::Result<PreparedRestore>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "RPG save quest tracker is invalid"));
    auto restoredTracker = candidateTracker.restoreSnapshotBinary(trackerBytes);
    if (!restoredTracker.ok()) return eve::Result<PreparedRestore>::failure(restoredTracker.status());
    inventory::InventorySaveSession inventorySession;
    inventorySession.bind(*bag_, *equipment_);
    auto inventoryCandidate = inventorySession.prepareRestoreSnapshotBinary(payload.inventory().value());
    if (!inventoryCandidate.ok()) return eve::Result<PreparedRestore>::failure(inventoryCandidate.status());
    std::optional<RPGActor::CheckpointCandidate> actorCandidate;
    std::optional<Party::CheckpointCandidate>    partyCandidate;
    if (actor_) {
        auto prepared = actor_->prepareCheckpointMessage(payload.actor());
        if (!prepared.ok()) return eve::Result<PreparedRestore>::failure(prepared.status());
        actorCandidate.emplace(std::move(prepared).takeValue());
    } else {
        auto prepared = party_->prepareCheckpointMessage(payload.party());
        if (!prepared.ok()) return eve::Result<PreparedRestore>::failure(prepared.status());
        partyCandidate.emplace(std::move(prepared).takeValue());
    }
    auto state = std::make_unique<PreparedRestore::Impl>(
        this, std::move(candidateGameState), std::move(candidateTracker), std::move(inventoryCandidate).takeValue(),
        std::move(actorCandidate), std::move(partyCandidate));
    return eve::Result<PreparedRestore>::success(PreparedRestore(std::move(state)));
}

eve::Result<void> RPGSaveSession::restoreSnapshotBinary(std::string_view bytes) {
    auto prepared = prepareRestoreSnapshotBinary(bytes);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitPrepared(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void RPGSaveSession::commitPrepared(PreparedRestore prepared) noexcept {
    auto &state = *prepared.state_;
    EV_ASSERT(state.owner == this, "RPG prepared restore belongs to another session");
    gameState_->switches_.swap(state.gameState.switches_);
    gameState_->variables_.swap(state.gameState.variables_);
    gameState_->selfVariables_.swap(state.gameState.selfVariables_);
    gameState_->selfStrings_.swap(state.gameState.selfStrings_);
    tracker_->entries.swap(state.tracker.entries);
    tracker_->order.swap(state.tracker.order);
    tracker_->pending.swap(state.tracker.pending);
    tracker_->polled.swap(state.tracker.polled);
    if (actor_)
        actor_->commitCheckpoint(std::move(*state.actor));
    else
        party_->commitCheckpoint(std::move(*state.party));
    inventory::InventorySaveSession inventorySession;
    inventorySession.bind(*bag_, *equipment_);
    inventorySession.commitPrepared(std::move(state.inventory));
}

}  // namespace eve::rpg
