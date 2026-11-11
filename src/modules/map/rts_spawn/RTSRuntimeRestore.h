#pragma once
/** @file RTSRuntimeRestore.h
 * @brief map_rts_spawn module-interface: native game checkpoint admission and isolated-owner replacement.
 * @remarks No persistent schema or external implementation registry; unsupported native contexts reject explicitly.
 */
#include <memory>
#include "common/Export.h"
#include "common/Snapshot.h"
namespace eve::map_rts_spawn {
class RTSMapRuntime;
class RTSRuntimeCheckpoint;
/** @brief Own authority-free checkpoint admission for a later synchronous isolated-world construction.
 * @ownership Pins immutable native facts and compiled core, never source/target worlds, services or hash callbacks.
 * @thread Simulation/content owner thread outside Views; methods reject token reentry.
 */
class EVENGINE_API_ORCHESTRATION PreparedRTSRuntimeRestore final {
public:
    /** @brief Discard CPU facts only; does not affect any game. */
    ~PreparedRTSRuntimeRestore();
    /** @brief Transfer CPU candidate. */
    PreparedRTSRuntimeRestore(PreparedRTSRuntimeRestore&&) noexcept;
    /** @brief Replace CPU candidate. */
    PreparedRTSRuntimeRestore& operator=(PreparedRTSRuntimeRestore&&) noexcept;
    PreparedRTSRuntimeRestore(const PreparedRTSRuntimeRestore&)            = delete;
    PreparedRTSRuntimeRestore& operator=(const PreparedRTSRuntimeRestore&) = delete;
    /** @brief Admit native context, participant identities and clocks, then compile exact frozen core content.
     * @param saved Immutable owning checkpoint, pinned before any digest callback.
     * @param hashProvider Explicit digest borrowed only during this call, never retained.
     * @return CPU token or moved/incomplete/unadmitted/mismatched native facts or digest failure.
     * @ownership Source game may already be gone. No target world, actor, service or listener is constructed.
     * @cost Whole core sealing/compilation and participant coordination per explicit restore admission.
     * @throws std::bad_alloc No game state or capabilities changed.
     */
    [[nodiscard]] static Result<PreparedRTSRuntimeRestore> prepare(std::shared_ptr<const RTSRuntimeCheckpoint> saved,
                                                                   const SnapshotHashProvider& hashProvider);
    /** @brief Materialize all native participants in one hidden world, then replace the entire game owner.
     * @param owner Exclusive game slot, possibly empty; any old owner must be idle in the same closed native context.
     * @return Applied and consume token, or failure preserving token and the complete old game/capabilities.
     * @ownership Owns the new Table/map/roots/weapons/pools/Crowd/services/session; old world retires after
     * publication.
     * @remarks No digest, live asset read, external callback, simulation step or historical-event replay.
     * Captured Ready/SpawnPending clock responsibility is restored exactly. Accepts native facts or file-parsed facts
     * after the same joint admission.
     * @thread Simulation owner, outside Views and callbacks; provider dispatch must be quiescent during replacement.
     * @cost Full native world copying/materialization and old-world teardown per explicit commit attempt.
     * @throws std::bad_alloc Hidden candidate discarded; same token may retry and old game remains available.
     */
    [[nodiscard]] Result<void> commit(std::unique_ptr<RTSMapRuntime>& owner);

private:
    struct Impl;
    explicit PreparedRTSRuntimeRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rts_spawn
