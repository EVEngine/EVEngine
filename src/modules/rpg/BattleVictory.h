#pragma once

/**
 * @file BattleVictory.h
 * @brief Atomic post-battle progression and persistent-world settlement.
 */

#include "common/Result.h"

#include <string>

namespace eve::rpg {

class GameState;
class RPGActor;
class Tracker;

/** @brief Complete data needed to settle one victorious persistent encounter. */
struct BattleVictoryRequest {
    std::string mapId;
    std::string objectId;
    std::string requiredQuestId;
    double      xpAmount = 0.0;
    double      xpGrowth = 1.2;
    std::string attributeId;
    double      attributeAmount = 0.0;
    std::string notifyTopic;
    std::string notifyTarget;
    int         notifyAmount = 0;
    std::string defeatCounterId;
    int         defeatCounterAmount = 0;
    std::string levelPointAttributeId;
    int         pointsPerLevel = 0;
};

/** @brief Observable summary returned after a complete victory settlement. */
struct BattleVictoryReceipt {
    int levelsGained = 0;
    int skillsLearned = 0;
};

/** @brief Product transaction boundary for post-battle rewards and encounter lifecycle. */
class BattleVictory {
public:
    /**
     * @brief Atomically settle XP, derived class skills, attributes, quest progress, and encounter consumption.
     * @return Receipt after complete commit, or a structured failure with actor/state/tracker unchanged.
     * @remarks The required quest must be active and the encounter unconsumed. Level-up events are
     * queued only after all non-actor owners have committed, and are poll-only.
     * @ownership All participants are borrowed and remain caller-owned.
     * @thread Call on the participants' owning simulation thread; concurrent mutation is unsupported.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] static eve::Result<BattleVictoryReceipt>
    settle(RPGActor &actor, GameState &gameState, Tracker &tracker,
           const BattleVictoryRequest &request);
};

}  // namespace eve::rpg
