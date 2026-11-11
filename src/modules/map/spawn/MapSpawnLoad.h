#pragma once
/** @file MapSpawnLoad.h
 * @brief map_spawn module-interface: explicit assets, isolated map admission and staged domain composition.
 * @remarks Uses injected file/content/domain services, no capability lookup. Owns no domain actors.
 * Lifecycle/activation events flow through SpawnWorld; script/tool/save adapters remain separate.
 */
#include "common/BorrowedRef.h"
#include "common/Export.h"
#include "spawn/SpawnWorld.h"
namespace eve::map_rpg_spawn {
class PreparedRPGMapRestore;
}
namespace eve::service {
class IFileSystem;
}
namespace eve::map {
class Map;
class PreparedMapLoad;
class MapGridState;
class MapLease;
struct MapGridGeometry;
}  // namespace eve::map
namespace eve::spawn {
class ISpawnOccupancy;
class SpawnRestoreState;
}  // namespace eve::spawn
namespace eve::map_spawn {
class MapSpawnSpace;
enum class PlacementTraceMode;
class MapSpawnAssets;
struct SpawnGridTransform;
class MapSpawnSession;
/** @brief Explicit isotropic pixel-to-world conversion; no tile-size inference. */
struct PixelTransform {
    spawn::Point origin;
    double       scale = 1;
};
/** @brief Convert finite map pixels using an explicit finite, positive isotropic transform.
 * @return Owning World2D point or InvalidArgument; no map/session pointer is retained.
 * @thread Calling thread; pure, reentrant, no callbacks or allocation on success.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<spawn::Point> pixelToWorld(PixelTransform transform, spawn::Point pixel);
/** @brief Invert the same admitted pixel transform for authoring/presentation.
 * @return Owning pixel point or InvalidArgument for invalid transform/input/unrepresentable output.
 * @thread Calling thread; pure, reentrant, no callbacks or allocation on success.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<spawn::Point> worldToPixel(PixelTransform transform, spawn::Point world);
/** @brief Owning domain inputs for map composition; positions are explicit, absent differs from an empty frame. */
struct SpawnDomainServices {
    std::shared_ptr<spawn::ISpawnFactory>    factory;
    std::shared_ptr<spawn::ISpawnOccupancy>  occupancy;
    std::optional<std::vector<spawn::Point>> players;
};
/**
 * @brief Owning navigation update captured outside the frame hot path; never retains a Map/entity pointer.
 * @ownership Weak-observes its source session; cancellation only releases the immutable capture.
 * @thread Simulation owner thread, no reentry or callbacks during publication.
 */
class EVENGINE_API_WORLD PreparedMapNavigation {
public:
    /** @brief Release an unpublished capture. */
    ~PreparedMapNavigation();
    /** @brief Transfer the capture, leaving an empty token. */
    PreparedMapNavigation(PreparedMapNavigation&&) noexcept;
    /** @brief Transfer the capture, cancelling the previous one. */
    PreparedMapNavigation& operator=(PreparedMapNavigation&&) noexcept;
    PreparedMapNavigation(const PreparedMapNavigation&)            = delete;
    PreparedMapNavigation& operator=(const PreparedMapNavigation&) = delete;
    /** @brief Publish only if Map owner, layer generation and tile revision still match the capture.
     * @return Changed/Unchanged, or failure leaving the prior navigation intact.
     * @ownership Borrows the same live Map only during this call.
     * @cost Constant snapshot transfer plus metadata admission; grid copying belongs to preparation. */
    [[nodiscard]] Result<spawn::Change> commit(map::Map& target);

private:
    friend class MapSpawnSession;
    struct Impl;
    explicit PreparedMapNavigation(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/**
 * @brief Owning live map spawn session, transferred only after map publication.
 * @ownership Owns scheduler/space/provider session, not Map or domain world. Close before unloading either.
 * @thread Simulation owner thread; no concurrent calls or world callback reentry.
 */
class EVENGINE_API_WORLD MapSpawnSession {
public:
    /** @brief Release local resources only; domain cleanup is explicit. */
    ~MapSpawnSession();
    MapSpawnSession(const MapSpawnSession&)            = delete;
    MapSpawnSession& operator=(const MapSpawnSession&) = delete;
    /** @brief Borrow read-only scheduler diagnostics until session destruction. */
    [[nodiscard]] const spawn::SpawnWorld& world() const noexcept;
    /** @brief Borrow read-only map-space diagnostics until session destruction. */
    [[nodiscard]] const MapSpawnSpace& space() const noexcept;
    /** @brief Reset optional bounded placement observations without changing simulation or RNG.
     * @return Failure while advancing/closing or after map retirement; previous captures remain owning.
     * @thread Session owner thread, outside callbacks; no reentry or callbacks during reset.
     */
    [[nodiscard]] Result<void> resetPlacementTrace(PlacementTraceMode mode);
    /** @brief Borrow current source handles until close or session destruction; copy handles to retain them. */
    [[nodiscard]] std::span<const spawn::SpawnerHandle> sourceHandles() const noexcept;
    /** @brief Check current Map owner/geometry/layer revision without capturing or mutating navigation.
     * @return True when an explicit refresh is needed; geometry/asset replacement requires a new session.
     * @cost Constant metadata work plus locator comparison, suitable for one check per frame. */
    [[nodiscard]] Result<bool> navigationNeedsRefresh(map::Map& target) const;
    /** @brief Capture a validated owning grid update; old navigation remains published until token commit.
     * @ownership Borrows Map for this call; token weak-observes the session.
     * @cost Grid-sized copying/capture, amortized per changed navigation revision. */
    [[nodiscard]] Result<std::unique_ptr<PreparedMapNavigation>> prepareNavigationRefresh(map::Map& target);
    /** @brief Advance one coherent scene frame only with current navigation; player facts also feed placement.
     * @return Owning report; invalid inputs preserve the prior player frame and scheduler state.
     * @remarks Refresh explicitly before retrying a dirty-map frame. Input spans are not retained.
     * @thread Owner thread outside map mutation; nested advances/refresh/close are rejected.
     * @cost Bounded scheduler work plus player-frame copy; never captures a grid implicitly. */
    [[nodiscard]] Result<spawn::AdvanceReport> advance(map::Map& target, const SimulationStep& step,
                                                       std::span<const spawn::LifecycleInput> lifecycle  = {},
                                                       spawn::ActivationFrame                 activation = {});
    /** @brief Queue owning controls; event callbacks may enqueue but closing sessions reject new work. */
    [[nodiscard]] Result<void> enqueue(spawn::ControlCommand command);
    /** @brief Subscribe to committed core events; callbacks obey the core's inspect/enqueue-only mutation contract. */
    [[nodiscard]] Result<Subscription> subscribe(Observer<spawn::SpawnEvent>::Callback callback);
    /** @brief Copy the admitted coordinate conversion. */
    [[nodiscard]] PixelTransform pixelTransform() const noexcept;
    /** @brief Share original structural asset inputs actually read by this load, without reading current files.
     * @ownership Immutable owner survives this session; runtime map edits and renderer assets are separate facts.
     */
    [[nodiscard]] std::shared_ptr<const MapSpawnAssets> assets() const noexcept;
    /** @brief Convert finite pixel coordinates, or reject an unrepresentable result. */
    [[nodiscard]] Result<spawn::Point> toWorld(spawn::Point pixel) const;
    /** @brief Convert finite World2D coordinates back to map pixels for presentation.
     * @return Owning pixel position, or failure if subtraction/division is unrepresentable.
     * @remarks Uses the same admitted transform as placement; does not read or mutate actors. */
    [[nodiscard]] Result<spawn::Point> toPixel(spawn::Point world) const;
    /** @brief Explicitly detach remaining sources; failure retains retryable remaining responsibilities.
     * @cost Linear in sources and their domain cleanup, amortized per unload. */
    [[nodiscard]] Result<void> close(spawn::DetachPolicy policy);

private:
    friend class PreparedMapSpawn;
    friend class eve::map_rpg_spawn::PreparedRPGMapRestore;
    [[nodiscard]] bool replacementReady(const map::MapLease& owner) const noexcept;
    void               retireForReplacement() noexcept;
    friend class PreparedMapNavigation;
    MapSpawnSession(std::unique_ptr<spawn::SpawnWorld>, std::shared_ptr<spawn::SpawnProviders>,
                    std::vector<spawn::SpawnerHandle>, PixelTransform, std::shared_ptr<MapSpawnSpace>, map::MapLease,
                    std::size_t, map::MapGridGeometry, std::string);
    std::unique_ptr<spawn::SpawnWorld>     world_;
    std::shared_ptr<spawn::SpawnProviders> providers_;
    std::vector<spawn::SpawnerHandle>      sources_;
    PixelTransform                         transform_;
    std::shared_ptr<MapSpawnSpace>         space_;
    struct NavigationBinding;
    std::shared_ptr<NavigationBinding> navigation_;
    void                               bindPublishedNavigation(map::Map& target) noexcept;
};
/**
 * @brief Owning hidden map/runtime candidate; cancellation creates no live actors or map layers.
 * @ownership Owns isolated map candidate and staged simulation; no borrowed file/content services retained.
 * @thread Simulation/Map owner thread, non-reentrant; commit must be outside ECS Views.
 */
class EVENGINE_API_WORLD PreparedMapSpawn {
public:
    /** @brief Cancel unpublished local resources; never perform business cleanup implicitly. */
    ~PreparedMapSpawn();
    /** @brief Transfer the unpublished candidate. */
    PreparedMapSpawn(PreparedMapSpawn&&) noexcept;
    /** @brief Transfer the candidate, cancelling the old unpublished candidate. */
    PreparedMapSpawn& operator=(PreparedMapSpawn&&) noexcept;
    PreparedMapSpawn(const PreparedMapSpawn&)            = delete;
    PreparedMapSpawn& operator=(const PreparedMapSpawn&) = delete;
    /**
     * @brief Read strict binding/assets, admit isolated map and derive its navigation before provider attachment.
     * @param target Borrowed target Map; its lifetime is weak-observed, never retained.
     * @param binding Borrowed eve.map-spawn-binding/v1, including optional zero-based navigation_layer.
     * @param files Borrowed canonical project asset reader for root and relative dependent assets.
     * @param templates Borrowed immutable template source.
     * @param units Borrowed canonical domain validator.
     * @param owner Unique live scheduler incarnation identity.
     * @param seed Injected deterministic simulation seed.
     * @param objectContract Borrowed map object contract, completely validated during preparation.
     * @param domain Owning domain factory, optional occupancy and optional initial player frame.
     * @return Owning candidate; every failure preserves target Map and domain actors.
     * @cost Asset/dependency parsing, isolated ECS, navigation capture and provider admission, once per map load.
     * @throws std::bad_alloc Input snapshot allocation failed without publication.
     */
    [[nodiscard]] static Result<PreparedMapSpawn> prepare(map::Map& target, const Value& binding,
                                                          service::IFileSystem&                 files,
                                                          const spawn::ITemplateResolver&       templates,
                                                          const spawn::IUnitDefinitionResolver& units,
                                                          PersistentId owner, std::uint64_t seed,
                                                          std::string_view objectContract, SpawnDomainServices domain);
    /**
     * @brief Publish the prepared map and transfer the already-admitted simulation session.
     * @param target Borrowed same Map used in prepare; mismatched/revoked owner fails without publication.
     * @return Owning session; failure preserves prior map and this retryable candidate.
     * @cost Candidate-sized component publication, once per successful load; no asset reads or callbacks.
     */
    [[nodiscard]] Result<std::unique_ptr<MapSpawnSession>> commit(map::Map& target);

    /** @brief Prepare saved map/grid and admitted core with actual domain providers, without publication.
     * @param target Actual target Map, weakly observed until successful commit.
     * @param assets Immutable original structural bytes, object contract and map binding.
     * @param grid Runtime cells/terrain/navigation matching the saved structural layers.
     * @param state Canonically admitted core, copied for this independent binding attempt.
     * @param domain Owning factory/occupancy and explicit optional player capability.
     * @return Isolated candidate or failure preserving target and admitted facts.
     * @ownership Owns staged map/core/providers and immutable assets. Inputs are borrowed synchronously.
     * @thread Map/simulation owner thread outside Views; no external callbacks or domain entity creation.
     * @cost Full structural map replay, grid staging and scheduler/provider binding per restore attempt.
     * @throws std::bad_alloc No target state published; caller may retry from the same admitted state.
     */
    [[nodiscard]] static Result<PreparedMapSpawn> prepareRestored(map::Map&                             target,
                                                                  std::shared_ptr<const MapSpawnAssets> assets,
                                                                  const map::MapGridState&              grid,
                                                                  const spawn::SpawnRestoreState&       state,
                                                                  SpawnDomainServices                   domain);
    /** @brief Observe the already-prepared core without exposing advance/detach or publishing the map.
     * @return Borrowed immutable core, absent after candidate move or successful commit.
     * @ownership Borrow expires at move/destruction/commit; never extends candidate lifetime.
     * @thread Candidate owner thread; constant observation, no callbacks or lazy work.
     */
    [[nodiscard]] OptionalRef<const spawn::SpawnWorld> world() const noexcept;

private:
    PreparedMapSpawn(std::unique_ptr<map::PreparedMapLoad>, std::unique_ptr<MapSpawnSession>);
    std::unique_ptr<map::PreparedMapLoad> mapCandidate_;
    std::unique_ptr<MapSpawnSession>      session_;
};
}  // namespace eve::map_spawn
