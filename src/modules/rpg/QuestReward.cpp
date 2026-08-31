#include "rpg/QuestReward.h"

#include "inventory/InventorySystem.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/QuestSystem.h"
#include "rpg/Tracker.h"

#include <climits>
#include <cmath>
#include <optional>

namespace eve::rpg {
namespace {

eve::Result<int> rewardFailure(eve::DiagnosticCode code, std::string message,
                               std::string path, std::string questId) {
    return eve::Result<int>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {{"questId", std::move(questId)}},
        "rpg.quest-reward"));
}

}  // namespace

eve::Result<int> QuestReward::claim(Tracker &tracker, GameState &gameState,
                                    inventory::Bag &bag, const std::string &questId) {
    const QuestDefinition *definition = QuestRegistry::find(questId);
    if (!definition)
        return rewardFailure(eve::DiagnosticCode::NotFound, "quest reward claim references an unknown quest",
                             "questId", questId);
    if (tracker.getState(questId) != "ready")
        return rewardFailure(eve::DiagnosticCode::PreconditionViolation,
                             "quest must be ready before rewards can be claimed", "quest.state", questId);

    std::vector<inventory::InventoryItemGrant> itemGrants;
    itemGrants.reserve(definition->rewards.size());
    for (std::size_t index = 0; index < definition->rewards.size(); ++index) {
        const RewardSpec &reward = definition->rewards[index];
        const std::string path = "quest.rewards[" + std::to_string(index) + "]";
        if (reward.id.empty() || !std::isfinite(reward.amount) || reward.amount <= 0.0)
            return rewardFailure(eve::DiagnosticCode::InvalidArgument,
                                 "quest reward requires an id and finite positive amount", path, questId);
        if (reward.type == "item") {
            if (reward.amount > static_cast<double>(INT_MAX) || std::floor(reward.amount) != reward.amount)
                return rewardFailure(eve::DiagnosticCode::InvalidArgument,
                                     "item reward amount must be a positive integer", path + ".amount", questId);
            itemGrants.push_back({reward.id, static_cast<int>(reward.amount)});
        } else if (reward.type == "attribute") {
            const double committed = gameState.getVariable(reward.id) + reward.amount;
            if (!std::isfinite(committed))
                return rewardFailure(eve::DiagnosticCode::InvalidArgument,
                                     "attribute reward would produce a non-finite value", path + ".amount", questId);
        } else {
            return rewardFailure(eve::DiagnosticCode::Unsupported,
                                 "quest reward type is not supported by the canonical claim transaction",
                                 path + ".type", questId);
        }
    }

    std::optional<inventory::PreparedInventoryAdd> preparedInventory;
    if (!itemGrants.empty()) {
        auto prepared = inventory::InventorySystem::prepareAddBatch(&bag, itemGrants);
        if (!prepared.ok()) return eve::Result<int>::failure(prepared.status());
        preparedInventory.emplace(std::move(prepared).takeValue());
    }

    auto gameStateBefore = gameState.snapshotJson();
    if (!gameStateBefore.ok()) return eve::Result<int>::failure(gameStateBefore.status());
    std::string gameStateSnapshot = std::move(gameStateBefore).takeValue();
    auto entriesBefore = tracker.entries;
    auto orderBefore = tracker.order;
    auto pendingBefore = tracker.pending;
    auto polledBefore = tracker.polled;

    for (const RewardSpec &reward : definition->rewards)
        if (reward.type == "attribute") gameState.addVariable(reward.id, reward.amount);

    std::string claimReason;
    if (!QuestSystem::claim(&tracker, questId, &claimReason)) {
        auto restored = gameState.restoreSnapshotJson(gameStateSnapshot);
        const bool restoredOk = restored.ok();
        tracker.entries = std::move(entriesBefore);
        tracker.order = std::move(orderBefore);
        tracker.pending = std::move(pendingBefore);
        tracker.polled = std::move(polledBefore);
        if (!restoredOk)
            return rewardFailure(eve::DiagnosticCode::InvariantViolation,
                                 "quest reward rollback could not restore GameState", "rollback", questId);
        return rewardFailure(eve::DiagnosticCode::Conflict,
                             "quest state changed before reward commit: " + claimReason,
                             "quest.state", questId);
    }

    if (preparedInventory) {
        auto committed = inventory::InventorySystem::commitAddBatch(std::move(*preparedInventory));
        if (!committed.ok()) {
            auto restored = gameState.restoreSnapshotJson(gameStateSnapshot);
            const bool restoredOk = restored.ok();
            tracker.entries = std::move(entriesBefore);
            tracker.order = std::move(orderBefore);
            tracker.pending = std::move(pendingBefore);
            tracker.polled = std::move(polledBefore);
            if (!restoredOk)
                return rewardFailure(eve::DiagnosticCode::InvariantViolation,
                                     "quest reward rollback could not restore GameState", "rollback", questId);
            return eve::Result<int>::failure(committed.status());
        }
    }

    return eve::Result<int>::success(static_cast<int>(definition->rewards.size()),
                                     eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
