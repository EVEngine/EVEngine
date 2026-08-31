#pragma once

/**
 * @file QuestReward.h
 * @brief Atomic orchestration for claiming data-driven quest rewards.
 */

#include "common/Result.h"

#include <string>

namespace eve::inventory {
class Bag;
}

namespace eve::rpg {

class GameState;
class Tracker;

/** @brief Product-level transaction boundary for quest completion and rewards. */
class QuestReward {
public:
    /**
     * @brief Validate and atomically claim every reward declared by one ready quest.
     * @param tracker Borrowed authoritative quest tracker.
     * @param gameState Borrowed authoritative numeric attribute owner.
     * @param bag Borrowed authoritative inventory owner.
     * @param questId Exact registered quest id whose state must be `ready`.
     * @return Number of committed reward entries, or a structured failure with no observable mutation.
     * @remarks Reward type `item` adds an integral quantity to Bag; `attribute`
     * adds a finite amount to the same-named GameState variable. Unsupported or
     * malformed rewards are rejected before mutation. Inventory hooks run only
     * after quest, attributes, and inventory have all reached their final state.
     * @ownership Participants remain caller-owned and must outlive this call.
     * @thread Call on the participants' owning simulation thread; concurrent mutation is unsupported.
     * @reentrancy Inventory hooks may re-enter after the no-fail commit boundary only.
     */
    [[nodiscard]] static eve::Result<int> claim(Tracker &tracker, GameState &gameState,
                                                 inventory::Bag &bag, const std::string &questId);
};

}  // namespace eve::rpg
