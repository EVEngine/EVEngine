#pragma once

#include "common/Result.h"
#include "common/Time.h"
#include "pixelworld/PixelMaterial.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace eve::pixelworld {

class PixelWorldControlService;

inline constexpr int kPixelChunkSize = 64;
inline constexpr std::uint8_t kPixelSleepHysteresisTicks = 3;

/** @brief Compact authoritative state for one simulated world pixel. */
struct PixelCell {
    MaterialId material = MaterialId::Air;
    std::int16_t temperature = 20;
    std::uint8_t lifetime = 0;
    /** @brief Canonical sub-degree thermal energy in [0, material heatCapacity). */
    std::uint16_t thermalRemainder = 0;

    friend bool operator==(const PixelCell&, const PixelCell&) = default;
};

/** @brief Counters produced by one deterministic simulation step. */
struct StepStats {
    eve::SimulationTick tick = eve::SimulationTick::zero();
    std::uint32_t chunksVisited = 0;
    std::uint32_t cellsVisited = 0;
    std::uint32_t cellsChanged = 0;
    std::uint32_t cellsMoved = 0;
    std::uint32_t temperatureTransfers = 0;
    std::uint64_t thermalEnergyTransferred = 0;
    std::uint64_t thermalEnergyClamped = 0;
    std::uint32_t phaseChanges = 0;
    std::uint32_t reactions = 0;
    std::uint32_t parallelTasks = 0;
    std::uint32_t chunksReclaimed = 0;
};

/**
 * @brief Synchronous executor for owning PixelWorld candidate buffers.
 *
 * Implementations may schedule indices on worker threads but must join before
 * returning. PixelWorld controls the callback, which reads immutable phase state
 * and writes only its index-owned buffer; implementations must not retain it.
 */
class PixelWorkScheduler {
public:
    virtual ~PixelWorkScheduler() = default;
    /** @brief Execute every index in [0, workItems) exactly once and synchronously join. */
    virtual void parallelFor(std::size_t workItems,
                             const std::function<void(std::size_t)>& body) = 0;
    /** @brief Observable worker capacity used for diagnostics only. */
    [[nodiscard]] virtual std::size_t workerCount() const noexcept = 0;
};

/**
 * @brief Owning, immutable projection of one authoritative simulation chunk.
 *
 * The snapshot remains valid independently of subsequent world mutation. Cells
 * are row-major and contain `kPixelChunkSize * kPixelChunkSize` entries unless
 * `removed` is true, in which case cells is empty and consumers must erase projection.
 */
struct PixelChunkSnapshot {
    int x = 0;
    int y = 0;
    std::uint64_t revision = 0;
    bool removed = false;
    std::vector<PixelCell> cells;

    friend bool operator==(const PixelChunkSnapshot&, const PixelChunkSnapshot&) = default;
};

/** @brief Owning authoritative Chunk correction with source world metadata. */
struct PixelChunkBatch {
    std::uint64_t catalogFingerprint = 0;
    std::uint64_t sourceSeed = 0;
    std::uint64_t sourceRevision = 0;
    eve::SimulationTick sourceTick = eve::SimulationTick::zero();
    std::uint64_t sourceLastEditSequence = 0;
    /** @brief Replace the replica projection instead of applying an incremental correction. */
    bool fullResync = false;
    std::vector<PixelChunkSnapshot> chunks;

    friend bool operator==(const PixelChunkBatch&, const PixelChunkBatch&) = default;
};

/** @brief Inclusive finite rectangle expressed in Chunk coordinates. */
struct PixelChunkRegion {
    int minX = 0;
    int minY = 0;
    int maxX = -1;
    int maxY = -1;

    friend bool operator==(const PixelChunkRegion&, const PixelChunkRegion&) = default;
};

/** @brief Read-only per-Chunk simulation diagnostics for tooling and overlays. */
struct PixelChunkDiagnostic {
    int x = 0;
    int y = 0;
    std::uint64_t revision = 0;
    std::uint32_t nonAirCells = 0;
    std::uint32_t mobileCells = 0;
    std::int16_t minimumTemperature = 20;
    std::int16_t maximumTemperature = 20;
    std::uint8_t idleTicks = 0;
    bool active = false;
};

/** @brief Receipt for one all-or-nothing authoritative Chunk batch application. */
struct PixelChunkApplyReceipt {
    std::uint64_t revisionBefore = 0;
    std::uint64_t revisionAfter = 0;
    std::uint32_t chunksReplaced = 0;
    std::uint32_t chunksRemoved = 0;
    std::uint64_t worldEpoch = 0;
};

/** @brief Receipt for one transactional compatible material Catalog hot reload. */
struct PixelCatalogReloadReceipt {
    std::uint64_t fingerprintBefore = 0;
    std::uint64_t fingerprintAfter = 0;
    std::uint64_t revisionBefore = 0;
    std::uint64_t revisionAfter = 0;
    std::uint64_t worldEpoch = 0;
    std::uint32_t chunksRebuilt = 0;
    bool replayHistoryInvalidated = false;
};

/** @brief Canonical deterministic edit operation admitted at a Tick boundary. */
enum class PixelEditKind : std::uint8_t { PaintCircle, HeatCircle, Explosion };

/**
 * @brief Owning replayable edit command. Sequence must be exactly previous + 1.
 * @remarks `material` is used by PaintCircle; `strength` by Explosion; and
 * `temperatureDelta` by HeatCircle/Explosion.
 */
struct PixelEditCommand {
    std::uint64_t sequence = 0;
    PixelEditKind kind = PixelEditKind::PaintCircle;
    int centerX = 0;
    int centerY = 0;
    int radius = 0;
    MaterialId material = MaterialId::Air;
    int strength = 0;
    std::int16_t temperatureDelta = 0;
};

/** @brief Deterministic receipt for one atomically accepted edit command. */
struct PixelEditReceipt {
    std::uint64_t sequence = 0;
    std::uint64_t revisionBefore = 0;
    std::uint64_t revisionAfter = 0;
    std::uint32_t cellsChanged = 0;
    std::uint32_t cellsRemoved = 0;
    std::uint32_t cellsHeated = 0;
};

/** @brief Finite inclusive world-space rectangle used by structural queries. */
struct PixelRegion {
    int minX = 0;
    int minY = 0;
    int maxX = -1;
    int maxY = -1;
};

/** @brief Runtime identity of the world epoch from which a fragment was detached. */
struct PixelWorldLink {
    std::uint64_t world = 0;
    std::uint64_t epoch = 0;
    friend bool operator==(const PixelWorldLink&, const PixelWorldLink&) = default;
};

/**
 * @brief Owning material bitmap detached atomically from a PixelWorld.
 * @remarks `cells` is row-major in the tight `width` by `height` bounds. Air
 * preserves holes. The artifact remains readable after its source world dies;
 * its link is accepted for rasterization only by the same live world epoch.
 */
struct PixelFragment {
    PixelWorldLink source;
    std::uint64_t id = 0;
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    std::uint32_t solidCellCount = 0;
    std::vector<PixelCell> cells;
};

/** @brief Receipt for one all-or-nothing fragment rasterization. */
struct PixelFragmentRasterReceipt {
    std::uint64_t fragmentId = 0;
    std::uint64_t revisionBefore = 0;
    std::uint64_t revisionAfter = 0;
    std::uint32_t cellsPlaced = 0;
};

/**
 * @brief Sparse, chunked, deterministic 2D falling-material world.
 *
 * PixelWorld is the sole authority for material state. It is simulation-thread
 * affine and invokes no callbacks. Rendering and physics consume snapshots or
 * queries through adapters; neither owns a mutable copy of the world.
 */
class PixelWorld {
public:
    /** @brief Construct an empty world with an explicit deterministic seed. */
    explicit PixelWorld(std::uint64_t seed = 1);
    /** @brief Construct an empty world owning a validated immutable material catalog. */
    PixelWorld(std::uint64_t seed, MaterialCatalog catalog);
    ~PixelWorld();
    PixelWorld(PixelWorld&&) noexcept;
    PixelWorld& operator=(PixelWorld&&) noexcept;
    PixelWorld(const PixelWorld&) = delete;
    PixelWorld& operator=(const PixelWorld&) = delete;

    /** @brief Return the cell at world coordinates; absent chunks read as air. */
    PixelCell getCell(int x, int y) const noexcept;
    /** @brief Convenience query returning the numeric material id. */
    int getMaterial(int x, int y) const noexcept;
    /** @brief Query whether a validated material id participates in structural solid collision. */
    bool isSolidMaterial(MaterialId material) const noexcept;
    /** @brief Query the Catalog-owned preview/render color encoded as 0xRRGGBBAA. */
    std::uint32_t materialDisplayRgba(MaterialId material) const noexcept;
    /**
     * @brief Transactionally hot-reload a compatible Catalog while paused.
     * @remarks Material count, ids and names must remain stable so existing cells keep their
     * meaning. Success rebuilds Chunk metadata, advances epoch, and invalidates prior replay
     * history because rule semantics changed. Failure leaves all observable state unchanged.
     */
    [[nodiscard]] eve::Result<PixelCatalogReloadReceipt> reloadMaterialCatalog(
        MaterialCatalog catalog, std::uint64_t expectedFingerprint);
    /** @brief Set one cell and wake its chunk and neighbors. Air never allocates a missing chunk. */
    void setCell(int x, int y, PixelCell cell);
    /** @brief Set one cell from a stable built-in material name. */
    void setMaterial(int x, int y, std::string_view material);
    /** @brief Canonical checked name-based setter; unknown names leave the world unchanged. */
    [[nodiscard]] eve::Result<void> setMaterialChecked(int x, int y, std::string_view material);
    /** @brief Paint a filled material circle and return the number of changed cells. */
    std::size_t paintCircle(int centerX, int centerY, int radius, std::string_view material);
    /** @brief Canonical checked circle edit; unknown names leave the world unchanged. */
    [[nodiscard]] eve::Result<std::size_t> paintCircleChecked(int centerX, int centerY, int radius,
                                                              std::string_view material);
    /**
     * @brief Validate and atomically apply one strictly sequenced edit command.
     * @remarks Simulation-thread affine; invokes no callbacks. Rejection leaves
     * material cells, revision and last edit sequence unchanged.
     */
    [[nodiscard]] eve::Result<PixelEditReceipt> applyEdit(const PixelEditCommand& command);
    /** @brief Script/demo explosion facade that emits the next command sequence. */
    PixelEditReceipt explode(int centerX, int centerY, int radius, int strength,
                             std::int16_t temperatureDelta);
    /**
     * @brief Atomically detach unsupported solid components inside a finite region.
     * @param region Inclusive scan bounds; area is limited to prevent accidental unbounded work.
     * @param supportY Components touching this y coordinate or below remain authoritative terrain.
     * @param minimumCells Components smaller than this remain terrain.
     * @return Owning fragments in canonical top-left order. A component touching solid terrain
     * outside `region` is treated as supported. Failure leaves the world unchanged.
     */
    [[nodiscard]] eve::Result<std::vector<PixelFragment>> extractUnsupportedFragments(
        PixelRegion region, int supportY, std::uint32_t minimumCells = 1);
    /**
     * @brief Transactionally rasterize a detached fragment at a new world-space origin.
     * @remarks Rejects stale/wrong-world links and any overlap with non-air cells. The fragment
     * is borrowed for the call and is never retained or mutated.
     */
    [[nodiscard]] eve::Result<PixelFragmentRasterReceipt> rasterizeFragment(
        const PixelFragment& fragment, int originX, int originY);
    /** @brief Current runtime link used for stale fragment detection. */
    PixelWorldLink worldLink() const noexcept;
    /** @brief Remove all chunks and reset the simulation tick. */
    void clear() noexcept;

    /**
     * @brief Advance exactly one scheduler-owned tick.
     * @return Deterministic counters for the completed step.
     */
    [[nodiscard]] eve::Result<StepStats> advance(eve::SimulationTick tick);
    /**
     * @brief Advance one deterministic tick using a synchronous candidate scheduler.
     * @remarks Simulation-thread affine. Worker callbacks receive owning output slots,
     * never mutate chunks, and are joined before canonical commit.
     */
    [[nodiscard]] eve::Result<StepStats> advanceScheduled(eve::SimulationTick tick,
                                                          PixelWorkScheduler& scheduler);
    /** @brief Script/demo facade that advances the next integral tick. */
    StepStats step();

    /** @brief Pause or resume ordinary simulation entry points without changing state. */
    void setPaused(bool paused) noexcept;
    /** @brief Whether ordinary simulation entry points are currently paused. */
    bool isPaused() const noexcept;

    std::uint64_t seed() const noexcept;
    std::uint64_t revision() const noexcept;
    std::uint64_t tickValue() const noexcept;
    std::uint64_t lastEditSequence() const noexcept;
    int chunkCount() const noexcept;
    int activeChunkCount() const noexcept;
    /** @brief Fingerprint of the immutable catalog that gives cell ids their meaning. */
    std::uint64_t materialCatalogFingerprint() const noexcept;

    /**
     * @brief Copy chunks changed after `sinceRevision` in canonical y/x order.
     * @remarks Simulation-thread affine. Returned snapshots own their cells and
     * remain valid after mutation, restore, or destruction of this world.
     */
    std::vector<PixelChunkSnapshot> snapshotChangedChunks(std::uint64_t sinceRevision) const;

    /**
     * @brief Copy present Chunks and retained tombstones in one finite Chunk-coordinate region.
     * @param region Inclusive bounds limited to 65536 Chunk coordinates.
     * @param sinceRevision Only projections newer than this revision are returned.
     * @return Canonical y/x-ordered owning projections, or bounded-input failure.
     */
    [[nodiscard]] eve::Result<std::vector<PixelChunkSnapshot>> snapshotChunksInRegion(
        PixelChunkRegion region, std::uint64_t sinceRevision = 0) const;

    /** @brief Query allocated Chunk diagnostics in a bounded inclusive region. */
    [[nodiscard]] eve::Result<std::vector<PixelChunkDiagnostic>> chunkDiagnostics(
        PixelChunkRegion region) const;

    /**
     * @brief Transactionally apply a canonical authoritative Chunk correction batch.
     * @param batch Owning source metadata and unique y/x-ordered Chunk snapshots.
     * @param expectedRevision Current local revision used to reject stale writers.
     * @return Commit receipt; any validation failure leaves all world state unchanged.
     * @remarks The source revision, Tick and edit sequence may advance but never rewind.
     * A successful correction invalidates outstanding fragment links by advancing epoch.
     */
    [[nodiscard]] eve::Result<PixelChunkApplyReceipt> applyChunkBatch(
        const PixelChunkBatch& batch, std::uint64_t expectedRevision);

    /** @brief Serialize schema `eve.pixelworld`, version 3, in canonical Chunk order. */
    [[nodiscard]] eve::Result<std::vector<std::byte>> saveSnapshot() const;
    /**
     * @brief Transactionally restore a version 1, 2 or 3 PixelWorld snapshot.
     * @remarks Older built-in-Catalog versions migrate explicitly. Unknown versions and malformed
     * input are rejected without changing this world; restore advances epoch on success.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshot(std::span<const std::byte> bytes);

private:
    friend class PixelWorldControlService;
    eve::Result<StepStats> advanceImpl(eve::SimulationTick tick, PixelWorkScheduler* scheduler);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::pixelworld
