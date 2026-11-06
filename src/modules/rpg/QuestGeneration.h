#pragma once
#include "common/Export.h"

/**
 * @file QuestGeneration.h
 * @brief Data-driven procedural quest generation from archetypes and world entities.
 *
 * Games publish generation archetypes (templates with entity-tag filters and slot
 * substitution), then call QuestGenerator with a seeded world snapshot. The
 * generator selects entity/archetype pairs by urgency and emits ordinary
 * QuestDefinition instances that register into QuestRegistry — progress still
 * flows through Tracker::notify; the engine does not interpret kill/talk semantics.
 */

#include "common/Result.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief One objective template inside a generation archetype. */
struct EVENGINE_API_PLATFORM QuestGenerationObjectiveTemplate {
    std::string id;
    std::string topic;
    /** @brief Literal target when targetSlot is empty. */
    std::string target;
    /**
     * @brief Substitution source: empty | "entity" | "giver" | "domain" | "var:<name>".
     * @remarks When nonempty, overrides @c target after resolution.
     */
    std::string targetSlot;
    int         count = 1;
    /** @brief When nonempty, count is taken from entity.vars[countVar] (positive int). */
    std::string countVar;
    bool        optional = false;
};

/** @brief One reward template inside a generation archetype. */
struct EVENGINE_API_PLATFORM QuestGenerationRewardTemplate {
    std::string type;
    std::string id;
    /** @brief Substitution source for reward id (same slot vocabulary as objectives). */
    std::string idSlot;
    double      amount = 0.0;
    /** @brief When nonempty, amount is taken from entity.vars[amountVar] (finite > 0). */
    std::string amountVar;
};

/**
 * @brief Immutable generation archetype: how to bind one world entity into a QuestDefinition.
 */
struct EVENGINE_API_PLATFORM QuestGenerationArchetype {
    std::string id;
    double      weight = 1.0;
    /** @brief Entity must carry every listed tag. */
    std::vector<std::string> requiresEntityTags;
    std::string              startPolicy      = "manual";
    std::string              completePolicy   = "claim";
    std::string              objectivePolicy  = "all";
    double                   timeLimitSeconds = 0.0;
    std::string              refreshKey;
    /** @brief Id pattern with {archetype}/{entity}/{giver}/{domain}/{n}; empty → gen.{archetype}.{entity}.{n}. */
    std::string                                   idPattern;
    std::vector<QuestGenerationObjectiveTemplate> objectives;
    std::vector<QuestGenerationRewardTemplate>    rewards;
    std::vector<std::string>                      tags;
    /** @brief Extra strings; values may contain the same placeholders as idPattern. */
    std::unordered_map<std::string, std::string> extra;
};

/** @brief Process-local catalogue of quest-generation archetypes. */
class EVENGINE_API_PLATFORM QuestGenerationCatalogue {
public:
    /**
     * @brief Strictly validate and atomically replace every generation archetype.
     * @param json Object or non-empty array of archetype objects (`eve.rpg.quest-generation-archetypes` optional).
     * @return Committed archetype count, or a structured failure preserving the prior catalogue.
     * @thread Call on the owning content/simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] static eve::Result<int> replaceFromJsonStrict(const std::string &json);
    /** @brief Clear every archetype on the owning content thread. */
    static void clear();
    /** @brief Return the committed archetype count. */
    static int count();
    /**
     * @brief Find one archetype by id.
     * @return Borrowed nullable definition owned by the catalogue.
     * @lifetime Invalidated by clear() or a successful replacement.
     */
    static const QuestGenerationArchetype *find(const std::string &id);
    /**
     * @brief Stable ids currently registered.
     * @cost Low — copies the archetype id list; call during content setup, not per frame.
     */
    static std::vector<std::string> ids();
};

/**
 * @brief Procedural quest builder: archetypes × world entities → QuestDefinition instances.
 */
class EVENGINE_API_PLATFORM QuestGenerator {
public:
    /**
     * @brief Generate quest definitions without mutating QuestRegistry.
     * @param requestJson UTF-8 `eve.rpg.quest-generate` version 1 document.
     * @return Owning JSON `{schema,version,ids,quests}` or a structured failure.
     * @remarks Selection modes: mostUrgent | weighted | equal. maxQuests defaults to 1.
     * @cost Medium — linear in entities × matching archetypes × maxQuests; call on board refill, not per frame.
     * @thread Call on the owning simulation/content thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] static eve::Result<std::string> generateJson(const std::string &requestJson);

    /**
     * @brief Generate quests and register them into QuestRegistry.
     * @param requestJson Same schema as generateJson(); optional `replaceGenerated` (default true)
     * removes every existing definition tagged `generated` before registration.
     * @return Number of newly registered definitions, or a structured failure with no partial register
     * when validation fails before commit. On registerQuest cycle rejection mid-commit the successful
     * earlier inserts remain (same as repeated registerQuest); prefer unique idPattern.
     * @cost Medium — same as generateJson plus registry writes; call on board refill.
     * @thread Call on the owning simulation/content thread. Call Tracker::syncAuto afterwards.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] static eve::Result<int> generateAndRegister(const std::string &requestJson);
};

}  // namespace eve::rpg
