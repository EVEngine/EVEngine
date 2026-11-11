#pragma once
/** @file RTSMapRuntime.h
 * @brief map_rts_spawn module-interface: owned real RTS/map game composition and explicit simulation frames.
 * @remarks No new ECS model, provider registry, save schema or tool family. Uses canonical domain services.
 */
#include "spawn/SpawnWorld.h"
namespace eve::service {
class IFileSystem;
}
namespace eve::rts {
struct CommandSpec;
}
namespace eve::map_rts_spawn {
class RTSRuntimeCheckpoint;
class PreparedRTSRuntimeRestore;
/** @brief Own one private Map, RTS Table, Crowd, action executor, content and spawn session.
 * @ownership No caller Map/domain pointer is retained. Destroying this owner releases its entire private world;
 * explicit close performs observable source cleanup. No external actor handles are exported.
 * @thread Simulation owner thread, outside Views. Calls are non-reentrant, no locks around callbacks.
 * @remarks Positive frame deltas and contiguous ticks start at one. Domain step failure may be partial and faults
 * this runtime: only inspect/close remain available. Spawn failure before core tick consumption retains the
 * completed domain frame; retry the exact same step without stepping RTS again. Exceptions after core consumption
 * likewise fault the runtime: per-batch publication cannot be replayed or described as full-frame rollback.
 * Whole persistence uses RTSRuntimeCheckpoint/PreparedRTSRuntimeRestore in the closed native profile.
 * Live content/map hot-reload remains unsupported; reopen starts a new game.
 */
class EVENGINE_API_ORCHESTRATION RTSMapRuntime final {
public:
    /** @brief Release the complete private world; normal gameplay unload should explicitly close first. */
    ~RTSMapRuntime();
    RTSMapRuntime(const RTSMapRuntime&)            = delete;
    RTSMapRuntime& operator=(const RTSMapRuntime&) = delete;
    /** @brief Prepare a fully isolated game world using canonical content/template/map admission.
     * @return Owning ready runtime or failure without publishing any caller-visible map, unit or agent.
     * @ownership Arguments are owned copies before filesystem callbacks; files is borrowed only during preparation.
     * @remarks World listener families publish together after successful admission; filesystem callbacks run
     * outside candidate listener capture, preserving their external registrations. Ordinary shared services
     * retain their own lifetime protocol. Requires square orthogonal cells representable by RTS float navigation.
     * Initial players are known empty. Template revisions and initial command stay fixed for the session.
     * @cost Whole content/map/template admission and navigation setup per explicit new game, amortized over frames.
     */
    [[nodiscard]] static Result<std::unique_ptr<RTSMapRuntime>> prepare(
        std::string content, std::vector<Value> templates, Value binding, service::IFileSystem& files,
        SubjectRef faction, PersistentId owner, std::uint64_t seed, std::shared_ptr<const rts::CommandSpec> command);
    /** @brief Step the actual domain once, then hand its same owning damage facts to map spawning.
     * @return Spawn report or error; a pre-consumption spawn failure is retried with exactly the same step.
     * @remarks Domain failure cannot be rolled back. Initial input validation rejects before any time advancement.
     * @cost Actual RTS simulation plus bounded spawn work per injected frame; no asset or grid loading.
     */
    [[nodiscard]] Result<spawn::AdvanceReport> advance(const SimulationStep& step);
    /** @brief Copy actual RTS presentation state, spawn observations and ready/spawn_pending/faulted/closed phase.
     * @return Owning transient value independent of this world, or reentry error. Not a persistence artifact.
     * @cost Linear actual domain/source/member projection per explicit observation, not per movement operation.
     */
    [[nodiscard]] Result<Value> captureState() const;
    /** @brief Freeze actual owner clocks/phase, map/core/roots, Crowd, actions and retained event queues together.
     * @return Independent in-memory facts or failure without consuming events, advancing time or changing phase.
     * @ownership Returned value owns facts, never this world or its services; no service callbacks run.
     * @thread Owner thread, outside Views and runtime calls. Ready and SpawnPending are supported.
     * @remarks Not a complete game-save or restore token; unsupported remaining participants cannot be inferred.
     * @cost Copies all joined native participants once per explicit checkpoint attempt.
     */
    [[nodiscard]] Result<RTSRuntimeCheckpoint> captureCheckpoint(PersistentId saveEpoch, Revision revision) const;
    /** @brief Stop new frames and remove all managed units; failure preserves explicit cleanup responsibility.
     * @cost Linear source/unit cleanup per explicit close attempt; never advances time.
     */
    [[nodiscard]] Result<void> close();

private:
    friend class PreparedRTSRuntimeRestore;
    struct Impl;
    explicit RTSMapRuntime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rts_spawn
