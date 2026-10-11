#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
class PointSet;
struct TerrainMultiTileReport;
struct TerrainStampSettings;

/** @brief Pcg-compatible rules for deriving tree scale from placement fitness. */
enum class TerrainTreeScaleMode : std::uint8_t { Fixed, Fitness, Random, FitnessRandomized };

/** @brief Height reference used when a tree does not snap directly to terrain. */
enum class TerrainTreeYOffsetMode : std::uint8_t { TerrainHeight, SeaLevel, Custom };

/** @brief Explicit deterministic tree placement configuration for one terrain rectangle and resource. */
struct TerrainTreePlacementSettings {
    float originX = 0, originZ = 0, width = 1, depth = 1, heightScale = 1;
    float spacing = 10, spawnDensity = 1, jitterPercent = 0.5F;
    float failureRate = 0, minimumFitness = 0.5F;
    bool  snapToTerrain = true;
    TerrainTreeYOffsetMode yOffsetMode = TerrainTreeYOffsetMode::TerrainHeight;
    float seaLevel = 0, customOffset = 0, minimumYOffset = -2, maximumYOffset = 2;
    TerrainTreeScaleMode scaleMode = TerrainTreeScaleMode::Fitness;
    float minimumWidth = 0.75F, maximumWidth = 1.5F;
    float minimumHeight = 0.75F, maximumHeight = 1.5F;
    float widthRandomPercentage = 0.5F, heightRandomPercentage = 0.5F;
    float healthyR = 1, healthyG = 1, healthyB = 1, healthyA = 1;
    float dryR = 1, dryG = 1, dryB = 1, dryA = 1;
    float bendFactor = 0, boundsRadius = 1;
    std::int32_t seed = 1;
    std::uint64_t namespaceId = 1;
    int maxPoints = 1000000;
    std::string asset;
};

/** @brief Previous and replacement prototype values used for atomic Pcg-style tree refresh. */
struct TerrainTreeRescaleSettings {
    TerrainTreeScaleMode scaleMode = TerrainTreeScaleMode::Fitness;
    float previousMinimumWidth = 0.75F, previousMaximumWidth = 1.5F;
    float previousMinimumHeight = 0.75F, previousMaximumHeight = 1.5F;
    float minimumWidth = 0.75F, maximumWidth = 1.5F;
    float minimumHeight = 0.75F, maximumHeight = 1.5F;
    float bendFactor = 0, boundsRadius = 1;
    std::string asset;
};

/** @brief Pcg SetTerrainTrees operation branch. Add and Replace both append after upstream clearing policy. */
enum class TerrainTreeOperationMode : std::uint8_t { Add, Replace, Remove };

/** @brief Borrowed terrain-local tree owner, height raster and world geometry for one synchronous transaction. */
struct TerrainTreeTile {
    std::string name;
    PointSet* trees = nullptr;
    const Heightmap* heights = nullptr;
    double originX = 0, originZ = 0, width = 1, depth = 1;
    int treeResolutionX = 0, treeResolutionY = 0;
    bool worldMap = false;
};

/**
 * @brief Apply one Pcg tree distribution window across selected terrain tiles atomically.
 * @param tiles Borrowed distinct PointSet owners and finite height rasters in deterministic traversal order.
 * @param operationFitness Borrowed normalized raster matching the calculated Tree-domain operation window.
 * @param settings Prototype, spacing, density, scale and explicit RNG/identity settings; tile geometry is overridden.
 * @param operationSettings Finite positive world operation bounds.
 * @param mode Add/Replace append existing points as Pcg SetTerrainTrees does; Remove filters matching assets.
 * @param worldMapOperation Select ordinary or world-map tiles.
 * @param validTerrainNames Optional exact allow-list.
 * @return Mapping report with emitted or removed point total in changedSamples, or a structured diagnostic.
 * @ownership Retains no pointer or raster. Caller exclusively owns every output on one thread for the call.
 */
[[nodiscard]] Result<TerrainMultiTileReport> applyTerrainTreesMultiTile(
    const std::vector<TerrainTreeTile>& tiles, const Heightmap& operationFitness,
    const TerrainTreePlacementSettings& settings, const TerrainStampSettings& operationSettings,
    TerrainTreeOperationMode mode, bool worldMapOperation = false,
    const std::vector<std::string>& validTerrainNames = {});

/**
 * @brief Owning script-safe multi-terrain tree transaction workspace.
 * Tree sets and height rasters are copied on insertion. Each operation and all-tile history snapshot is evaluated in
 * a private candidate and published by one owner-thread swap. No external pointer, callback or hidden RNG survives.
 */
class TerrainMultiTreeWorkspace {
public:
    TerrainMultiTreeWorkspace();
    ~TerrainMultiTreeWorkspace();
    TerrainMultiTreeWorkspace(TerrainMultiTreeWorkspace&&) noexcept;
    TerrainMultiTreeWorkspace& operator=(TerrainMultiTreeWorkspace&&) noexcept;
    TerrainMultiTreeWorkspace(const TerrainMultiTreeWorkspace&) = delete;
    TerrainMultiTreeWorkspace& operator=(const TerrainMultiTreeWorkspace&) = delete;
    /** @brief Copy one uniquely named tree owner and height raster into the workspace. */
    [[nodiscard]] Result<int> addTile(const std::string& name, const PointSet& trees, const Heightmap& heights,
                                      double originX, double originZ, double width, double depth,
                                      int treeResolutionX, int treeResolutionY, bool worldMap = false);
    /** @brief Apply one shared distribution window and append or remove trees atomically. */
    [[nodiscard]] Result<int> apply(const Heightmap& operationFitness,
                                    const TerrainTreePlacementSettings& settings,
                                    const TerrainStampSettings& operationSettings, TerrainTreeOperationMode mode,
                                    bool worldMapOperation = false);
    /** @brief Copy one named current PointSet into caller-owned output. */
    [[nodiscard]] Result<int> copyTile(const std::string& name, PointSet& output) const;
    /** @brief Restore the prior all-tile tree snapshot. */
    [[nodiscard]] Result<int> undo();
    /** @brief Restore the next all-tile tree snapshot. */
    [[nodiscard]] Result<int> redo();
    /** @brief Number of owned tiles. */
    int getTileCount() const noexcept;
    /** @brief Emitted or removed points from the current snapshot's operation. */
    int getLastChangedSamples() const noexcept;
    /** @brief Affected tiles from the current snapshot's operation. */
    int getLastAffectedTiles() const noexcept;
    /** @brief Successful operation count including redo suffix. */
    int getOperationCount() const noexcept;
    /** @brief Applied operation count at the current history cursor. */
    int getAppliedCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Scan a terrain rectangle and atomically export accepted Pcg-style tree instances.
 * @param output Exclusively borrowed destination replaced only after every candidate and attribute succeeds.
 * @param fitness Finite normalized placement fitness sampled over the complete terrain rectangle.
 * @param heights Finite normalized terrain heights sampled independently from the fitness resolution.
 * @param settings Positive finite domain, spacing, density, dimensions and bounds; normalized probabilities/colors;
 * valid enum values; ordered offset/scale ranges; nonzero namespace; nonempty resource; nonnegative budget.
 * @return Emitted point count or a structured diagnostic. Candidate traversal and conditional XorshiftPlus draws match
 * Pcg's tree path: failure, X/Z jitter, fitness feather, optional offset/scale randomization, then yaw. Point IDs derive
 * from namespace and scan-cell identity rather than random draw position. Each point owns world position, independent
 * width/height scale, yaw, fitness color and asset/treeCandidate/bendFactor metadata. Caller serializes access on the
 * owner thread; the operation retains no references, invokes no callbacks, reads no hidden time and publishes nothing
 * on failure.
 */
[[nodiscard]] Result<int> exportTerrainTreePoints(PointSet& output, const Heightmap& fitness,
                                                  const Heightmap& heights,
                                                  const TerrainTreePlacementSettings& settings);

/**
 * @brief Remove matching tree points wherever the current fitness is strictly above the threshold.
 * @param output Exclusively borrowed destination replaced atomically; it may alias input.
 * @param input Immutable tree/mixed point snapshot whose attributes and ordering are preserved for retained rows.
 * @param fitness Finite normalized removal mask spanning settings' terrain rectangle.
 * @param settings Valid placement settings; only domain, minimumFitness and asset select removals.
 * @return Removed count or a structured diagnostic. Points outside the rectangle and other assets remain untouched.
 * Caller serializes access on the owner thread; no references or callbacks survive the call.
 */
[[nodiscard]] Result<int> removeTerrainTreePoints(PointSet& output, const PointSet& input, const Heightmap& fitness,
                                                  const TerrainTreePlacementSettings& settings);

/**
 * @brief Atomically refresh scale, bounds and bend metadata for one tree asset while preserving identity/transform.
 * @param output Exclusively borrowed destination replaced only on success; it may alias input.
 * @param input Immutable mixed PointSet snapshot.
 * @param settings Previous scale interval and new prototype values. Non-fixed refresh uses Pcg's clamped inverse-lerp
 * mapping independently for width and height; a collapsed previous interval maps to the new minimum.
 * @return Number of matching rows, or a structured validation/attribute diagnostic. Position, yaw, color, stable ID,
 * row order and unrelated attributes are retained. Owner-thread only; no callbacks or retained references.
 */
[[nodiscard]] Result<int> rescaleTerrainTreePoints(PointSet& output, const PointSet& input,
                                                   const TerrainTreeRescaleSettings& settings);
}  // namespace eve::procgen
