#pragma once
/** @file MapSpawnSpace.h
 * @brief map_spawn module-interface: injected spatial providers and optional owning bounded observations.
 * @remarks Trace is transient diagnostic output, not ECS state, save data, capability registration or tool protocol.
 */
#include "common/Export.h"
#include "spawn/SpawnSpace.h"
#include "spawn/SpawnWorld.h"
namespace eve::spawn {
class SpawnRestoreState;
}
namespace eve::map {
class NavigationGridSnapshot;
class MapLease;
}  // namespace eve::map
namespace eve::map_spawn {
/** @brief Explicit observational recording mode; never changes placement decisions or RNG consumption. */
enum class PlacementTraceMode { Disabled, RecentAttempts };
/** @brief Actual placement decision; first failing constraint follows admitted configuration order. */
enum class PlacementDecision {
    Accepted,
    OutsideRegion,
    Walkability,
    TerrainClearance,
    PlayerDistance,
    Occupied,
    BatchClearance,
    SamplingError
};
/** @brief Distinguish planning from final checks, which may repeat for the same operation. */
enum class PlacementStage { Sampling, FinalCheck };
/** @brief Owning observation, not a source lease, reservation or successful domain commit. */
struct PlacementAttempt {
    spawn::SpawnerHandle        source;
    PersistentId                operation;
    SubjectRef                  subject;
    std::optional<spawn::Point> point;
    std::uint32_t               attempt  = 0;
    PlacementStage              stage    = PlacementStage::Sampling;
    PlacementDecision           decision = PlacementDecision::Accepted;
};
/** @brief Detached oldest-to-newest trace; retained rows are capped at 256, dropped count saturates at uint64 max.
 * @remarks Missing point means the canonical sampler returned no candidate or failed. Accepted is spatial only.
 * No runtime pointers, content owners or provider references escape; no persistent schema is introduced.
 */
struct PlacementTrace {
    PlacementTraceMode            mode    = PlacementTraceMode::Disabled;
    std::uint64_t                 dropped = 0;
    std::vector<PlacementAttempt> attempts;
};
/** @brief Axis-aligned navigation cell geometry in explicit World2D units. */
struct SpawnGridTransform {
    spawn::Point origin;
    spawn::Point cellSize{1, 1};
};
/**
 * @brief Real map navigation placement plus final spatial admission.
 * @ownership Owns compiled content and immutable navigation captures; shares injected occupancy service.
 * @thread Simulation owner thread, non-reentrant; no gameplay callbacks or locks.
 * @remarks Owner and content must match the attached SpawnWorld. Slots follow its stable instance-ID order.
 */
class EVENGINE_API_WORLD MapSpawnSpace final : public spawn::ISpawnPlacement {
public:
    /** @brief Bind owning content and optional occupancy; admission rejects missing required capabilities.
     * @cost Copies/moves content and builds slot ordering, amortized once per map incarnation. */
    MapSpawnSpace(spawn::CompiledMap map, PersistentId owner, SpawnGridTransform transform,
                  std::shared_ptr<spawn::ISpawnOccupancy> occupancy);
    /** @brief Prepare spatial bindings from admitted saved slots and their pinned canonical placement content.
     * @return Owning provider or consumed/empty state failure; does not bind live sources or read current assets.
     * @ownership Copies immutable placement owners; never retains the restore candidate, providers or live handles.
     * @thread Simulation owner thread, no callbacks or ECS mutation. Install navigation/players explicitly afterward.
     * @cost Linear occupied binding copy and slot index allocation per restored map provider.
     * @throws std::bad_alloc Local construction failed without source mutation.
     */
    [[nodiscard]] static Result<std::shared_ptr<MapSpawnSpace>> prepareRestored(
        const spawn::SpawnRestoreState& state, SpawnGridTransform transform,
        std::shared_ptr<spawn::ISpawnOccupancy> occupancy);
    /** @brief Release local snapshots; never move or delete domain units. */
    ~MapSpawnSpace() override;
    MapSpawnSpace(const MapSpawnSpace&)            = delete;
    MapSpawnSpace& operator=(const MapSpawnSpace&) = delete;
    /** @brief Bind a Map owner once at composition time; a revoked Map prevents subsequent spatial publication.
     * @return Failure for an invalid observation or repeat binding; standalone snapshot providers may remain unbound.
     */
    [[nodiscard]] Result<void> bindMapLifetime(map::MapLease owner);
    /** @brief Atomically replace a valid orthogonal navigation capture; invalid input preserves prior capture.
     * @cost Constant shared snapshot transfer, amortized per explicit navigation refresh. */
    [[nodiscard]] Result<void> setNavigation(map::NavigationGridSnapshot navigation);
    /** @brief Own a finite player frame; empty means no player exclusion disks; absent frame is a missing capability.
     * @cost Linear in players (at most 4096), amortized once per changed player frame. */
    [[nodiscard]] Result<void> setPlayers(std::span<const spawn::Point> players);
    /** @brief Explicitly reset the bounded observation buffer and select recording mode; no callbacks or RNG use.
     * @thread Provider owner thread outside plan/check; no reentry or concurrent operations.
     * @ownership All previous captures remain owning; clearing does not affect placement or live source identity.
     */
    void resetTrace(PlacementTraceMode mode) noexcept;
    /** @brief Copy at most 256 recent actual decisions without querying occupancy, sampling or advancing.
     * @return Owning trace, valid after provider destruction; allocation failure leaves provider unchanged.
     * @thread Provider owner thread, no callbacks or reentry. Disabled recording returns an empty capture.
     * @cost Bounded allocation/copy of recent attempts per explicit diagnostic refresh.
     */
    [[nodiscard]] PlacementTrace captureTrace() const;
    /** @brief Validate requested constraints, transform and current capability availability without mutation. */
    [[nodiscard]] Result<void> validate(const spawn::PlacementSpec&, const spawn::Region&) const override;
    /** @brief Bounded sampling using the injected placement RNG; blocked results publish no units.
     * @remarks Optional tracing records actual attempts even when the batch is blocked; no additional RNG draws.
     * @cost At most units times attempts times 4096 grid cells plus occupancy, amortized per batch. */
    [[nodiscard]] Result<spawn::PlacementOutcome> plan(const spawn::BatchRequest&, const spawn::PlacementSpec&,
                                                       const spawn::Region&, std::uint64_t& randomState) override;
    /** @brief Recheck owning positions against current terrain/players/occupancy, without changing RNG.
     * @remarks Optional diagnostic recording changes only the bounded observation buffer, not spatial/domain facts.
     * @return Committed means spatially ready only; domain publication remains with the factory.
     * @cost Linear in positions times bounded grid/occupancy work, amortized per final commit. */
    [[nodiscard]] Result<spawn::CommitOutcome> check(const spawn::BatchRequest&, std::span<const spawn::Point>) const;

private:
    friend class MapSpawnSession;
    [[nodiscard]] Result<std::optional<std::vector<spawn::Point>>> replacePlayers(
        std::span<const spawn::Point> players);
    void restorePlayers(std::optional<std::vector<spawn::Point>> players) noexcept;
    struct Impl;
    explicit MapSpawnSpace(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/**
 * @brief Atomic domain factory decorator that revalidates space immediately before domain publication.
 * @ownership Shares actual domain factory/space; prepared tokens weak-observe this decorator's lifetime.
 * @thread Simulation owner thread; no concurrent movement/navigation changes during a synchronous commit.
 */
class EVENGINE_API_WORLD SpatialSpawnFactory final : public spawn::ISpawnFactory {
public:
    /** @brief Bind actual domain factory and map space; missing services fail operations explicitly. */
    SpatialSpawnFactory(std::shared_ptr<spawn::ISpawnFactory> domain, std::shared_ptr<MapSpawnSpace> space);
    /** @brief Cancel local unpublished tokens by revoking the decorator owner; never destroy units. */
    ~SpatialSpawnFactory() override;
    /** @brief Borrow the actual owned domain factory through nested decorators; self if no delegate is installed.
     * @ownership Synchronous borrow under this decorator's lifetime, no callbacks/allocation or state changes.
     */
    [[nodiscard]] const spawn::ISpawnFactory& domainFactory() const noexcept override;

    SpatialSpawnFactory(const SpatialSpawnFactory&)            = delete;
    SpatialSpawnFactory& operator=(const SpatialSpawnFactory&) = delete;
    /** @brief Prepare actual domain transaction after current spatial admission.
     * @cost Domain preparation plus bounded space checks, amortized per batch. */
    [[nodiscard]] Result<spawn::PrepareOutcome> prepare(const spawn::BatchRequest&,
                                                        std::span<const spawn::Point>) override;
    /** @brief Delegate explicit cleanup to the actual domain owner.
     * @cost Proportional to requested domain cleanup, amortized per detach. */
    [[nodiscard]] Result<spawn::RemovalReceipt> remove(const spawn::RemovalRequest&) override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace eve::map_spawn
