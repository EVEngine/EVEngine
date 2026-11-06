#pragma once

// Private projection shared by encounter and action-kill adapters. The catalogue
// owns reward definitions; BattleVictory owns validation and state publication.
#include "rpg/BattleVictory.h"
#include "rpg/EncounterCatalogue.h"

#include <string_view>

namespace eve::rpg::detail {
/** @brief Project catalogue reward fields into the canonical victory transaction. */
[[nodiscard]] inline BattleVictoryRequest encounterVictoryRequest(const EncounterDefinition& definition,
                                                                  std::string_view mapId, std::string_view objectId) {
    BattleVictoryRequest request;
    request.mapId                 = mapId;
    request.objectId              = objectId;
    request.requiredQuestId       = definition.requiredQuestId;
    request.xpAmount              = definition.xpReward;
    request.xpGrowth              = definition.xpGrowth;
    request.attributeId           = "gold";
    request.attributeAmount       = definition.goldReward;
    request.notifyTopic           = definition.notifyTopic;
    request.notifyTarget          = definition.notifyTarget;
    request.notifyAmount          = definition.notifyAmount;
    request.defeatCounterId       = definition.defeatCounterId;
    request.defeatCounterAmount   = definition.defeatCounterAmount;
    request.levelPointAttributeId = definition.levelPointAttributeId;
    request.pointsPerLevel        = definition.pointsPerLevel;
    return request;
}
}  // namespace eve::rpg::detail
