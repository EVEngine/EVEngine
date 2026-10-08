#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
class PointSet;
class TerrainDetailLayer;
class TerrainBiomePreset;
class TerrainSplatmap;
class TerrainSpawnPlan;
struct TerrainDetailSettings;
struct TerrainObjectPlacementSettings;
struct TerrainProbePlacementSettings;
struct TerrainStampSettings;
struct TerrainTreePlacementSettings;
enum class TerrainDetailMode;
enum class TerrainObjectOperationMode : std::uint8_t;
enum class TerrainProbeOperationMode : std::uint8_t;
enum class TerrainTreeOperationMode : std::uint8_t;
/** @brief Observable lifecycle of a caller-driven staged world spawn. */
enum class TerrainWorldRunStatus { Idle, Pending, Completed, Cancelled, Failed };

/** @brief Validated Pcg-style centered terrain-grid topology. */
struct TerrainWorldCreationSettings {
    int tilesX = 1, tilesZ = 1;
    float tileSize = 1024, tileHeight = 1000;
    float centerX = 0, centerZ = 0;
    int heightmapResolution = 1025;
    int controlTextureResolution = 1024;
    int detailResolution = 1024;
    int treeResolution = 1024;
    int objectResolution = 1024;
    int splatLayers = 1, defaultSplatLayer = 0;
    int defaultDetailDensity = 0;
    bool worldMap = false;
    std::string namePrefix = "Terrain";
    std::string nameSuffix;
};

/** @brief Select Pcg spawn domains to clear in one atomic world operation. */
struct TerrainWorldClearSettings {
    bool details = true;
    bool trees = true;
    bool objects = true;
    bool probes = true;
    /** @brief Zero clears any source; non-zero clears only points emitted by that stable namespace. */
    std::uint64_t sourceNamespace = 0;
};

/**
 * @brief Own a complete Pcg-style terrain grid and its height, splat, detail, tree, object and probe state.
 * Creation and every operation publish by one owner swap. History snapshots contain every domain, so undo/redo
 * cannot expose a height/resource combination that never existed. Inputs are copied during calls and never retained.
 * The workspace is caller-thread affine, invokes no callback, reads no clock and uses only explicitly supplied seeds.
 */
class EVENGINE_API_DOMAINS TerrainWorldWorkspace {
public:
    struct Impl;
    TerrainWorldWorkspace();
    ~TerrainWorldWorkspace();
    TerrainWorldWorkspace(TerrainWorldWorkspace&&) noexcept;
    TerrainWorldWorkspace& operator=(TerrainWorldWorkspace&&) noexcept;
    TerrainWorldWorkspace(const TerrainWorldWorkspace&) = delete;
    TerrainWorldWorkspace& operator=(const TerrainWorldWorkspace&) = delete;
    /** @brief Atomically replace the world with a zero-height centered tile grid. */
    [[nodiscard]] Result<int> create(const TerrainWorldCreationSettings& settings);
    /** @brief Apply one height stamp and record a complete-world snapshot. */
    [[nodiscard]] Result<int> stamp(const Heightmap& stamp, const TerrainStampSettings& settings,
                                    const Heightmap& localMask, const Heightmap& globalMask);
    /** @brief Paint one splat layer and record a complete-world snapshot. */
    [[nodiscard]] Result<int> paintSplat(const Heightmap& paint, int targetLayer,
                                         const TerrainStampSettings& operation);
    /** @brief Apply one detail rule and record a complete-world snapshot. */
    [[nodiscard]] Result<int> applyDetail(const Heightmap& fitness, const TerrainDetailSettings& settings,
                                          const TerrainStampSettings& operation,
                                          TerrainDetailMode mode, std::int32_t seed);
    /** @brief Apply one tree rule and record a complete-world snapshot. */
    [[nodiscard]] Result<int> applyTrees(const Heightmap& fitness, const TerrainTreePlacementSettings& settings,
                                         const TerrainStampSettings& operation, TerrainTreeOperationMode mode);
    /** @brief Apply one game-object rule and record a complete-world snapshot. */
    [[nodiscard]] Result<int> applyObjects(const Heightmap& fitness,
                                           const TerrainObjectPlacementSettings& settings,
                                           const TerrainStampSettings& operation,
                                           TerrainObjectOperationMode mode);
    /** @brief Apply one reflection or light probe rule and record a complete-world snapshot. */
    [[nodiscard]] Result<int> applyProbes(const Heightmap& fitness,
                                          const TerrainProbePlacementSettings& settings,
                                          const TerrainStampSettings& operation,
                                          TerrainProbeOperationMode mode);
    /** @brief Execute all enabled rules from an owned spawn plan as one complete-world transaction. */
    [[nodiscard]] Result<int> spawn(const TerrainSpawnPlan& plan);
    /** @brief Compile and execute biome-active preset spawners as one complete-world transaction. */
    [[nodiscard]] Result<int> spawnBiome(const TerrainBiomePreset& preset);
    /** @brief Compile and execute stamper-active preset spawners as one complete-world transaction. */
    [[nodiscard]] Result<int> spawnStamper(const TerrainBiomePreset& preset);
    /** @brief Start an owned rule-by-rule spawn without exposing candidate world state. */
    [[nodiscard]] Result<TerrainWorldRunStatus> beginSpawn(const TerrainSpawnPlan& plan);
    /** @brief Start the biome-active preset stack as an owned staged spawn. */
    [[nodiscard]] Result<TerrainWorldRunStatus> beginSpawnBiome(const TerrainBiomePreset& preset);
    /** @brief Start the stamper-active preset stack as an owned staged spawn. */
    [[nodiscard]] Result<TerrainWorldRunStatus> beginSpawnStamper(const TerrainBiomePreset& preset);
    /** @brief Execute at most maxRules rules and publish the complete world once all rules finish. */
    [[nodiscard]] Result<TerrainWorldRunStatus> stepSpawn(int maxRules);
    /** @brief Cancel a pending spawn and discard its complete-world candidate. */
    [[nodiscard]] Result<TerrainWorldRunStatus> cancelSpawn();
    /** @brief Return the staged world-spawn lifecycle state. */
    [[nodiscard]] TerrainWorldRunStatus getSpawnStatus() const noexcept;
    /** @brief Return rules visited by the current or most recent staged spawn. */
    [[nodiscard]] int getSpawnCompletedRules() const noexcept;
    /** @brief Flatten every owned heightmap to zero and record one complete-world snapshot. */
    [[nodiscard]] Result<int> flatten();
    /**
     * @brief Set every owned height sample from a world-unit elevation using Pcg's Clamp01 conversion.
     * @param heightWorldUnits Requested world elevation.
     * @param worldHeightSpan Positive vertical size represented by normalized height 1.
     * @return Changed sample count; validation failure leaves the complete world and history unchanged.
     */
    [[nodiscard]] Result<int> setHeightWorldUnits(float heightWorldUnits, float worldHeightSpan);
    /** @brief Clear selected spawn domains on every tile and record one complete-world snapshot. */
    [[nodiscard]] Result<int> clearSpawns(const TerrainWorldClearSettings& settings);
    /** @brief Copy a tile heightmap by stable creation-order index. */
    [[nodiscard]] Result<int> copyHeightmap(int index, Heightmap& output) const;
    /** @brief Copy a tile splatmap by stable creation-order index. */
    [[nodiscard]] Result<int> copySplatmap(int index, TerrainSplatmap& output) const;
    /** @brief Copy a tile detail layer by stable creation-order index. */
    [[nodiscard]] Result<int> copyDetail(int index, TerrainDetailLayer& output) const;
    /** @brief Copy a tile tree PointSet by stable creation-order index. */
    [[nodiscard]] Result<int> copyTrees(int index, PointSet& output) const;
    /** @brief Copy a tile game-object PointSet by stable creation-order index. */
    [[nodiscard]] Result<int> copyObjects(int index, PointSet& output) const;
    /** @brief Copy a tile reflection/light probe PointSet by stable creation-order index. */
    [[nodiscard]] Result<int> copyProbes(int index, PointSet& output) const;
    /** @brief Return a tile's persistent name copy. */
    [[nodiscard]] Result<std::string> getTileName(int index) const;
    /** @brief Return a tile's world X origin. */
    [[nodiscard]] Result<double> getTileOriginX(int index) const;
    /** @brief Return a tile's world Z origin. */
    [[nodiscard]] Result<double> getTileOriginZ(int index) const;
    /** @brief Restore the previous complete-world snapshot. */
    [[nodiscard]] Result<int> undo();
    /** @brief Restore the next complete-world snapshot. */
    [[nodiscard]] Result<int> redo();
    /** @brief Return owned tile count. */
    [[nodiscard]] int getTileCount() const noexcept;
    /** @brief Return changed element count from the current snapshot. */
    [[nodiscard]] int getLastChangedSamples() const noexcept;
    /** @brief Return affected tile count from the current snapshot. */
    [[nodiscard]] int getLastAffectedTiles() const noexcept;
    /** @brief Return retained operation count. */
    [[nodiscard]] int getOperationCount() const noexcept;
    /** @brief Return applied operation count. */
    [[nodiscard]] int getAppliedCount() const noexcept;
private:
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen
