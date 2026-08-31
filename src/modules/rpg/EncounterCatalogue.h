#pragma once

/** @file EncounterCatalogue.h @brief Strict RPG encounter definitions and settlement projection. */

#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** @brief One immutable enemy member inside an encounter composition. */
struct EncounterMemberDefinition {
    std::string id;
    std::string displayName;
    std::string skillId;
    double      attack = 0.0;
    double      defense = 0.0;
    double      maxHp = 1.0;
    double      speed = 0.0;
};

/** @brief One immutable, fully specified encounter composition and settlement definition. */
struct EncounterDefinition {
    std::string id;
    std::string displayName;
    std::vector<EncounterMemberDefinition> members;
    double      xpReward = 0.0;
    double      xpGrowth = 1.2;
    double      goldReward = 0.0;
    std::string requiredQuestId;
    std::string notifyTopic;
    std::string notifyTarget;
    int         notifyAmount = 0;
    std::string defeatCounterId;
    int         defeatCounterAmount = 0;
    std::string levelPointAttributeId;
    int         pointsPerLevel = 0;
};

/** @brief Process-local strict encounter content catalogue. */
class EncounterCatalogue {
public:
    /**
     * @brief Validate and atomically replace every encounter definition.
     * @return Committed definition count, or a structured failure preserving the prior catalogue.
     * @remarks Referenced skills and quests must already exist. Unknown fields are rejected.
     * @thread Call on the owning content/simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] static eve::Result<int> replaceFromJsonStrict(const std::string &json);
    static void clear();
    static int count();
    static const EncounterDefinition *find(const std::string &id);
    /** @brief Return the number of ordered enemy members in an encounter, or zero when unknown. */
    static int memberCount(const std::string &id);
    /**
     * @brief Create and initialize an ECS-owned enemy actor from one definition.
     * @return Borrowed actor owned by the RPG ECS world, or null for an unknown id/allocation failure.
     */
    static RPGActor *createActor(const std::string &id);
    /**
     * @brief Create one indexed member of an encounter composition.
     * @return Borrowed actor owned by the RPG ECS world, or null for an invalid id/index/allocation failure.
     */
    static RPGActor *createMemberActor(const std::string &id, int memberIndex);
};

}  // namespace eve::rpg
