#pragma once
/** @file RPGSpawnPreview.h
 * @brief map_rpg_spawn_editor composition face: isolated canonical RPG/map simulation for authoring.
 */
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "common/Time.h"
namespace eve {
class Value;
class SubjectRef;
}  // namespace eve
namespace eve::service {
class IFileSystem;
}
namespace eve::graphics {
class ISolidRectRenderer;
}
namespace eve::rpg_spawn {
class RPGUnitCatalogue;
}
namespace eve::map_rpg_spawn {
struct ActorStyle;
}
namespace eve::map_spawn {
struct PlacementTrace;
}
namespace eve::spawn_editor {
struct FormSourceSnapshot;
}
namespace eve::spawn {
class ITemplateResolver;
struct ActivationFrame;
struct SpawnerHandle;
struct AdvanceReport;
struct WorldDiagnostics;
enum class Change;
}  // namespace eve::spawn
namespace eve::map_rpg_spawn_editor {
/** @brief Own an independent canonical map/session and actual private RPG Table for configuration preview.
 * @ownership No gameplay/editor Table, actor, map or RNG is shared. Shares actual content/catalogue owners; the caller
 * keeps definitions and skill content stable while preview runs. File/template services are borrowed only during
 * create. No provider or temporary pointer is retained.
 * @thread Authoring/simulation owner thread outside Views; mutating/reentrant observation calls reject reentry.
 * @remarks Only spawn scheduling/placement is simulated; combat and AI are not implicitly advanced. Preview actors
 * exist solely in this private Table, never in a formal gameplay world. Captures/draws describe committed preview
 * batches, not predictions of successful creation in a different live world. No persistent format is introduced.
 * The same source/content, seed and steps yield the canonical scheduler's deterministic results; owner incarnations
 * and ephemeral handles intentionally differ. Source edits require explicit reconstruction, never implicit hot reload.
 */
class EVENGINE_API_EDITORS RPGSpawnPreview final {
public:
    /** @brief Release the wholly private simulation; explicit stop observes domain-cleanup failures before teardown. */
    ~RPGSpawnPreview();
    RPGSpawnPreview(const RPGSpawnPreview&)            = delete;
    RPGSpawnPreview& operator=(const RPGSpawnPreview&) = delete;
    /** @brief Freeze committed source and assemble a separate map through canonical RPG/map prepare and commit.
     * @return Owning preview or canonical source/map/content/provider error, without changing caller worlds or files.
     * @ownership Source is consumed; binding/contract are copied before file callbacks. Actual units owner is retained.
     * @param owner Explicit non-nil preview scheduler identity, independent of any formal game session.
     * @param seed Independent injected preview RNG seed; no gameplay RNG is read or consumed.
     * @param bodyRadius Explicit World2D occupancy footprint, independent of display markers.
     * @remarks Initial player capability is known-empty; supply players/events in advance to exercise activation.
     * @cost Complete map/dependency loading, source compilation and private world admission per explicit start/reset.
     * @throws std::bad_alloc Preparation failed before exposing the candidate.
     */
    [[nodiscard]] static Result<std::unique_ptr<RPGSpawnPreview>> create(
        const Value& binding, std::string objectContract, service::IFileSystem& files,
        spawn_editor::FormSourceSnapshot source, const spawn::ITemplateResolver& templates,
        std::shared_ptr<rpg_spawn::RPGUnitCatalogue> units, PersistentId owner, std::uint64_t seed, double bodyRadius);
    /** @brief Advance the private canonical session with explicit time and activation; no gameplay events are polled.
     * @return Actual report or failure; accepted prior batches are not rolled back on later diagnostic failure.
     * @cost Bounded canonical scheduling and actual RPG materialization per supplied step.
     */
    [[nodiscard]] Result<spawn::AdvanceReport> advance(const SimulationStep& step, spawn::ActivationFrame activation);
    /** @brief Defeat one currently source-owned private actor through canonical HP damage.
     * @param subject Persistent identity from this preview's current diagnostic membership.
     * @return Changed/Unchanged for a live/already defeated member, or missing/stale membership error.
     * @ownership Borrows identity synchronously; retains real private events, never caller pointers.
     * @thread Preview owner thread outside Views; rejects reentry and holds no locks.
     * @reentrancy Canonical attribute calculators follow the domain contract; no owner/actor lifecycle mutation.
     * @remarks Does not advance time. The next advance hands private death events to the canonical lifecycle bridge;
     * failed advancement retains events for retry. Does not poll or modify the formal gameplay event queues.
     * Canonical damage prepares event storage before HP mutation; allocation failure leaves HP/events unchanged.
     * @cost Linear diagnostic/member and private actor lookup per explicit authoring action.
     */
    [[nodiscard]] Result<spawn::Change> defeat(SubjectRef subject);
    /** @brief Resolve one external event name against actual frozen instance predicates without advancing time.
     * @return Owning qualified handles for all matches, or InvalidArgument/NotFound without simulation mutation.
     * @ownership Borrows event during this call; output handles observe this world and are not source leases.
     * @remarks Feed the returned handles into advance's ActivationFrame. Stopped/foreign incarnations reject at the
     * canonical advance boundary; other activation kinds are not manually overridden. No callbacks or publication.
     * @cost Linear owning source/member capture and event-name matching per explicit input preparation.
     */
    [[nodiscard]] Result<std::vector<spawn::SpawnerHandle>> resolveEvent(std::string_view event) const;
    /** @brief Capture owning read-only facts from the private canonical scheduler; never advances it.
     * @return Detached capture or stopped/reentry error; no runtime pointer escapes.
     * @cost Linear source/member diagnostic capture per explicit refresh.
     */
    [[nodiscard]] Result<spawn::WorldDiagnostics> captureDiagnostics() const;
    /** @brief Capture the owning bounded trace enabled for this private preview, without sampling or advancement.
     * @return Last 256 actual spatial decisions, or stopped/reentry failure; independent of preview lifetime.
     * @cost Bounded allocation and copy per explicit diagnostic refresh; no provider callbacks.
     */
    [[nodiscard]] Result<map_spawn::PlacementTrace> capturePlacementTrace() const;
    /** @brief Draw retained rejected candidate points in red, in canonical map-pixel coordinates.
     * @return Number of markers or stopped/projection failure. Missing sampler positions have no invented marker.
     * @ownership Renderer is synchronously borrowed; callbacks must not destroy/reenter preview; no locks.
     * @remarks Historical spatial rejection is not a failed actor, reservation or current occupancy fact.
     * @cost Bounded trace capture/projection and drawing per presentation, at most 256 markers.
     */
    [[nodiscard]] Result<std::size_t> drawPlacementTrace(graphics::ISolidRectRenderer& renderer) const;
    /** @brief Draw only actual preview actors through the canonical presentation boundary in map-pixel coordinates.
     * @return Draw count or canonical presentation error; renderer exceptions propagate after prior draws.
     * @ownership Synchronous renderer borrow; callbacks must not destroy/reenter this preview. No locks are held.
     * @cost Linear source/member observation and owning draw batch allocation per presentation.
     */
    [[nodiscard]] Result<std::size_t> draw(graphics::ISolidRectRenderer&    renderer,
                                           const map_rpg_spawn::ActorStyle& style) const;
    /** @brief Explicitly remove private preview units; failure retains the session for cleanup retry.
     * @return Success (also when already stopped) or actual canonical domain-cleanup failure.
     * @cost Linear private source/unit cleanup per explicit stop.
     */
    [[nodiscard]] Result<void> stop();
    /** @brief True while this owner retains an active private session; does not poll or advance simulation. */
    [[nodiscard]] bool active() const noexcept;
    /** @brief Committed editor revision frozen at creation, independent of subsequent form changes. */
    [[nodiscard]] Revision sourceRevision() const noexcept;

private:
    struct Impl;
    explicit RPGSpawnPreview(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rpg_spawn_editor
