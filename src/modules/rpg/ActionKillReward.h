#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "rpg/BattleVictory.h"

#include <string_view>

namespace eve::rpg {
class GameState;
class Tracker;
struct RPGDamageTarget;
struct RPGActionDamageReceipt;

/**
 * @brief Translate a committed action death into the existing atomic victory transaction.
 * @remarks Uses single-member EncounterCatalogue definitions as reward templates.
 * Templates must satisfy BattleVictory preconditions, including an active required quest.
 * GameState owns the durable consumed fact under reserved scope rpg.action-kill.v1;
 * the target SubjectRef identifies one life, independent of execution, tick or map.
 * No additional registry, actor mirror, RNG or save format is introduced.
 */
class EVENGINE_API_PLATFORM ActionKillReward {
public:
    /**
     * @brief Grant XP, class skills, gold and quest progress once for a committed death.
     * @param recipient Borrowed value identifying the live reward actor and its stable subject.
     * @param gameState Borrowed owner of persistent reward and consumed facts.
     * @param tracker Borrowed owner of quest progress.
     * @param death Borrowed trusted local receipt emitted by RPGActionDamageSink.
     * @param encounterId Borrowed single-member reward template ID, not retained.
     * @return Applied with a victory receipt, NoOp with zero gains if already consumed,
     * or structured failure leaving reward participants and the consumed fact unchanged.
     * @ownership All participants remain caller-owned; no references survive this call.
     * @lifetime The recipient ECS generation is checked before use. The killed actor may
     * already have been destroyed. The host must never reuse a victim subject for a new life.
     * @thread Owning simulation thread only, including content registries.
     * @reentrancy No callbacks or scripts are invoked.
     * @cost Copies game/quest state through BattleVictory, much more than an identity query;
     * call only for death receipts, not every simulation tick.
     * @remarks This is not a network authorization boundary: callers must authenticate receipts
     * before admission. Keep a failed receipt for retry. Persist actor, tracker and GameState
     * together through RPGSaveSession; saving only the consumed marker can lose rewards.
     * Duplicate detection precedes template and quest checks so a successful retry remains NoOp
     * after quest completion or content removal. Loot/party reward distribution is not included.
     */
    [[nodiscard]] static Result<BattleVictoryReceipt> settle(const RPGDamageTarget& recipient, GameState& gameState,
                                                             Tracker& tracker, const RPGActionDamageReceipt& death,
                                                             std::string_view encounterId);
};
}  // namespace eve::rpg
