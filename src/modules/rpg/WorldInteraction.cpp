#include "rpg/WorldInteraction.h"
#include <cmath>
#include <optional>
#include <string>
#include "inventory/InventorySystem.h"
#include "rpg/GameState.h"
#include "rpg/Tracker.h"
#include "rpg/WorldState.h"

// WorldInteraction.cpp
namespace eve::rpg {
namespace {

eve::Result<int> interactionFailure(eve::DiagnosticCode code, std::string message, std::string path,
                                    const WorldLootRequest &request) {
    return eve::Result<int>::failure(eve::Diagnostic::error(
        code, message, path, {{"mapId", request.mapId}, {"objectId", request.objectId}}, "rpg.world-loot"));
}

struct TrackerRollback {
    std::unordered_map<std::string, QuestRuntime> entries;
    std::vector<std::string>                      order;
    std::vector<QuestEvent>                       pending;
    std::vector<QuestEvent>                       polled;
};

void restoreTracker(Tracker &tracker, TrackerRollback rollback) {
    tracker.entries = std::move(rollback.entries);
    tracker.order   = std::move(rollback.order);
    tracker.pending = std::move(rollback.pending);
    tracker.polled  = std::move(rollback.polled);
}

}  // namespace

eve::Result<int> WorldInteraction::collectLoot(GameState &gameState, Tracker &tracker, inventory::Bag &bag,
                                               const WorldLootRequest &request) {
    const bool hasItem      = !request.itemId.empty() || request.itemQuantity != 0;
    const bool hasAttribute = !request.attributeId.empty() || request.attributeAmount != 0.0;
    const bool hasNotification =
        !request.notifyTopic.empty() || !request.notifyTarget.empty() || request.notifyAmount != 0;
    if (!hasItem && !hasAttribute && !hasNotification)
        return interactionFailure(eve::DiagnosticCode::InvalidArgument, "world loot must contain at least one effect",
                                  "request", request);
    if (hasItem && (request.itemId.empty() || request.itemQuantity <= 0))
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "item loot requires an id and positive quantity", "item", request);
    if (hasAttribute &&
        (request.attributeId.empty() || !std::isfinite(request.attributeAmount) || request.attributeAmount <= 0.0))
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "attribute loot requires an id and finite positive amount", "attribute", request);
    if (hasAttribute && !std::isfinite(gameState.getVariable(request.attributeId) + request.attributeAmount))
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "attribute loot would produce a non-finite value", "attribute.amount", request);
    if (hasNotification && (request.notifyTopic.empty() || request.notifyTarget.empty() || request.notifyAmount <= 0))
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "quest notification requires topic, target, and positive amount", "notification",
                                  request);
    if (!request.requiredQuestId.empty() && tracker.getState(request.requiredQuestId) != "active")
        return interactionFailure(eve::DiagnosticCode::PreconditionViolation,
                                  "required quest must be active before collecting world loot", "requiredQuestId",
                                  request);

    auto gameStateBefore = gameState.snapshotJson();
    if (!gameStateBefore.ok()) return eve::Result<int>::failure(gameStateBefore.status());
    std::string gameStateSnapshot = std::move(gameStateBefore).takeValue();
    GameState   candidateState;
    auto        candidateRestore = candidateState.restoreSnapshotJson(gameStateSnapshot);
    if (!candidateRestore.ok()) return eve::Result<int>::failure(candidateRestore.status());
    WorldState candidateWorld(candidateState);
    if (candidateWorld.isObjectConsumed(request.mapId, request.objectId))
        return interactionFailure(eve::DiagnosticCode::Conflict, "world loot object was already consumed", "objectId",
                                  request);
    auto candidateConsume = candidateWorld.consumeObject(request.mapId, request.objectId);
    if (!candidateConsume.ok()) return eve::Result<int>::failure(candidateConsume.status());

    std::optional<inventory::PreparedInventoryAdd> preparedInventory;
    if (hasItem) {
        auto prepared = inventory::InventorySystem::prepareAddBatch(&bag, {{request.itemId, request.itemQuantity}});
        if (!prepared.ok()) return eve::Result<int>::failure(prepared.status());
        preparedInventory.emplace(std::move(prepared).takeValue());
    }

    TrackerRollback trackerBefore{tracker.entries, tracker.order, tracker.pending, tracker.polled};
    if (hasAttribute) gameState.addVariable(request.attributeId, request.attributeAmount);
    if (hasNotification) tracker.notify(request.notifyTopic, request.notifyTarget, request.notifyAmount);
    WorldState world(gameState);
    auto       consumed = world.consumeObject(request.mapId, request.objectId);
    if (!consumed.ok() || consumed.code() != eve::StatusCode::Applied) {
        auto       restored   = gameState.restoreSnapshotJson(gameStateSnapshot);
        const bool restoredOk = restored.ok();
        restoreTracker(tracker, std::move(trackerBefore));
        if (!restoredOk)
            return interactionFailure(eve::DiagnosticCode::InvariantViolation,
                                      "world loot rollback could not restore GameState", "rollback", request);
        return interactionFailure(eve::DiagnosticCode::Conflict, "world loot object changed before commit", "objectId",
                                  request);
    }

    if (preparedInventory) {
        auto committed = inventory::InventorySystem::commitAddBatch(std::move(*preparedInventory));
        if (!committed.ok()) {
            auto       restored   = gameState.restoreSnapshotJson(gameStateSnapshot);
            const bool restoredOk = restored.ok();
            restoreTracker(tracker, std::move(trackerBefore));
            if (!restoredOk)
                return interactionFailure(eve::DiagnosticCode::InvariantViolation,
                                          "world loot rollback could not restore GameState", "rollback", request);
            return eve::Result<int>::failure(committed.status());
        }
    }

    return eve::Result<int>::success(
        static_cast<int>(hasItem) + static_cast<int>(hasAttribute) + static_cast<int>(hasNotification) + 1,
        eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg

// WorldState.cpp
namespace eve::rpg {
namespace {

constexpr std::size_t maxIdBytes = 256;

bool validId(std::string_view id) {
    if (id.empty() || id.size() > maxIdBytes) return false;
    for (unsigned char character : id)
        if (character < 0x20 || character == 0x7f) return false;
    return true;
}

std::string scope(std::string_view mapId) { return "world.object:" + std::string(mapId); }

}  // namespace

bool WorldState::isObjectConsumed(std::string_view mapId, std::string_view objectId) const {
    if (!gameState_ || !validId(mapId) || !validId(objectId)) return false;
    const std::string owningScope = scope(mapId);
    const std::string owningObjectId(objectId);
    return gameState_->hasSelfVariable(owningScope, owningObjectId) &&
           gameState_->getSelfVariable(owningScope, owningObjectId) != 0.0;
}

eve::Result<void> WorldState::consumeObject(std::string_view mapId, std::string_view objectId) {
    if (!validId(mapId))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "world-state IDs must be non-empty, bounded, and control-free",
            "mapId", {}, "rpg.world-state"));
    if (!validId(objectId))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "world-state IDs must be non-empty, bounded, and control-free",
            "objectId", {}, "rpg.world-state"));
    if (isObjectConsumed(mapId, objectId))
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    gameState_->setSelfVariable(scope(mapId), std::string(objectId), 1.0);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorldState::resetObject(std::string_view mapId, std::string_view objectId) {
    if (!validId(mapId))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "world-state IDs must be non-empty, bounded, and control-free",
            "mapId", {}, "rpg.world-state"));
    if (!validId(objectId))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "world-state IDs must be non-empty, bounded, and control-free",
            "objectId", {}, "rpg.world-state"));
    if (!isObjectConsumed(mapId, objectId))
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    gameState_->setSelfVariable(scope(mapId), std::string(objectId), 0.0);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
