#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
class PointSet;
struct TerrainStampSettings;
struct TerrainMultiTileReport;

/** @brief Pcg-compatible scale policy for one terrain object instance resource. */
enum class TerrainObjectScaleMode : std::uint8_t { Fixed, Random, Fitness, FitnessRandomized };
/** @brief Height reference used by one terrain object instance resource. */
enum class TerrainObjectYOffsetMode : std::uint8_t { TerrainHeight, SeaLevel, Custom };
/** @brief Mutation performed by a multi-terrain game-object operation. */
enum class TerrainObjectOperationMode : std::uint8_t { Add, Replace, Remove };

/** @brief One resource entry emitted around each accepted Pcg game-object prototype location. */
struct TerrainObjectInstanceSettings {
    std::string asset;
    int minimumInstances = 1, maximumInstances = 1;
    float failureRate = 0;
    float minimumOffsetX = 0, maximumOffsetX = 0;
    float minimumOffsetY = 0, maximumOffsetY = 0;
    float minimumOffsetZ = 0, maximumOffsetZ = 0;
    TerrainObjectYOffsetMode yOffsetMode = TerrainObjectYOffsetMode::TerrainHeight;
    float customOffset = 0;
    TerrainObjectScaleMode scaleMode = TerrainObjectScaleMode::Fixed;
    bool commonScale = true;
    float minimumScale = 1, maximumScale = 1;
    float minimumScaleX = 1, maximumScaleX = 1;
    float minimumScaleY = 1, maximumScaleY = 1;
    float minimumScaleZ = 1, maximumScaleZ = 1;
    float scaleRandomPercentage = 0;
    float scaleRandomPercentageX = 0, scaleRandomPercentageY = 0, scaleRandomPercentageZ = 0;
    float minimumRotationX = 0, maximumRotationX = 0;
    float minimumRotationY = 0, maximumRotationY = 0;
    float minimumRotationZ = 0, maximumRotationZ = 0;
    bool yOffsetAlongSlope = false;
    bool alignForwardToSlope = false;
    bool rotateToSlope = false;
};

/** @brief Deterministic Pcg SetTerrainGameObjects scan, collision and resource configuration. */
struct TerrainObjectPlacementSettings {
    float originX = 0, originZ = 0, width = 1, depth = 1, heightScale = 1;
    float spacing = 10, spawnDensity = 1, jitterPercent = 0.5F;
    float startOffsetX = 0, startOffsetZ = 0;
    float failureRate = 0, minimumFitness = 0.5F, minimumInstanceFitness = 0.5F;
    float minimumDirection = 0, maximumDirection = 360;
    float boundsRadius = 1, boundsCheckQuality = 100, prototypeScale = 1;
    bool boundsCollisionCheck = false;
    float seaLevel = 0;
    std::int32_t seed = 1;
    std::uint64_t namespaceId = 1;
    int maxPoints = 1000000;
    std::string prototype;
    std::vector<TerrainObjectInstanceSettings> instances;
    /** @brief Append one validated copied resource entry and return the new resource count. */
    [[nodiscard]] Result<int> addInstance(const TerrainObjectInstanceSettings& instance);
};

/**
 * @brief Export Pcg-style terrain game-object resources into an attributed PointSet atomically.
 * @param output Exclusively borrowed destination replaced only after the complete candidate succeeds.
 * @param fitness Finite normalized distribution raster spanning the terrain rectangle.
 * @param heights Finite normalized terrain-height raster; its resolution is independent from fitness.
 * @param settings Explicit prototype, child resources, world domain, seed, collision and transform rules.
 * @return Emitted child instance count or structured InvalidArgument diagnostic.
 * @ownership Retains no input, callback, asset or scene object. Caller serializes access on its owner thread.
 * Candidate traversal uses one Pcg XorshiftPlus stream and no time source. Stable identities derive from namespace,
 * candidate cell, resource index and child ordinal. Asset resolution and scene ownership remain downstream.
 */
[[nodiscard]] Result<int> exportTerrainObjectPoints(PointSet& output, const Heightmap& fitness,
                                                    const Heightmap& heights,
                                                    const TerrainObjectPlacementSettings& settings);

/**
 * @brief Remove matching prototype points wherever fitness is strictly above removalStrength.
 * @return Removed count; output and all attributes are published atomically on success.
 */
[[nodiscard]] Result<int> removeTerrainObjectPoints(PointSet& output, const PointSet& input,
                                                    const Heightmap& fitness,
                                                    const TerrainObjectPlacementSettings& settings,
                                                    float removalStrength);

/** @brief One exclusively borrowed terrain object owner and its immutable height source. */
struct TerrainObjectTile {
    std::string name;
    PointSet* objects = nullptr;
    const Heightmap* heights = nullptr;
    double originX = 0, originZ = 0, width = 1, depth = 1;
    int objectResolutionX = 0, objectResolutionY = 0;
    bool worldMap = false;
};

/**
 * @brief Apply one Pcg-style game-object rule over mapped terrain tiles atomically.
 * @return Mapping report whose changedSamples counts emitted or removed child objects.
 * @ownership PointSet owners must be distinct and remain valid for the call. No pointer is retained.
 * One injected seed drives a single RNG stream in mapping order; accepted centers collide across tile seams.
 */
[[nodiscard]] Result<TerrainMultiTileReport> applyTerrainObjectsMultiTile(
    const std::vector<TerrainObjectTile>& tiles, const Heightmap& operationFitness,
    const TerrainObjectPlacementSettings& settings, const TerrainStampSettings& operationSettings,
    TerrainObjectOperationMode mode, bool worldMapOperation,
    const std::vector<std::string>& validTerrainNames = {});

/** @brief Owning transactional multi-terrain object workspace with undo and redo snapshots. */
class TerrainMultiObjectWorkspace {
public:
    TerrainMultiObjectWorkspace();
    ~TerrainMultiObjectWorkspace();
    TerrainMultiObjectWorkspace(TerrainMultiObjectWorkspace&&) noexcept;
    TerrainMultiObjectWorkspace& operator=(TerrainMultiObjectWorkspace&&) noexcept;
    TerrainMultiObjectWorkspace(const TerrainMultiObjectWorkspace&) = delete;
    TerrainMultiObjectWorkspace& operator=(const TerrainMultiObjectWorkspace&) = delete;
    /** @brief Deep-copy one named tile before the first operation. */
    [[nodiscard]] Result<int> addTile(const std::string& name, const PointSet& objects, const Heightmap& heights,
                                     double originX, double originZ, double width, double depth,
                                     int resolutionX, int resolutionY, bool worldMap = false);
    /** @brief Apply one operation as a single atomic history entry. */
    [[nodiscard]] Result<int> apply(const Heightmap& fitness, const TerrainObjectPlacementSettings& settings,
                                   const TerrainStampSettings& operationSettings, TerrainObjectOperationMode mode,
                                   bool worldMapOperation = false);
    /** @brief Copy one current tile result into output. */
    [[nodiscard]] Result<int> copyTile(const std::string& name, PointSet& output) const;
    /** @brief Restore the preceding snapshot. */
    [[nodiscard]] Result<int> undo();
    /** @brief Restore the next snapshot. */
    [[nodiscard]] Result<int> redo();
    /** @brief Return registered tile count. */
    [[nodiscard]] int getTileCount() const noexcept;
    /** @brief Return changed object count from the current snapshot. */
    [[nodiscard]] int getLastChangedSamples() const noexcept;
    /** @brief Return affected tile count from the current snapshot. */
    [[nodiscard]] int getLastAffectedTiles() const noexcept;
    /** @brief Return retained operation history count. */
    [[nodiscard]] int getOperationCount() const noexcept;
    /** @brief Return currently applied operation count. */
    [[nodiscard]] int getAppliedCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen
