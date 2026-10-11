#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
class TerrainDetailLayer;
struct TerrainStampSettings;
struct TerrainDetailSettings;

/** @brief Pcg terrain-height seam controls; maxDifference is retained for source-profile compatibility. */
struct TerrainHeightStitchSettings {
    int extraSeamSize = 1;
    float maxDifference = 1;
};

/** @brief Borrowed mutable height tile participating in one synchronous world operation. */
struct TerrainHeightTile {
    std::string name;
    Heightmap*  heightmap = nullptr;
    double      originX = 0, originZ = 0, width = 1, depth = 1;
    bool        worldMap = false;
};

/** @brief Pcg multi-terrain pixel domain; only heightmaps share the border sample between adjacent tiles. */
enum class TerrainOperationDomain { Heightmap, Texture, Tree, TerrainDetail, GameObject, BakedMask };

/** @brief Immutable tile geometry and raster resolution used to calculate a shared operation window. */
struct TerrainOperationTile {
    std::string name;
    double originX = 0, originZ = 0, width = 1, depth = 1;
    int resolutionX = 0, resolutionY = 0;
    bool worldMap = false;
};

/** @brief Borrowed mutable detail layer participating in one synchronous world operation. */
struct TerrainDetailTile {
    std::string name;
    TerrainDetailLayer* layer = nullptr;
    double originX = 0, originZ = 0, width = 1, depth = 1;
    bool worldMap = false;
};

/** @brief Integer rectangles mapping one tile into the shared operation raster. */
struct TerrainAffectedPixels {
    std::string terrainName;
    int localX = 0, localY = 0, operationX = 0, operationY = 0, width = 0, height = 0;
};

/** @brief Completed multi-tile stamp statistics and Pcg-compatible affected-pixel mappings. */
struct TerrainMultiTileReport {
    int affectedTiles = 0;
    int changedSamples = 0;
    int operationX = 0, operationY = 0, operationWidth = 0, operationHeight = 0;
    std::vector<TerrainAffectedPixels> mappings;
};

/**
 * @brief Calculate Pcg-compatible local and shared pixel rectangles for any multi-terrain raster domain.
 * @param tiles Borrowed immutable descriptors with unique names, matching resolution/world pixel size and grid-aligned origins.
 * @param settings Finite positive world-space operation bounds; rotation expands the conservative affected rectangle.
 * @param domain Heightmap uses a one-pixel shared seam; every other domain advances by its full resolution.
 * @param worldMapOperation Select ordinary or world-map tiles.
 * @param validTerrainNames Optional exact allow-list; empty selects all intersecting tiles in the domain.
 * @return Deterministically ordered mappings and the shared operation rectangle, or InvalidArgument.
 * @ownership Retains no descriptor or name. Caller owns inputs for the synchronous call. No callbacks, RNG or hidden time.
 */
[[nodiscard]] Result<TerrainMultiTileReport> mapTerrainOperationMultiTile(
    const std::vector<TerrainOperationTile>& tiles, const TerrainStampSettings& settings,
    TerrainOperationDomain domain, bool worldMapOperation = false,
    const std::vector<std::string>& validTerrainNames = {});

/**
 * @brief Apply one shared Pcg detail-distribution raster to every selected tile atomically.
 * @param tiles Borrowed distinct initialized detail owners with matching dimensions and aligned world geometry.
 * @param operationFitness Borrowed finite raster matching the calculated shared operation rectangle.
 * @param settings Pcg detail mode, thresholds and target density.
 * @param operationSettings World-space operation rectangle used to derive per-tile mappings.
 * @param seed Explicit signed seed for one XorshiftPlus stream shared across tiles in descriptor order.
 * @param worldMapOperation Select ordinary or world-map tiles.
 * @param validTerrainNames Optional exact allow-list.
 * @return Affected mappings and changed-cell total, or InvalidArgument; all layers publish together.
 * @ownership Retains no pointer or raster. Caller exclusively owns every layer on one thread for the call.
 */
[[nodiscard]] Result<TerrainMultiTileReport> applyTerrainDetailMultiTile(
    const std::vector<TerrainDetailTile>& tiles, const Heightmap& operationFitness,
    const TerrainDetailSettings& settings, const TerrainStampSettings& operationSettings, std::int32_t seed,
    bool worldMapOperation = false, const std::vector<std::string>& validTerrainNames = {});

/** @brief Apply one world-space stamp to all selected intersecting height tiles atomically.
 * @param tiles Borrowed tile descriptors. Heightmaps must be distinct, finite, nonempty, share one
 * resolution and have positive finite extents. Tile origins must align to whole `(resolution-1)` spans.
 * @param stamp Borrowed finite stamp raster.
 * @param settings World-space stamp configuration. Per-tile origin/spacing are derived from descriptors.
 * @param localMask Borrowed finite local stamp mask.
 * @param globalMask Borrowed finite global stamp mask.
 * @param worldMapOperation Select ordinary or world-map tiles, matching Pcg's separate operation domain.
 * @param validTerrainNames Optional exact allow-list; empty selects every intersecting tile in the domain.
 * @return Report containing Pcg-compatible local/operation pixel rectangles, or InvalidArgument.
 * @throws std::bad_alloc; all target heightmaps remain unchanged.
 * @ownership Retains no descriptor, pointer, raster, callback or scene link. Caller exclusively owns every
 * target on one thread for the entire call. All candidates finish before any tile publishes. No RNG/time.
 */
[[nodiscard]] Result<TerrainMultiTileReport> applyTerrainStampMultiTile(
    const std::vector<TerrainHeightTile>& tiles, const Heightmap& stamp, const TerrainStampSettings& settings,
    const Heightmap& localMask, const Heightmap& globalMask, bool worldMapOperation = false,
    const std::vector<std::string>& validTerrainNames = {});

/**
 * @brief Stitch the overlapping shared edge of two grid-aligned height tiles with Pcg's seam interpolation.
 * @param terrainA First exclusively borrowed height tile.
 * @param terrainB Second exclusively borrowed height tile with distinct storage.
 * @param settings Seam radius in samples and the Pcg profile's currently inactive max-difference field.
 * @return Unique changed sample count across both tiles; neither publishes when validation fails.
 * @ownership No pointer or reference survives this synchronous caller-thread operation.
 */
[[nodiscard]] Result<int> stitchTerrainHeightmaps(const TerrainHeightTile& terrainA,
                                                  const TerrainHeightTile& terrainB,
                                                  const TerrainHeightStitchSettings& settings = {});

/** @brief Owning script-safe collection of height tiles with atomic multi-tile operations.
 * Tiles are copied on insertion and never retain caller rasters. The workspace is one mutable owner,
 * move-only, and caller-thread affine. It invokes no callbacks and exposes only copied outputs.
 */
class TerrainMultiTileWorkspace {
public:
    TerrainMultiTileWorkspace();
    ~TerrainMultiTileWorkspace();
    TerrainMultiTileWorkspace(TerrainMultiTileWorkspace&&) noexcept;
    TerrainMultiTileWorkspace& operator=(TerrainMultiTileWorkspace&&) noexcept;
    TerrainMultiTileWorkspace(const TerrainMultiTileWorkspace&) = delete;
    TerrainMultiTileWorkspace& operator=(const TerrainMultiTileWorkspace&) = delete;
    /** @brief Copy a uniquely named tile into the workspace atomically. */
    [[nodiscard]] Result<int> addTile(const std::string& name, const Heightmap& heightmap, double originX,
                                      double originZ, double width, double depth, bool worldMap = false);
    /** @brief Apply a stamp transaction to owned tiles; failure preserves tiles and the prior report. */
    [[nodiscard]] Result<int> stamp(const Heightmap& stamp, const TerrainStampSettings& settings,
                                    const Heightmap& localMask, const Heightmap& globalMask,
                                    bool worldMapOperation = false);
    /** @brief Stitch two named owned tiles and append one undoable all-tile snapshot. */
    [[nodiscard]] Result<int> stitch(const std::string& terrainA, const std::string& terrainB,
                                     const TerrainHeightStitchSettings& settings = {});
    /** @brief Copy a named current tile into a matching caller-owned output. */
    [[nodiscard]] Result<int> copyTile(const std::string& name, Heightmap& output) const;
    /** @brief Restore the previous all-tile snapshot atomically. */
    [[nodiscard]] Result<int> undo();
    /** @brief Restore the next all-tile snapshot atomically. */
    [[nodiscard]] Result<int> redo();
    /** @brief Number of owned tiles. */
    int getTileCount() const noexcept;
    /** @brief Affected tile count from the last successful operation, or zero before one. */
    int getLastAffectedTiles() const noexcept;
    /** @brief Changed sample count from the last successful operation, or zero before one. */
    int getLastChangedSamples() const noexcept;
    /** @brief Number of affected-pixel mappings from the last successful operation. */
    int getLastMappingCount() const noexcept;
    /** @brief Successful stamp count including any redo suffix. */
    int getOperationCount() const noexcept;
    /** @brief Applied successful stamp count at the current history cursor. */
    int getAppliedCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Owning script-safe multi-terrain detail transaction workspace.
 * Input layers are copied on insertion. Operations, reports and all-layer undo snapshots publish by one owner-thread
 * swap; no borrowed raster or callback survives a call. The explicit seed creates one deterministic stream per apply.
 */
class TerrainMultiDetailWorkspace {
public:
    TerrainMultiDetailWorkspace();
    ~TerrainMultiDetailWorkspace();
    TerrainMultiDetailWorkspace(TerrainMultiDetailWorkspace&&) noexcept;
    TerrainMultiDetailWorkspace& operator=(TerrainMultiDetailWorkspace&&) noexcept;
    TerrainMultiDetailWorkspace(const TerrainMultiDetailWorkspace&) = delete;
    TerrainMultiDetailWorkspace& operator=(const TerrainMultiDetailWorkspace&) = delete;
    /** @brief Copy a uniquely named initialized detail tile into the workspace atomically. */
    [[nodiscard]] Result<int> addTile(const std::string& name, const TerrainDetailLayer& layer, double originX,
                                      double originZ, double width, double depth, bool worldMap = false);
    /** @brief Apply one shared operation raster to all selected detail tiles atomically. */
    [[nodiscard]] Result<int> apply(const Heightmap& operationFitness, const TerrainDetailSettings& settings,
                                    const TerrainStampSettings& operationSettings, std::int32_t seed,
                                    bool worldMapOperation = false);
    /** @brief Copy one named current layer into caller-owned output. */
    [[nodiscard]] Result<int> copyTile(const std::string& name, TerrainDetailLayer& output) const;
    /** @brief Restore the prior all-layer snapshot. */
    [[nodiscard]] Result<int> undo();
    /** @brief Restore the next all-layer snapshot. */
    [[nodiscard]] Result<int> redo();
    /** @brief Number of owned detail tiles. */
    int getTileCount() const noexcept;
    /** @brief Changed cells from the last applied snapshot. */
    int getLastChangedSamples() const noexcept;
    /** @brief Affected tiles from the last applied snapshot. */
    int getLastAffectedTiles() const noexcept;
    /** @brief Successful apply count including redo suffix. */
    int getOperationCount() const noexcept;
    /** @brief Applied operation count at the current history cursor. */
    int getAppliedCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen
