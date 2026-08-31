#include "rpg/BattleVictory.h"

#include "rpg/ClassSystem.h"
#include "rpg/GameState.h"
#include "rpg/LevelSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/Tracker.h"
#include "rpg/WorldState.h"

#include <cmath>

namespace eve::rpg {
namespace {

eve::Result<BattleVictoryReceipt> victoryFailure(eve::DiagnosticCode code, std::string message,
                                                  std::string path,
                                                  const BattleVictoryRequest &request) {
    return eve::Result<BattleVictoryReceipt>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path),
        {{"mapId", request.mapId}, {"objectId", request.objectId}}, "rpg.battle-victory"));
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

bool finiteAddition(const GameState &state, const std::string &id, double amount) {
    return id.empty() ? amount == 0.0 : std::isfinite(amount) &&
                                             std::isfinite(state.getVariable(id) + amount);
}

}  // namespace

eve::Result<BattleVictoryReceipt>
BattleVictory::settle(RPGActor &actor, GameState &gameState, Tracker &tracker,
                      const BattleVictoryRequest &request) {
    if (request.requiredQuestId.empty() || tracker.getState(request.requiredQuestId) != "active")
        return victoryFailure(eve::DiagnosticCode::PreconditionViolation,
                              "required quest must be active before victory settlement",
                              "requiredQuestId", request);
    if (request.notifyTopic.empty() || request.notifyTarget.empty() || request.notifyAmount <= 0)
        return victoryFailure(eve::DiagnosticCode::InvalidArgument,
                              "victory quest notification must be complete and positive",
                              "notification", request);
    if (request.attributeId.empty() || !std::isfinite(request.attributeAmount) ||
        request.attributeAmount < 0.0 ||
        !finiteAddition(gameState, request.attributeId, request.attributeAmount))
        return victoryFailure(eve::DiagnosticCode::InvalidArgument,
                              "victory attribute reward is invalid or non-finite", "attribute", request);
    if (request.defeatCounterId.empty() || request.defeatCounterAmount <= 0 ||
        !finiteAddition(gameState, request.defeatCounterId,
                        static_cast<double>(request.defeatCounterAmount)))
        return victoryFailure(eve::DiagnosticCode::InvalidArgument,
                              "victory counter increment is invalid", "defeatCounter", request);
    if (request.levelPointAttributeId.empty() || request.pointsPerLevel < 0)
        return victoryFailure(eve::DiagnosticCode::InvalidArgument,
                              "level-point reward configuration is invalid", "levelPoints", request);

    auto preparedXp = LevelSystem::prepareGainXp(&actor, request.xpAmount, request.xpGrowth);
    if (!preparedXp.ok())
        return eve::Result<BattleVictoryReceipt>::failure(preparedXp.status());
    PreparedProgressionGain xp = std::move(preparedXp).takeValue();
    const int levelsGained = xp.levelsGained();
    const double levelPoints = static_cast<double>(levelsGained) * request.pointsPerLevel;
    if (!finiteAddition(gameState, request.levelPointAttributeId, levelPoints))
        return victoryFailure(eve::DiagnosticCode::InvalidArgument,
                              "level-point reward would produce a non-finite value", "levelPoints", request);

    auto gameStateBefore = gameState.snapshotJson();
    if (!gameStateBefore.ok())
        return eve::Result<BattleVictoryReceipt>::failure(gameStateBefore.status());
    std::string gameStateSnapshot = std::move(gameStateBefore).takeValue();
    GameState candidateState;
    auto candidateRestore = candidateState.restoreSnapshotJson(gameStateSnapshot);
    if (!candidateRestore.ok())
        return eve::Result<BattleVictoryReceipt>::failure(candidateRestore.status());
    WorldState candidateWorld(candidateState);
    if (candidateWorld.isObjectConsumed(request.mapId, request.objectId))
        return victoryFailure(eve::DiagnosticCode::Conflict,
                              "encounter was already consumed", "objectId", request);
    auto candidateConsume = candidateWorld.consumeObject(request.mapId, request.objectId);
    if (!candidateConsume.ok())
        return eve::Result<BattleVictoryReceipt>::failure(candidateConsume.status());

    TrackerRollback trackerBefore{tracker.entries, tracker.order, tracker.pending, tracker.polled};
    gameState.addVariable(request.attributeId, request.attributeAmount);
    gameState.addVariable(request.defeatCounterId, request.defeatCounterAmount);
    if (levelPoints > 0.0) gameState.addVariable(request.levelPointAttributeId, levelPoints);
    tracker.notify(request.notifyTopic, request.notifyTarget, request.notifyAmount);
    WorldState world(gameState);
    auto consumed = world.consumeObject(request.mapId, request.objectId);
    if (!consumed.ok() || consumed.code() != eve::StatusCode::Applied) {
        auto restored = gameState.restoreSnapshotJson(gameStateSnapshot);
        const bool restoredOk = restored.ok();
        restoreTracker(tracker, std::move(trackerBefore));
        if (!restoredOk)
            return victoryFailure(eve::DiagnosticCode::InvariantViolation,
                                  "victory rollback could not restore GameState", "rollback", request);
        return victoryFailure(eve::DiagnosticCode::Conflict,
                              "encounter changed before victory commit", "objectId", request);
    }

    auto committedXp = LevelSystem::commitGainXp(std::move(xp));
    if (!committedXp.ok()) {
        auto restored = gameState.restoreSnapshotJson(gameStateSnapshot);
        const bool restoredOk = restored.ok();
        restoreTracker(tracker, std::move(trackerBefore));
        if (!restoredOk)
            return victoryFailure(eve::DiagnosticCode::InvariantViolation,
                                  "victory rollback could not restore GameState", "rollback", request);
        return eve::Result<BattleVictoryReceipt>::failure(committedXp.status());
    }

    BattleVictoryReceipt receipt;
    receipt.levelsGained = std::move(committedXp).takeValue();
    receipt.skillsLearned = ClassSystem::checkLevelSkills(&actor);
    return eve::Result<BattleVictoryReceipt>::success(
        std::move(receipt), eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
