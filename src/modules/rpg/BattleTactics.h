#pragma once

/** @file BattleTactics.h @brief Strict ordered battle-action policies for companions and AI. */

#include "common/Result.h"
#include "rpg/Battle.h"

#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** @brief One ordered tactic rule; an empty condition resource is an unconditional default. */
struct BattleTacticRule {
    std::string skillId;
    BattleTargetPolicy targetPolicy = BattleTargetPolicy::Auto;
    std::string conditionResource;
    double belowRatio = 1.0;
};

/** @brief One immutable ordered tactic definition. */
struct BattleTacticsDefinition {
    std::string id;
    std::vector<BattleTacticRule> rules;
};

/** @brief Process-local strict catalogue that selects and queues companion/AI actions. */
class BattleTacticsCatalogue {
public:
    /**
     * @brief Validate and atomically replace all tactic definitions.
     * @return Committed definition count, or a structured failure preserving the previous catalogue.
     * @remarks Skill references and target policies are validated. Each definition must end in an
     * unconditional default so queueAction cannot silently omit a living actor's turn.
     */
    [[nodiscard]] static eve::Result<int> replaceFromJsonStrict(const std::string &json);
    static void clear();
    static int count();
    /**
     * @brief Select the first matching rule and queue it through Battle's canonical checked API.
     * @return Selected skill id (empty means basic attack), or a structured failure without queue mutation.
     */
    [[nodiscard]] static eve::Result<std::string> queueAction(Battle *battle, RPGActor *actor,
                                                              const std::string &tacticsId);
};

}  // namespace eve::rpg
