#pragma once
/** @file RPGMapSession.h
 * @brief map_rpg_spawn module-interface: owning game-session composition and retryable domain event handoff.
 * @remarks Explicit services/content in; lifecycle events are borrowed, never polled or stolen.
 * No ECS model, capability registry, script or tool protocol is introduced here.
 */
#include "common/Export.h"
#include "common/Snapshot.h"
#include "spawn/SpawnWorld.h"
namespace ecs {
class Table;
}
namespace eve::service {
class IFileSystem;
}
namespace eve::map {
class Map;
}
namespace eve::rpg {
struct VitalsEvent;
class SkillConditionBindings;
}  // namespace eve::rpg
namespace eve::rpg_spawn {
class RPGUnitCatalogue;
struct ActorSnapshot;
}  // namespace eve::rpg_spawn
namespace eve::graphics {
class ISolidRectRenderer;
}
namespace eve::map_spawn {
class MapSpawnSession;
class PreparedMapNavigation;
enum class PlacementTraceMode;
}  // namespace eve::map_spawn
namespace eve::map_rpg_spawn {
struct ActorStyle;
class PreparedRPGMapSession;
class PreparedRPGMapRestore;
class RPGMapCheckpoint;
class RPGMapArchive;
/**
 * @brief Owning RPG/map runtime composition with durable-in-memory lifecycle handoff across rejected frames.
 * @ownership Shares the actual domain Table/catalogue and owns the map spawn session; borrows Map per operation.
 * Close before replacing/unloading Map. Destruction releases resources and never implicitly removes actors.
 * @thread Simulation/render owner thread outside ECS Views; reentrant mutations are rejected.
 */
class EVENGINE_API_WORLD RPGMapSession {
public:
    /** @brief Release local resources; explicit close owns domain cleanup responsibility. */
    ~RPGMapSession();
    RPGMapSession(const RPGMapSession&)            = delete;
    RPGMapSession& operator=(const RPGMapSession&) = delete;
    /**
     * @brief Translate the caller's event batch, retain pending facts, and advance the coordinated map frame.
     * @return Owning report; rejection/exception retains translated facts for a later retry.
     * @remarks Pass each already-polled event batch once; an empty batch retries retained facts. Never polls RPG.
     * @cost Linear in tracked actors/events/pending facts plus bounded spawn work per frame.
     */
    [[nodiscard]] Result<spawn::AdvanceReport> advance(map::Map& target, const SimulationStep& step,
                                                       std::span<const rpg::VitalsEvent> events,
                                                       spawn::ActivationFrame            activation);
    /** @brief Retain one already-produced resource batch without advancing the scheduler or domain.
     * @return Failure preserves previously retained facts; success accepts this batch exactly once.
     * @ownership Input is borrowed synchronously. The caller owns its consumption watermark and must not resubmit.
     * @thread Owner thread outside Views; no gameplay callbacks or locks; reentrant calls reject.
     * @cost Linear tracked actor/event reconciliation per explicit lifecycle handoff.
     * @throws std::bad_alloc Prior retained facts survive; no partial bridge publication. */
    [[nodiscard]] Result<void> retainLifecycle(std::span<const rpg::VitalsEvent> events);
    /** @brief Share the actual RPG Table for canonical gameplay; external owners may retain retired actors.
     * @ownership Real shared ownership, independent of this session's lifetime; no default Table wrapper.
     */
    [[nodiscard]] std::shared_ptr<ecs::Table> world() const noexcept;
    /** @brief Borrow read-only spawn diagnostics; no mutable scheduler bypass. */
    [[nodiscard]] const map_spawn::MapSpawnSession& spawning() const noexcept;
    /** @brief Observe one source-owned actual actor through the canonical factory identity index.
     * @return Owning resource/position snapshot, or busy/closing/stale/missing error; no callbacks or mutation.
     * @cost Logarithmic identity lookup per explicit query on the simulation owner thread.
     */
    [[nodiscard]] Result<rpg_spawn::ActorSnapshot> inspectActor(SubjectRef subject) const;
    /** @brief Apply finite nonnegative canonical HP damage to a current source-owned actor.
     * @return Actual damage or validation/lifetime error; no scheduler advance or lifecycle handoff.
     * @ownership Caller owns the active RPG event scope and subsequent event-batch dispatch; no pointer escapes.
     * @thread Session owner thread outside Views; rejects busy/closing and holds no locks.
     * @reentrancy Canonical attribute calculators must not mutate/destroy actor, Table or session owners.
     * @cost Logarithmic identity lookup and resource/event preparation per explicit damage action.
     */
    [[nodiscard]] Result<double> applyDamage(SubjectRef subject, double amount);
    /** @brief Reset bounded map placement observation without changing domain, schedule or RNG.
     * @return Failure while busy/closing or after map retirement; no callbacks, owner thread only.
     */
    [[nodiscard]] Result<void> resetPlacementTrace(map_spawn::PlacementTraceMode mode);
    /** @brief Queue controls, including from spawn observers; closing sessions reject new input. */
    [[nodiscard]] Result<void> enqueue(spawn::ControlCommand command);
    /** @brief Observe committed spawn events; callbacks may inspect or enqueue, never reenter advance. */
    [[nodiscard]] Result<Subscription> subscribe(Observer<spawn::SpawnEvent>::Callback callback);
    /** @brief Cheap revision query delegated to the canonical map session. */
    [[nodiscard]] Result<bool> navigationNeedsRefresh(map::Map& target) const;
    /** @brief Explicitly capture owning navigation for later canonical commit.
     * @cost Grid-sized capture per changed map revision; no implicit capture in advance. */
    [[nodiscard]] Result<std::unique_ptr<map_spawn::PreparedMapNavigation>> prepareNavigationRefresh(map::Map& target);
    /** @brief Draw actual source-owned actors after an accepted frame through the existing renderer.
     * @cost Linear actor observation and temporary batch allocation per presentation. */
    [[nodiscard]] Result<std::size_t> draw(graphics::ISolidRectRenderer& renderer, const ActorStyle& style) const;
    /** @brief Explicitly close; failures retain domain ownership and pending lifecycle responsibility.
     * @cost Linear source/domain cleanup per unload attempt. */
    [[nodiscard]] Result<void> close(spawn::DetachPolicy policy);
    /** @brief Freeze core, domain and retained lifecycle inputs at one quiescent session boundary.
     * @param target Same live Map with current navigation; borrowed only for this operation.
     * @param saveEpoch Non-nil shared save identity; checkpointRevision must be nonzero.
     * @param eventSkills Pending-event skill content IDs; synchronous borrow, never retained.
     * @param eventEffects Pending-event effect content IDs; synchronous borrow, never retained.
     * @return Owning joined facts or failure without consuming pending events or modifying the live world.
     * @remarks Rejects missing/mismatched membership and unhanded-off deaths. Does not poll RPG queues;
     * the caller must hand off already-polled events through advance first. Other gameplay events, map assets,
     * persistent encoding and coordinated restore remain separate save participants. No callbacks or reentry.
     * @cost Full core/domain capture plus indexed membership validation and lifecycle copy per save attempt.
     * @throws std::bad_alloc Allocation failed without source mutation or lifecycle consumption.
     */
    [[nodiscard]] Result<RPGMapCheckpoint> capture(map::Map& target, PersistentId saveEpoch,
                                                   Revision                     checkpointRevision,
                                                   std::span<const std::string> eventSkills  = {},
                                                   std::span<const std::string> eventEffects = {});

private:
    friend class PreparedRPGMapSession;
    friend class PreparedRPGMapRestore;
    struct Impl;
    explicit RPGMapSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/**
 * @brief Hidden owning RPG/map composition candidate; no actors exist before publication and advance.
 * @ownership Shares actual domain/content owners, owns factory/occupancy/map candidate. No temporary borrows retained.
 * @thread Map/domain owner thread, non-reentrant, outside ECS Views.
 */
class EVENGINE_API_WORLD PreparedRPGMapSession {
public:
    /** @brief Cancel the unpublished candidate; no business cleanup. */
    ~PreparedRPGMapSession();
    /** @brief Transfer candidate ownership. */
    PreparedRPGMapSession(PreparedRPGMapSession&&) noexcept;
    /** @brief Cancel current candidate and transfer ownership. */
    PreparedRPGMapSession& operator=(PreparedRPGMapSession&&) noexcept;
    PreparedRPGMapSession(const PreparedRPGMapSession&)            = delete;
    PreparedRPGMapSession& operator=(const PreparedRPGMapSession&) = delete;
    /**
     * @brief Compose real RPG factory/occupancy and prepare the canonical isolated map transaction.
     * @param target Borrowed same Map used later for commit/advance; owner lifetime is observed canonically.
     * @param binding Borrowed strict versioned map-spawn asset binding.
     * @param files Borrowed canonical project asset reader, retained only during preparation.
     * @param templates Borrowed immutable template resolver, not retained by the result.
     * @param owner Unique scheduler incarnation identity supplied by the game.
     * @param seed Injected deterministic spawn seed.
     * @param objectContract Borrowed map object contract validated during preparation.
     * @param world Actual shared domain Table; no default-world or no-op-deleter ownership conversion.
     * @param units Shared canonical unit catalogue; templates is borrowed during preparation only.
     * @param bodyRadius Explicit uniform World2D occupancy radius, not a visual marker size.
     * @param players Owning initial World2D player frame; empty is known-no-players.
     * @param conditions Shared actual policy owner installed in spawned actor Skills; no callbacks during admission.
     * @return Owning candidate; failures do not publish layers or actors.
     * @cost Asset/dependency admission and map navigation capture, once per load.
     */
    [[nodiscard]] static Result<PreparedRPGMapSession> prepare(
        map::Map& target, const Value& binding, service::IFileSystem& files, const spawn::ITemplateResolver& templates,
        std::shared_ptr<ecs::Table> world, std::shared_ptr<rpg_spawn::RPGUnitCatalogue> units, PersistentId owner,
        std::uint64_t seed, std::string_view objectContract, double bodyRadius, std::vector<spawn::Point> players,
        std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Publish the canonical map candidate and transfer the admitted game session.
     * @return Retryable failure or owning runtime session. Map identity is checked by the canonical transaction.
     * @cost Candidate-sized map publication; no file reads or actor creation. */
    [[nodiscard]] Result<std::unique_ptr<RPGMapSession>> commit(map::Map& target);

private:
    struct Impl;
    explicit PreparedRPGMapSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Owning admitted map/RPG restoration, with a fresh isolated attempt for every commit.
 * @ownership Retains immutable same-boundary archive, explicit spatial inputs and a weak Map lease; no live Map,
 * Table, actors, spatial/factory provider or digest callback is retained by preparation. Cancellation releases local
 * CPU owners.
 * @thread Simulation/map owner thread outside Views; no mutation/replacement reentry on active operations.
 */
class EVENGINE_API_WORLD PreparedRPGMapRestore {
public:
    /** @brief Release admitted local facts only. */
    ~PreparedRPGMapRestore();
    /** @brief Transfer admitted restoration. */
    PreparedRPGMapRestore(PreparedRPGMapRestore&&) noexcept;
    /** @brief Release previous facts and transfer ownership. */
    PreparedRPGMapRestore& operator=(PreparedRPGMapRestore&&) noexcept;
    PreparedRPGMapRestore(const PreparedRPGMapRestore&)            = delete;
    PreparedRPGMapRestore& operator=(const PreparedRPGMapRestore&) = delete;
    /** @brief Admit one complete map save without materializing actors or publishing map layers.
     * @return Owning candidate or failure without target mutation; copies complete input before digest callbacks.
     * @param players Explicit initial player capability; absent differs from known empty, never inferred from save.
     * @remarks Same safe RPG/map participant scope as RPGMapArchive; textures/object/render changes remain separate.
     * @ownership Shares actual immutable condition owner; input/hash references are not retained.
     * @cost Whole artifact and canonical participant admission per prepared restore.
     * @throws std::bad_alloc Admission failed without publication.
     */
    [[nodiscard]] static Result<PreparedRPGMapRestore> prepare(
        map::Map& target, const SnapshotEnvelope& envelope, PersistentId saveEpoch,
        const SnapshotHashProvider& hashProvider, double bodyRadius, std::optional<std::vector<spawn::Point>> players,
        std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Build an isolated attempt, materialize saved actors, publish Map, revoke old sources and exchange owner.
     * @return Retired previous owner (possibly null), or failure preserving Map, active owner and this candidate.
     * @ownership Old subscriptions remain old; external old Table owners may retain bodies, but old leases revoke.
     * No digest/filesystem/gameplay callbacks or events occur during final publication and owner exchange.
     * @thread Map owner thread with its existing layer Table current/alive, outside Views; reentry rejects.
     * Callbacks must not mutate/destroy owners or their backing Tables.
     * @cost Full captured map parsing/grid rebuild, source binding and saved actor creation per commit attempt.
     * @throws std::bad_alloc Attempt failed before Map publication; local attempt cleanup preserves retry facts.
     */
    [[nodiscard]] Result<std::unique_ptr<RPGMapSession>> commit(map::Map&                       target,
                                                                std::unique_ptr<RPGMapSession>& active);

private:
    friend class PreparedRPGRuntimeRestore;
    /** @brief Revoke a private idle session after replacement publication; no callbacks or allocations. */
    static void                                        retireOwnedSession(RPGMapSession& session) noexcept;
    [[nodiscard]] static Result<PreparedRPGMapRestore> prepareAdmitted(
        map::Map& target, std::shared_ptr<const RPGMapArchive> archive, double radius,
        std::optional<std::vector<spawn::Point>> players);
    struct Impl;
    explicit PreparedRPGMapRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rpg_spawn
