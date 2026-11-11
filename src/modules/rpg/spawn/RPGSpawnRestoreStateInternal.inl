#pragma once
#include <map>
#include "rpg/ActorInitialization.h"
#include "rpg/spawn/RPGSpawnArchive.h"
#include "rpg/spawn/RPGSpawnRestoreState.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
namespace eve::rpg_spawn {
struct RPGSpawnRestoreState::Impl {
    struct Actor {
        SubjectRef                         subject;
        std::optional<ArchivedSpawnOrigin> origin;
        spawn::Point                       position;
        std::size_t                        unit = 0;
        rpg::PreparedMemberActor           state;
    };
    std::shared_ptr<const rpg::SkillConditionBindings>                    conditions;
    PersistentId                                                          epoch;
    Revision                                                              revision;
    SimulationTick                                                        tick;
    std::shared_ptr<const rpg::SkillContent>                              skills;
    std::vector<std::shared_ptr<const UnitRecipe>>                        units;
    std::vector<Actor>                                                    actors;
    std::map<std::string, std::shared_ptr<const UnitRecipe>, std::less<>> future;
};
}  // namespace eve::rpg_spawn
