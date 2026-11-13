#pragma once
#include "map/rts_spawn/RTSMapCheckpoint.h"
#include "rts/RTS.h"
#include "rts/spawn/RTSSpawnCapture.h"
#include "spawn/SpawnWorld.h"
namespace eve::map_rts_spawn {
struct RTSMapCheckpoint::Impl {
    PersistentId                                     epoch;
    Revision                                         revision;
    SimulationTick                                   tick;
    spawn::SpawnCheckpoint                           core;
    rts_spawn::RTSSpawnCapture                       provenance;
    rts::RTSStateSnapshot                            roots;
    std::vector<rts::RTSFrameDamageEvent>            inputs;
    std::vector<spawn::LifecycleInput>               pending;
    std::shared_ptr<const map_spawn::MapSpawnAssets> assets;
    std::shared_ptr<const map::MapGridState>         grid;
    Impl(PersistentId identity, Revision rev, SimulationTick clock, spawn::SpawnCheckpoint coreValue,
         rts_spawn::RTSSpawnCapture sourceValue, rts::RTSStateSnapshot rootValue)
        : epoch(identity),
          revision(rev),
          tick(clock),
          core(std::move(coreValue)),
          provenance(std::move(sourceValue)),
          roots(std::move(rootValue)) {}
};
}  // namespace eve::map_rts_spawn
