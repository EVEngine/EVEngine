#pragma once
// Private composition layout shared only by runtime admission and native restoration in this module.
#include <optional>
#include "action/Action.h"
#include "combat/Damage.h"
#include "crowd/Crowd.h"
#include "definitions/Definitions.h"
#include "map/Map.h"
#include "map/Pathfinder.h"
#include "map/rts_spawn/RTSMapRuntime.h"
#include "map/rts_spawn/RTSMapSession.h"
#include "rts/RTS.h"
#include "rts/RTSWeaponRestore.h"
#include "sensing/Sensing.h"
namespace eve::map_rts_spawn {
struct RTSMapRuntime::Impl {
    // Reverse destruction keeps navigation/Crowd/content alive until the RTS facade is gone.
    std::shared_ptr<ecs::Table>                      table    = std::make_shared<ecs::Table>();
    std::shared_ptr<definitions::DefinitionRegistry> registry = std::make_shared<definitions::DefinitionRegistry>();
    map::Map                                         map;
    map::Pathfinder                                  navigation;
    crowd::Crowd                                     crowd;
    action::ActionRuntime                            actions;
    sensing::SensingWorld                            sensing;
    combat::DamageRuntime                            damage;
    std::optional<rts::RestoredRTSWeaponPools>       restoredPools;
    rts::ActionAdapter                               executor{actions};
    rts::RTS                                         domain{table};
    std::unique_ptr<RTSMapSession>                   session;
    enum class Phase { Ready, DomainExecuting, SpawnPending, Faulted, Closing, Closed };
    Phase                         phase         = Phase::Ready;
    std::uint64_t                 completedTick = 0;
    std::optional<SimulationStep> pending;
    bool                          busy = false;
};
}  // namespace eve::map_rts_spawn
