#include "rpg/RPGSaveSession.h"
#include "rpg/Party.h"

#include "common/Snapshot.h"
#include "common/Value.h"
#include "inventory/InventorySaveSession.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/Tracker.h"

#include <algorithm>
#include <optional>
#include <cstdint>
#include <unordered_map>

namespace eve::rpg {

namespace {

template <typename T>
eve::Result<T> saveFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

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

/** Deterministic non-cryptographic corruption detector owned by schema version 1. */
eve::SnapshotHashProvider saveIntegrityProvider() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left ^= byte;
            left *= 1099511628211ull;
            right ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull;
            right *= 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
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
        return saveFailure<void>(eve::DiagnosticCode::ParseError,
                                 "quest-addition migration requires a tracker entries array",
                                 "payload.questTracker.entries");

    Tracker baseline;
    auto baselineJson = baseline.snapshotJson();
    if (!baselineJson.ok()) return eve::Result<void>::failure(baselineJson.status());
    auto parsedBaseline = eve::Value::fromJson(baselineJson.value());
    if (!parsedBaseline.ok()) return eve::Result<void>::failure(parsedBaseline.status());
    eve::Value baselineRoot = std::move(parsedBaseline).takeValue();
    eve::Value *baselineEntries = baselineRoot.find("entries");
    if (!baselineEntries || !baselineEntries->isArray())
        return saveFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                 "current tracker did not produce an entries array", "questTracker.entries");

    std::unordered_map<std::string, eve::Value> oldEntries;
    for (std::size_t index = 0; index < entries->arraySize(); ++index) {
        const eve::Value &entry = entries->at(index);
        const eve::Value *id = entry.isObject() ? entry.find("id") : nullptr;
        if (!id || !id->isString())
            return saveFailure<void>(eve::DiagnosticCode::ParseError,
                                     "quest-addition migration found an invalid old tracker entry",
                                     "payload.questTracker.entries[" + std::to_string(index) + "].id");
        if (!oldEntries.emplace(id->asString(), entry).second)
            return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                     "quest-addition migration found a duplicate old quest id",
                                     "payload.questTracker.entries[" + std::to_string(index) + "].id");
    }
    for (const auto &questId : questIds) {
        if (oldEntries.contains(questId))
            return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                     "quest-addition migration target already exists in the old tracker",
                                     "payload.questTracker.entries." + questId);
    }

    eve::Value::Array rebuilt;
    rebuilt.reserve(baselineEntries->arraySize());
    std::set<std::string> appliedAdditions;
    for (std::size_t index = 0; index < baselineEntries->arraySize(); ++index) {
        const eve::Value &initial = baselineEntries->at(index);
        const eve::Value *id = initial.find("id");
        if (!id || !id->isString())
            return saveFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "current tracker produced an invalid quest id", "questTracker.entries");
        auto old = oldEntries.find(id->asString());
        if (old != oldEntries.end()) {
            rebuilt.push_back(std::move(old->second));
            oldEntries.erase(old);
        } else if (questIds.contains(id->asString())) {
            rebuilt.push_back(initial);
            appliedAdditions.emplace(id->asString());
        } else {
            return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                     "old tracker is missing a quest without an additive migration",
                                     "payload.questTracker.entries." + id->asString());
        }
    }
    if (!oldEntries.empty())
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "old tracker contains a quest absent from the current registry",
                                 "payload.questTracker.entries." + oldEntries.begin()->first);
    if (appliedAdditions != questIds)
        return saveFailure<void>(eve::DiagnosticCode::NotFound,
                                 "quest-addition migration target is absent from the current registry",
                                 "questTracker.entries");
    entries->operator=(eve::Value(std::move(rebuilt)));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace

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
        return saveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "RPG save content version must be a non-empty identifier without controls",
                                 "contentVersion");
    if (!contentVersion_.empty() && contentVersion_ != contentVersion &&
        (!compatibleContentVersions_.empty() || !idRenameMigrations_.empty() ||
         !questAdditionMigrations_.empty() || !singleActorPartyMigrations_.empty()))
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "RPG content version cannot change after migration routes are registered",
                                 "contentVersion");
    contentVersion_ = std::string(contentVersion);
    compatibleContentVersions_.erase(contentVersion_);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::allowCompatibleContentVersion(std::string_view contentVersion) {
    if (!validContentVersion(contentVersion))
        return saveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "compatible RPG content version must be a non-empty identifier without controls",
                                 "contentVersion");
    if (contentVersion == contentVersion_)
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "current RPG content version must not be registered as an older version",
                                 "contentVersion");
    const bool hasMigration = std::any_of(idRenameMigrations_.begin(), idRenameMigrations_.end(),
                                          [&](const auto &entry) {
                                              return entry.first.fromContentVersion == contentVersion;
                                          }) ||
                              questAdditionMigrations_.contains(std::string(contentVersion)) ||
                              singleActorPartyMigrations_.contains(std::string(contentVersion));
    if (hasMigration)
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "RPG content version already has an ID migration route", "contentVersion");
    if (!compatibleContentVersions_.emplace(contentVersion).second)
        return saveFailure<void>(eve::DiagnosticCode::AlreadyExists,
                                 "compatible RPG content version is already registered", "contentVersion");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::addIdRenameMigration(std::string_view fromContentVersion,
                                                        RPGSaveIdDomain domain, std::string_view oldId,
                                                        std::string_view newId) {
    if (contentVersion_.empty())
        return saveFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "current RPG content version must be configured before migration rules",
                                 "contentVersion");
    if (!validContentVersion(fromContentVersion) || !validContentVersion(oldId) ||
        !validContentVersion(newId) || oldId == newId)
        return saveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "RPG ID migration requires distinct valid source and destination identifiers",
                                 "migration");
    if (fromContentVersion == contentVersion_)
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "RPG ID migration source must differ from the current content version",
                                 "fromContentVersion");
    if (compatibleContentVersions_.contains(std::string(fromContentVersion)))
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "RPG content version is already admitted as directly compatible",
                                 "fromContentVersion");
    MigrationKey key{std::string(fromContentVersion), domain, std::string(oldId)};
    if (!idRenameMigrations_.emplace(std::move(key), std::string(newId)).second)
        return saveFailure<void>(eve::DiagnosticCode::AlreadyExists,
                                 "RPG ID migration source is already registered", "migration");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::addQuestAdditionMigration(std::string_view fromContentVersion,
                                                             std::string_view questId) {
    if (contentVersion_.empty())
        return saveFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "current RPG content version must be configured before migration rules",
                                 "contentVersion");
    if (!validContentVersion(fromContentVersion) || !validContentVersion(questId))
        return saveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "quest-addition migration requires valid source and quest identifiers",
                                 "migration");
    if (fromContentVersion == contentVersion_)
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "quest-addition migration source must differ from the current content version",
                                 "fromContentVersion");
    if (compatibleContentVersions_.contains(std::string(fromContentVersion)))
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "RPG content version is already admitted as directly compatible",
                                 "fromContentVersion");
    if (!QuestRegistry::find(std::string(questId)))
        return saveFailure<void>(eve::DiagnosticCode::NotFound,
                                 "quest-addition migration target is not registered", "questId");
    auto &quests = questAdditionMigrations_[std::string(fromContentVersion)];
    if (!quests.emplace(questId).second)
        return saveFailure<void>(eve::DiagnosticCode::AlreadyExists,
                                 "quest-addition migration is already registered", "migration");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RPGSaveSession::allowSingleActorPartyMigration(
    std::string_view fromContentVersion) {
    if (contentVersion_.empty())
        return saveFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "current RPG content version must be configured before migration rules",
                                 "contentVersion");
    if (!validContentVersion(fromContentVersion) || fromContentVersion == contentVersion_)
        return saveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "single-actor party migration requires a distinct valid source version",
                                 "fromContentVersion");
    if (compatibleContentVersions_.contains(std::string(fromContentVersion)))
        return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                 "RPG content version is already admitted as directly compatible",
                                 "fromContentVersion");
    if (!singleActorPartyMigrations_.emplace(fromContentVersion).second)
        return saveFailure<void>(eve::DiagnosticCode::AlreadyExists,
                                 "single-actor party migration is already registered",
                                 "fromContentVersion");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> RPGSaveSession::snapshotJson() const {
    if (!gameState_ || !tracker_ || (!actor_ && !party_) || !bag_ || !equipment_)
        return saveFailure<std::string>(eve::DiagnosticCode::PreconditionViolation,
                                        "RPG save session requires bound participants");
    if (contentVersion_.empty())
        return saveFailure<std::string>(eve::DiagnosticCode::PreconditionViolation,
                                        "RPG save session requires a content version");
    auto gameStateJson = gameState_->snapshotJson();
    if (!gameStateJson.ok()) return eve::Result<std::string>::failure(gameStateJson.status());
    auto trackerJson = tracker_->snapshotJson();
    if (!trackerJson.ok()) return eve::Result<std::string>::failure(trackerJson.status());
    auto actorJson = actor_ ? actor_->checkpointJson() : party_->checkpointJson();
    if (!actorJson.ok()) return eve::Result<std::string>::failure(actorJson.status());
    inventory::InventorySaveSession inventorySession;
    inventorySession.bind(*bag_, *equipment_);
    auto inventoryJson = inventorySession.snapshotJson();
    if (!inventoryJson.ok()) return eve::Result<std::string>::failure(inventoryJson.status());
    auto gameStateValue = eve::Value::fromJson(gameStateJson.value());
    if (!gameStateValue.ok()) return eve::Result<std::string>::failure(gameStateValue.status());
    auto trackerValue = eve::Value::fromJson(trackerJson.value());
    if (!trackerValue.ok()) return eve::Result<std::string>::failure(trackerValue.status());
    auto actorValue = eve::Value::fromJson(actorJson.value());
    if (!actorValue.ok()) return eve::Result<std::string>::failure(actorValue.status());
    auto inventoryValue = eve::Value::fromJson(inventoryJson.value());
    if (!inventoryValue.ok()) return eve::Result<std::string>::failure(inventoryValue.status());
    eve::Value::Object payload;
    payload.emplace(actor_ ? "actor" : "party", std::move(actorValue).takeValue());
    payload.emplace("contentVersion", eve::Value(contentVersion_));
    payload.emplace("gameState", std::move(gameStateValue).takeValue());
    payload.emplace("inventory", std::move(inventoryValue).takeValue());
    payload.emplace("questTracker", std::move(trackerValue).takeValue());
    const eve::SchemaVersion schemaVersion(actor_ ? 1 : 2);
    auto envelope = eve::makeSnapshotEnvelope("rpg.save-session", saveSchema(), schemaVersion,
                                              eve::PersistentId::nil(), eve::Revision(0), eve::SimulationTick(0),
                                              eve::Value(std::move(payload)), saveIntegrityProvider());
    if (!envelope.ok()) return eve::Result<std::string>::failure(envelope.status());
    return eve::serializeSnapshotEnvelope(envelope.value());
}

eve::Result<void> RPGSaveSession::validateSnapshotJson(std::string_view json) {
    return restoreSnapshotJsonImpl(json, false);
}

eve::Result<void> RPGSaveSession::restoreSnapshotJson(std::string_view json) {
    return restoreSnapshotJsonImpl(json, true);
}

eve::Result<void> RPGSaveSession::restoreSnapshotJsonImpl(std::string_view json, bool publish) {
    if (!gameState_ || !tracker_ || (!actor_ && !party_) || !bag_ || !equipment_)
        return saveFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "RPG save session requires bound participants");
    if (contentVersion_.empty())
        return saveFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "RPG save session requires a content version");
    auto parsed = eve::parseSnapshotEnvelope(json, saveIntegrityProvider());
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    const eve::SnapshotEnvelope &envelope = parsed.value();
    if (envelope.type != "rpg.save-session")
        return saveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "snapshot does not belong to RPGSaveSession", "type");
    if (envelope.schema != saveSchema())
        return saveFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "snapshot does not belong to RPGSaveSession", "schema");
    eve::Value root = envelope.payload;
    if (!root.isObject())
        return saveFailure<void>(eve::DiagnosticCode::ParseError, "RPG save session payload must be an object",
                                 "payload");
    eve::Value *contentVersion = root.find("contentVersion");
    if (!contentVersion || !contentVersion->isString())
        return saveFailure<void>(eve::DiagnosticCode::ParseError,
                                 "RPG save-session content version is missing or invalid",
                                 "payload.contentVersion");
    const std::string sourceContentVersion = contentVersion->asString();
    const bool migrateSingleActor = party_ && envelope.schemaVersion == eve::SchemaVersion(1) &&
                                    singleActorPartyMigrations_.contains(sourceContentVersion);
    const eve::SchemaVersion expectedVersion(actor_ ? 1 : 2);
    if (envelope.schemaVersion != expectedVersion && !migrateSingleActor)
        return saveFailure<void>(eve::DiagnosticCode::UnknownVersion,
                                 "unsupported RPG save-session snapshot version", "schemaVersion");
    if (migrateSingleActor) {
        const eve::Value *oldActor = root.find("actor");
        if (!oldActor || !oldActor->isObject())
            return saveFailure<void>(eve::DiagnosticCode::ParseError,
                                     "single-actor migration requires an actor payload", "payload.actor");
        auto baselineJson = party_->checkpointJson();
        if (!baselineJson.ok()) return eve::Result<void>::failure(baselineJson.status());
        auto baseline = eve::Value::fromJson(baselineJson.value());
        if (!baseline.ok()) return eve::Result<void>::failure(baseline.status());
        eve::Value migratedParty = std::move(baseline).takeValue();
        eve::Value *members = migratedParty.find("members");
        if (!members || !members->isArray() || members->arraySize() == 0)
            return saveFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                     "single-actor migration requires a non-empty live party", "party");
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
            if (!added.ok()) return added;
        }
        if (migrateSingleActor) hasMigration = true;
        if (!hasMigration)
            return saveFailure<void>(eve::DiagnosticCode::Conflict,
                                     "RPG save-session content version has no compatible or migration route",
                                     "payload.contentVersion");
        root.set("contentVersion", eve::Value(contentVersion_));
    }
    const eve::Value *gameStateValue = root.find("gameState");
    const eve::Value *trackerValue = root.find("questTracker");
    const eve::Value *actorValue = root.find(actor_ ? "actor" : "party");
    const eve::Value *inventoryValue = root.find("inventory");
    if (!gameStateValue || !gameStateValue->isObject() || !trackerValue || !trackerValue->isObject() ||
        !actorValue || !actorValue->isObject() || !inventoryValue || !inventoryValue->isObject())
        return saveFailure<void>(eve::DiagnosticCode::ParseError,
                                 "RPG save-session participants must be objects", "payload");
    auto gameStateJson = gameStateValue->toJson();
    if (!gameStateJson.ok()) return eve::Result<void>::failure(gameStateJson.status());
    auto trackerJson = trackerValue->toJson();
    if (!trackerJson.ok()) return eve::Result<void>::failure(trackerJson.status());
    auto actorJson = actorValue->toJson();
    if (!actorJson.ok()) return eve::Result<void>::failure(actorJson.status());
    auto inventoryJson = inventoryValue->toJson();
    if (!inventoryJson.ok()) return eve::Result<void>::failure(inventoryJson.status());

    GameState candidateGameState = *gameState_;
    Tracker candidateTracker = *tracker_;
    auto gameStateRestore = candidateGameState.restoreSnapshotJson(gameStateJson.value());
    if (!gameStateRestore.ok()) return gameStateRestore;
    auto trackerRestore = candidateTracker.restoreSnapshotJson(trackerJson.value());
    if (!trackerRestore.ok()) return trackerRestore;
    inventory::InventorySaveSession inventorySession;
    inventorySession.bind(*bag_, *equipment_);
    auto inventoryCandidate = inventorySession.prepareRestoreSnapshotJson(inventoryJson.value());
    if (!inventoryCandidate.ok()) return eve::Result<void>::failure(inventoryCandidate.status());

    std::optional<RPGActor::CheckpointCandidate> actorCandidate;
    std::optional<Party::CheckpointCandidate> partyCandidate;
    if (actor_) {
        auto prepared = actor_->prepareCheckpointJson(actorJson.value());
        if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
        actorCandidate.emplace(std::move(prepared).takeValue());
    } else {
        auto prepared = party_->prepareCheckpointJson(actorJson.value());
        if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
        partyCandidate.emplace(std::move(prepared).takeValue());
    }

    if (!publish) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));

    gameState_->switches_.swap(candidateGameState.switches_);
    gameState_->variables_.swap(candidateGameState.variables_);
    gameState_->selfVariables_.swap(candidateGameState.selfVariables_);
    tracker_->entries.swap(candidateTracker.entries);
    tracker_->order.swap(candidateTracker.order);
    tracker_->pending.swap(candidateTracker.pending);
    tracker_->polled.swap(candidateTracker.polled);
    if (actor_)
        actor_->commitCheckpoint(std::move(*actorCandidate));
    else
        party_->commitCheckpoint(std::move(*partyCandidate));
    inventorySession.commitPrepared(std::move(inventoryCandidate).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
