#pragma once
/** @file MapRTSSpawn.h
 * @brief map_rts_spawn script face: owned canonical RTS map runtime, prepared before explicit publication.
 * @remarks Requires project filesystem only during prepare; no capability output, ECS export, MCP or save schema.
 */
#include "common/Module.h"
namespace eve::map_rts_spawn {
/** @brief Script composition root owning ready/live RTSMapRuntime instances.
 * @ownership Owns its complete private map/domain world. prepare does not replace a running world; close first.
 * @thread Script/simulation owner thread; rejects reentry, no callbacks under locks.
 * @remarks advance takes explicit contiguous tick/nanoseconds; captureState owns actual domain observations.
 * prepare consumes canonical RTS content JSON, an array of spawn-template JSON objects and map binding JSON.
 * Initial command is canonical AttackMove in world coordinates, faction/owner are nonnil persistent identities.
 * freezeRuntime(epoch, revision)/sealRuntime() retain detached closed-native facts and serialize the existing
 * whole-runtime file. snapshotHashAlgorithm reports the common bridge's active provider; file I/O stays with fs.
 * prepareRuntimeRestore(file, epoch)/commitRuntimeRestore() admit CPU facts then publish one complete owner.
 * Failures preserve the current game and retry token; close cancels pending restoration but retains detached saves.
 * Module factory construction retains its existing singleton semantics. No script callbacks run during publication.
 * @cost Prepare/restore/freeze/seal are whole-world operations separated into explicit preparation/publication;
 * per-frame advance and transient observations retain their native bounded/linear costs.
 * No mutable Map/domain handles, event polling, implicit clocks or hot-reload replacement are exposed.
 */
class EVENGINE_API_ORCHESTRATION MapRTSSpawn final : public Module {
public:
    Module_REG(MapRTSSpawn);
    /** @brief Allocate local control state only; no map, unit or agent publication. */
    MapRTSSpawn();
    /** @brief Release the complete private world; explicit close is the normal gameplay unload boundary. */
    ~MapRTSSpawn() override;
    MapRTSSpawn(const MapRTSSpawn&)            = delete;
    MapRTSSpawn& operator=(const MapRTSSpawn&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rts_spawn
