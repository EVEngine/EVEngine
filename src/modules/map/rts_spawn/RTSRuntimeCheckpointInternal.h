#pragma once
#include "combat/DamageCapture.h"
#include "definitions/DefinitionRegistryCapture.h"
#include "map/rts_spawn/RTSMapCheckpoint.h"
#include "map/rts_spawn/RTSRuntimeCheckpoint.h"
#include "rts/RTS.h"
#include "rts/RTSActionCapture.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSCrowdCapture.h"
#include "rts/RTSNavigationCapture.h"
#include "rts/RTSWeaponCapture.h"
#include "sensing/SensingCapture.h"
namespace eve::map_rts_spawn {
Result<void> validateRTSRuntimeBoundary(const RTSRuntimeCheckpoint& saved);
Result<void> rebuildRuntimeMembers(const rts::RTSStateSnapshot& roots, const rts::RTSCrowdCapture& captured,
                                   std::vector<RTSCrowdCheckpointMember>& target);

struct RTSRuntimeCheckpoint::Impl {
    mutable bool                           sealing = false;
    Status                                 context;
    combat::DamageCapture                  damage;
    definitions::DefinitionRegistryCapture definitions;
    RTSMapCheckpoint                       map;
    rts::RTSCrowdCapture                   crowdState;
    rts::RTSNavigationCapture              navigation;
    rts::RTSActionCapture                  actions;
    rts::RTSWeaponCapture                  weapons;
    rts::RTSUnitAttributeCapture           unitAttributes;
    rts::RTSFrameState                     events;
    sensing::SensingCapture                sensingState;
    rts::RTSCombatSnapshot                 combatState;
    std::vector<RTSCrowdCheckpointMember>  members;
    SimulationTick                         completed;
    std::optional<SimulationStep>          pending;
    RTSCheckpointPhase                     phase;
    Impl(Status contextValue, combat::DamageCapture damageValue,
         definitions::DefinitionRegistryCapture definitionsValue, RTSMapCheckpoint mapValue,
         rts::RTSCrowdCapture crowdValue, rts::RTSActionCapture actionsValue, rts::RTSFrameState eventsValue,
         sensing::SensingCapture sensingValue, rts::RTSCombatSnapshot combatValue, rts::RTSWeaponCapture weaponsValue,
         rts::RTSUnitAttributeCapture attributesValue, rts::RTSNavigationCapture navigationValue, SimulationTick clock,
         std::optional<SimulationStep> pendingValue, RTSCheckpointPhase phaseValue)
        : context(std::move(contextValue)),
          damage(std::move(damageValue)),
          definitions(std::move(definitionsValue)),
          map(std::move(mapValue)),
          crowdState(std::move(crowdValue)),
          navigation(std::move(navigationValue)),
          actions(std::move(actionsValue)),
          weapons(std::move(weaponsValue)),
          unitAttributes(std::move(attributesValue)),
          events(std::move(eventsValue)),
          sensingState(std::move(sensingValue)),
          combatState(std::move(combatValue)),
          completed(clock),
          pending(pendingValue),
          phase(phaseValue) {}
};
}  // namespace eve::map_rts_spawn
