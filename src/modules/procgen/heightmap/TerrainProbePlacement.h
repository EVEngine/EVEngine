#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "common/Result.h"
#include "procgen/heightmap/TerrainMultiTile.h"
namespace eve::procgen {
class Heightmap;
class PointSet;
struct TerrainStampSettings;
/** @brief Pcg probe resource kind. */
enum class TerrainProbeType : std::uint8_t { Reflection, Light };
/** @brief Mutation performed by a multi-terrain probe rule. */
enum class TerrainProbeOperationMode : std::uint8_t { Add, Replace, Remove };
/** @brief Deterministic value settings for Pcg SetProbes terrain scanning. */
struct TerrainProbePlacementSettings {
    std::string name;
    TerrainProbeType type = TerrainProbeType::Reflection;
    float originX = 0, originZ = 0, width = 1, depth = 1, heightScale = 1;
    float spacing = 10, jitterPercent = 0.5F, minimumFitness = 0.5F;
    bool seaLevelActive = false;
    float seaLevel = 0, reflectionOffset = 1, lightOffset = 2.5F;
    int reflectionResolution = 128;
    float reflectionClipDistance = 1000, reflectionShadowDistance = 80;
    std::int32_t seed = 1;
    std::uint64_t namespaceId = 1;
    int maxPoints = 1000000;
};
/**
 * @brief Export Pcg ReflectionProbe or LightProbe placements into an owned attributed PointSet.
 * @param output Exclusively borrowed destination, atomically replaced after the full scan succeeds.
 * @param fitness Finite normalized fitness raster over the configured rectangle.
 * @param heights Finite normalized terrain heights, independently resolved.
 * @param settings Explicit deterministic scan, height, resource and reflection settings.
 * @return Emitted probe count or InvalidArgument; failure leaves output unchanged.
 * @throws std::bad_alloc Output remains unchanged.
 * @thread Synchronous exclusive output access; no scene object, callback, clock or RNG state is retained.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> exportTerrainProbePoints(PointSet& output, const Heightmap& fitness,
                                                                        const Heightmap&                     heights,
                                                                        const TerrainProbePlacementSettings& settings);

/** @brief One exclusively borrowed terrain probe owner and immutable height source. */
struct TerrainProbeTile {
    std::string name;
    PointSet* probes = nullptr;
    const Heightmap* heights = nullptr;
    double originX = 0, originZ = 0, width = 1, depth = 1;
    int resolutionX = 0, resolutionY = 0;
    bool worldMap = false;
};

/**
 * @brief Apply one Pcg probe rule over mapped terrain tiles as one atomic transaction.
 * @return Mapping report whose changedSamples counts emitted or removed probes.
 * @ownership Tile outputs must be distinct. No pointer, raster, callback or RNG state is retained.
 * One seed drives a single stream in mapping order; Replace/Remove match the stable probe resource name.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<TerrainMultiTileReport> applyTerrainProbesMultiTile(
    const std::vector<TerrainProbeTile>& tiles, const Heightmap& operationFitness,
    const TerrainProbePlacementSettings& settings, const TerrainStampSettings& operationSettings,
    TerrainProbeOperationMode mode, bool worldMapOperation = false,
    const std::vector<std::string>& validTerrainNames = {});
}  // namespace eve::procgen
