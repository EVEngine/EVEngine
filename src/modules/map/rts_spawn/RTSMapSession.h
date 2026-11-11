#pragma once
/** @file RTSMapSession.h
 * @brief map_rts_spawn module-interface: canonical map admission and retained RTS lifecycle handoff.
 * @remarks Injects actual RTS/content/map services; introduces no ECS model, script/tool protocol or save schema.
 */
#include "common/Export.h"
#include "common/Snapshot.h"
#include "spawn/SpawnWorld.h"
namespace eve::map {
class Map;
}
namespace eve::service {
class IFileSystem;
}
namespace eve::definitions {
class DefinitionRegistry;
}
namespace eve::rts {
class RTS;
struct RTSFrameDamageEvent;
struct CommandSpec;
}  // namespace eve::rts
namespace eve::map_spawn {
class MapSpawnSession;
class PreparedMapNavigation;
}  // namespace eve::map_spawn
namespace eve::map_rts_spawn {
class PreparedRTSMapSession;
class RTSMapCheckpoint;
class RTSMapRestoreState;
/** @brief Own map spawning and its actual RTS factory/occupancy, without owning or stepping the RTS facade.
 * @ownership Weak-observes actual RTS/Table lifetimes through canonical providers. Close before normal unload.
 * Map is borrowed per call. No temporary RTS/Map/unit pointer is retained; content registry is shared.
 * @thread Simulation owner thread outside Views; mutations reject reentry. No locks across callbacks.
 * @remarks Game owns RTS, Crowd, navigation attachment and domain step. Step RTS once, dispatch its same damage
 * batch to other consumers and this session. RTS step may already have cleaned casualties: owning damage facts
 * preserve death classification after entity removal. Retry this session with empty events after
 * rejected map frames; never repeat the domain step to retry spawn. If input admission is uncertain after an
 * exception, resubmit the original batch: canonical identity retirement makes repeated death facts harmless.
 * Keep runtime content stable during a session.
 * Fresh preparation starts a new scheduler. Saved membership restore and hot-reload rebinding require separate
 * coordination; reopening configuration must not be used as a substitute for restoring source relationships.
 */
class EVENGINE_API_ORCHESTRATION RTSMapSession final {
public:
    /** @brief Release local owners only; explicit close reports business cleanup failures. */
    ~RTSMapSession();
    RTSMapSession(const RTSMapSession&)            = delete;
    RTSMapSession& operator=(const RTSMapSession&) = delete;
    /** @brief Own the input batch before translation, retain lifecycle facts and advance canonical map spawning.
     * @return Actual report or rejection; accepted earlier batches are not rolled back by later failures.
     * @ownership Input spans are synchronously copied. After admission, translation retires indexes once and
     * processing failures retain original or translated facts for retry. Busy/closing rejection admits no input.
     * @throws std::bad_alloc Initial input copying may fail before admission; keep and resubmit the caller batch
     * after an exception. Already translated facts remain retained; repeated facts cannot double-retire a member.
     * @remarks Does not poll or step RTS/Crowd. Caller retains its batch until successful handoff is observed.
     * @cost Linear input/tracked-unit/pending capture plus bounded canonical spawn work per supplied frame.
     */
    [[nodiscard]] Result<spawn::AdvanceReport> advance(map::Map& target, const SimulationStep& step,
                                                       std::span<const rts::RTSFrameDamageEvent> events,
                                                       spawn::ActivationFrame                    activation);
    /** @brief Freeze core, exact provenance/live weapons, native roots, map and both retained event stages.
     * @return Detached joined facts or inconsistent membership/owner/map failure, leaving all live state unchanged.
     * @ownership No source owner, callback or provider lease survives in the result.
     * @thread Domain/map owner thread outside Views and concurrent changes; rejects session reentry/closing.
     * @remarks Does not step RTS/Crowd, translate lifecycle, poll/clear events or issue restore authorization.
     * Caller coordinates domain time. External Crowd/action and non-generated weapon/content closure remain
     * separate participants; this memory freeze is not a complete persistent game save.
     * @cost Copies native roots, map grid, core, recipes, weapons and pending events per explicit save boundary.
     * @throws std::bad_alloc Candidate allocation failed without changing session/domain/core state.
     */
    [[nodiscard]] Result<RTSMapCheckpoint> capture(map::Map& target, PersistentId saveEpoch, Revision revision);
    /** @brief Borrow read-only spawning observations until close/destruction; no mutable core bypass. */
    [[nodiscard]] const map_spawn::MapSpawnSession& spawning() const noexcept;
    /** @brief Query current map/navigation metadata, without capturing a grid or advancing.
     * @return True if explicit refresh is needed, or stale/closing/reentry error.
     */
    [[nodiscard]] Result<bool> navigationNeedsRefresh(map::Map& target) const;
    /** @brief Prepare canonical navigation refresh, leaving prior capture in effect until token commit.
     * @cost Grid-sized immutable capture per changed map revision, not per simulation frame.
     */
    [[nodiscard]] Result<std::unique_ptr<map_spawn::PreparedMapNavigation>> prepareNavigationRefresh(map::Map& target);
    /** @brief Stop new frames and explicitly detach managed units; failure retains retryable cleanup responsibility.
     * @return Success or actual domain cleanup failure, including after RTS/Map destruction when roots are gone.
     * @cost Linear source/unit cleanup per explicit unload attempt. No domain simulation step is performed.
     */
    [[nodiscard]] Result<void> close(spawn::DetachPolicy policy);

private:
    friend class PreparedRTSMapSession;
    struct Impl;
    explicit RTSMapSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Own a hidden canonical map candidate and one unpublished RTS/map session.
 * @ownership Retains actual registry and provider owners, weakly observes RTS. File/template inputs are not retained.
 * @thread Map/domain owner thread outside Views. Input callbacks must not destroy borrowed arguments or reenter.
 */
class EVENGINE_API_ORCHESTRATION PreparedRTSMapSession final {
public:
    /** @brief Cancel unpublished resources without gameplay cleanup. */
    ~PreparedRTSMapSession();
    /** @brief Transfer hidden candidate. */
    PreparedRTSMapSession(PreparedRTSMapSession&&) noexcept;
    /** @brief Cancel current local candidate and transfer another. */
    PreparedRTSMapSession& operator=(PreparedRTSMapSession&&) noexcept;
    PreparedRTSMapSession(const PreparedRTSMapSession&)            = delete;
    PreparedRTSMapSession& operator=(const PreparedRTSMapSession&) = delete;
    /** @brief Compose actual RTS factory/footprints and prepare the canonical isolated map transaction.
     * @param domain Actual game-owned RTS facade whose Table/faction/Crowd remain alive for active use.
     * @param registry Actual canonical RTS definitions, shared for subsequent batches; keep content stable.
     * @param players Owning initial World2D capability; empty means known no players.
     * @param initialCommand Owning optional canonical command copied by the factory, admitted with each batch.
     * @return Owning candidate or failure without publishing map layers, units or agents.
     * @ownership Borrows target/binding/files/templates/objectContract only during this synchronous preparation.
     * @cost Full asset/definition/navigation admission per explicit load, amortized across runtime frames.
     */
    [[nodiscard]] static Result<PreparedRTSMapSession> prepare(
        map::Map& target, const Value& binding, service::IFileSystem& files, const spawn::ITemplateResolver& templates,
        rts::RTS& domain, SubjectRef faction, std::shared_ptr<definitions::DefinitionRegistry> registry,
        PersistentId owner, std::uint64_t seed, std::string_view objectContract, std::vector<spawn::Point> players,
        std::shared_ptr<const rts::CommandSpec> initialCommand);
    /** @brief Prepare map/core/source restoration from the exact native checkpoint, retaining both event stages.
     * @param target Actual target Map, weakly observed without publishing during preparation.
     * @param admitted Authority-free CPU admission, independently reusable for isolated attempts.
     * @param domain Caller-owned isolated RTS with saved roots and live Crowd already staged; keep stable until commit.
     * @param players Explicit initial spatial capability; absence differs from known empty and is not inferred.
     * @return Isolated map/session candidate; no domain roots are created or simulation time advanced.
     * @ownership Shares detached checkpoint facts and owns its new factory/core; weakly observes target and domain.
     * @thread Map/domain owner thread outside Views; no digest, filesystem or other external callbacks run.
     * @cost Full map/grid replay and source/index rebuilding per materialization attempt; no digest work.
     * @throws std::bad_alloc No target map/domain facts published; same checkpoint remains retryable.
     */
    [[nodiscard]] static Result<PreparedRTSMapSession> prepareRestored(
        map::Map& target, const RTSMapRestoreState& admitted, rts::RTS& domain,
        std::optional<std::vector<spawn::Point>> players);
    /** @brief Publish the canonical map candidate and transfer its session; expired RTS prevents publication.
     * @return Owning session or map/service/owner error; failed publication leaves this candidate unconsumed.
     * @remarks Rebind a missing provider before retry; a retired RTS owner requires a newly prepared candidate.
     * @cost Candidate-sized map publication per commit attempt; restored sessions also preflight native roots/source
     * facts. No asset reads or unit creation.
     */
    [[nodiscard]] Result<std::unique_ptr<RTSMapSession>> commit(map::Map& target);

private:
    struct Impl;
    explicit PreparedRTSMapSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rts_spawn
