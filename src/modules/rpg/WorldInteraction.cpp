#include "rpg/WorldInteraction.h"

#include "inventory/InventorySystem.h"
#include "rpg/GameState.h"
#include "rpg/Tracker.h"
#include "rpg/WorldState.h"

#include <cmath>
#include <optional>

namespace eve::rpg {
namespace {

eve::Result<int> interactionFailure(eve::DiagnosticCode code, std::string message,
                                    std::string path, const WorldLootRequest &request) {
    return eve::Result<int>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path),
        {{"mapId", request.mapId}, {"objectId", request.objectId}}, "rpg.world-loot"));
}

struct TrackerRollback {
    std::unordered_map<std::string, QuestRuntime> entries;
    std::vector<std::string>                      order;
    std::vector<QuestEvent>                       pending;
    std::vector<QuestEvent>                       polled;
};

void restoreTracker(Tracker &tracker, TrackerRollback rollback) {
    tracker.entries = std::move(rollback.entries);
    tracker.order = std::move(rollback.order);
    tracker.pending = std::move(rollback.pending);
    tracker.polled = std::move(rollback.polled);
}

}  // namespace

eve::Result<int> WorldInteraction::collectLoot(GameState &gameState, Tracker &tracker,
                                               inventory::Bag &bag,
                                               const WorldLootRequest &request) {
    const bool hasItem = !request.itemId.empty() || request.itemQuantity != 0;
    const bool hasAttribute = !request.attributeId.empty() || request.attributeAmount != 0.0;
    const bool hasNotification = !request.notifyTopic.empty() || !request.notifyTarget.empty() ||
                                 request.notifyAmount != 0;
    if (!hasItem && !hasAttribute && !hasNotification)
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "world loot must contain at least one effect", "request", request);
    if (hasItem && (request.itemId.empty() || request.itemQuantity <= 0))
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "item loot requires an id and positive quantity", "item", request);
    if (hasAttribute && (request.attributeId.empty() || !std::isfinite(request.attributeAmount) ||
                         request.attributeAmount <= 0.0))
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "attribute loot requires an id and finite positive amount", "attribute", request);
    if (hasAttribute &&
        !std::isfinite(gameState.getVariable(request.attributeId) + request.attributeAmount))
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "attribute loot would produce a non-finite value", "attribute.amount", request);
    if (hasNotification && (request.notifyTopic.empty() || request.notifyTarget.empty() ||
                            request.notifyAmount <= 0))
        return interactionFailure(eve::DiagnosticCode::InvalidArgument,
                                  "quest notification requires topic, target, and positive amount",
                                  "notification", request);
    if (!request.requiredQuestId.empty() && tracker.getState(request.requiredQuestId) != "active")
        return interactionFailure(eve::DiagnosticCode::PreconditionViolation,
                                  "required quest must be active before collecting world loot",
                                  "requiredQuestId", request);

    auto gameStateBefore = gameState.snapshotJson();
    if (!gameStateBefore.ok()) return eve::Result<int>::failure(gameStateBefore.status());
    std::string gameStateSnapshot = std::move(gameStateBefore).takeValue();
    GameState candidateState;
    auto candidateRestore = candidateState.restoreSnapshotJson(gameStateSnapshot);
    if (!candidateRestore.ok()) return eve::Result<int>::failure(candidateRestore.status());
    WorldState candidateWorld(candidateState);
    if (candidateWorld.isObjectConsumed(request.mapId, request.objectId))
        return interactionFailure(eve::DiagnosticCode::Conflict,
                                  "world loot object was already consumed", "objectId", request);
    auto candidateConsume = candidateWorld.consumeObject(request.mapId, request.objectId);
    if (!candidateConsume.ok()) return eve::Result<int>::failure(candidateConsume.status());

    std::optional<inventory::PreparedInventoryAdd> preparedInventory;
    if (hasItem) {
        auto prepared = inventory::InventorySystem::prepareAddBatch(
            &bag, {{request.itemId, request.itemQuantity}});
        if (!prepared.ok()) return eve::Result<int>::failure(prepared.status());
        preparedInventory.emplace(std::move(prepared).takeValue());
    }

    TrackerRollback trackerBefore{tracker.entries, tracker.order, tracker.pending, tracker.polled};
    if (hasAttribute) gameState.addVariable(request.attributeId, request.attributeAmount);
    if (hasNotification)
        tracker.notify(request.notifyTopic, request.notifyTarget, request.notifyAmount);
    WorldState world(gameState);
    auto consumed = world.consumeObject(request.mapId, request.objectId);
    if (!consumed.ok() || consumed.code() != eve::StatusCode::Applied) {
        auto restored = gameState.restoreSnapshotJson(gameStateSnapshot);
        const bool restoredOk = restored.ok();
        restoreTracker(tracker, std::move(trackerBefore));
        if (!restoredOk)
            return interactionFailure(eve::DiagnosticCode::InvariantViolation,
                                      "world loot rollback could not restore GameState", "rollback", request);
        return interactionFailure(eve::DiagnosticCode::Conflict,
                                  "world loot object changed before commit", "objectId", request);
    }

    if (preparedInventory) {
        auto committed = inventory::InventorySystem::commitAddBatch(std::move(*preparedInventory));
        if (!committed.ok()) {
            auto restored = gameState.restoreSnapshotJson(gameStateSnapshot);
            const bool restoredOk = restored.ok();
            restoreTracker(tracker, std::move(trackerBefore));
            if (!restoredOk)
                return interactionFailure(eve::DiagnosticCode::InvariantViolation,
                                          "world loot rollback could not restore GameState", "rollback", request);
            return eve::Result<int>::failure(committed.status());
        }
    }

    return eve::Result<int>::success(static_cast<int>(hasItem) + static_cast<int>(hasAttribute) +
                                         static_cast<int>(hasNotification) + 1,
                                     eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
