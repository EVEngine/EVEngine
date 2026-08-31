#pragma once

/**
 * @file WorldInteraction.h
 * @brief Atomic settlement of persistent world loot interactions.
 */

#include "common/Result.h"

#include <string>

namespace eve::inventory {
class Bag;
}

namespace eve::rpg {

class GameState;
class Tracker;

/** @brief Complete data needed to settle one persistent loot object. */
struct WorldLootRequest {
    std::string mapId;
    std::string objectId;
    std::string requiredQuestId;
    std::string itemId;
    int         itemQuantity = 0;
    std::string attributeId;
    double      attributeAmount = 0.0;
    std::string notifyTopic;
    std::string notifyTarget;
    int         notifyAmount = 0;
};

/** @brief Product transaction boundary for persistent world-object rewards. */
class WorldInteraction {
public:
    /**
     * @brief Atomically grant loot, notify quest progress, and consume one world object.
     * @param gameState Borrowed owner of attributes and persistent world facts.
     * @param tracker Borrowed quest-progress owner.
     * @param bag Borrowed inventory owner.
     * @param request Validated interaction description; omitted effects use empty id and zero amount.
     * @return Number of committed effects, or a structured failure with every owner unchanged.
     * @remarks A required quest must be active. The object must not already be consumed.
     * Inventory hooks run only after attribute, quest, world and inventory state are final.
     * @ownership All participants remain caller-owned and must outlive this call.
     * @thread Call on the participants' owning simulation thread; concurrent mutation is unsupported.
     * @reentrancy Inventory hooks may re-enter only after the no-fail commit boundary.
     */
    [[nodiscard]] static eve::Result<int> collectLoot(GameState &gameState, Tracker &tracker,
                                                       inventory::Bag &bag,
                                                       const WorldLootRequest &request);
};

}  // namespace eve::rpg
