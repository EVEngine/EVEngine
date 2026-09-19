#pragma once
#include "common/Export.h"


/** @file HexFeatures.h @brief Deterministic feature placement for hex decorations and walls. */

#include "hexmap/HexMap.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"

#include <cstdint>

namespace eve::hexmap {

/** @brief Five-component pseudo-random value used to pick and orient one feature. */
struct HexHash {
    float a = 0.f;
    float b = 0.f;
    float c = 0.f;
    float d = 0.f;
    float e = 0.f;
};

/**
 * @brief Deterministic replacement for the reference project's hash grid.
 *
 * The reference samples an array of `HexHash` cells indexed by
 * `worldXZ * hashGridScale`, seeded once from `Random`. This implementation
 * derives the same five values from a pure integer hash of the grid cell and the
 * seed, so a map's decoration layout is reproducible from the map seed alone and
 * no table has to be allocated or kept alive.
 *
 * @note Immutable after construction; safe to share between chunk builds.
 */
class EVENGINE_API_WORLD HexHashGrid {
public:
    /** @brief Spacing of the hash cells, matching the reference `hashGridScale`. */
    static constexpr float kScale = 0.25f;
    /** @brief Size of the reference grid; only used to reproduce its wrap. */
    static constexpr std::int32_t kSize = 256;

    /**
     * @brief Creates a hash field for `seed`.
     * @param seed Deterministic seed; the same seed always yields the same field.
     */
    explicit HexHashGrid(std::uint32_t seed = 0u) noexcept : seed_(seed) {}

    /** @brief Samples the five-component hash at a world-space position. */
    [[nodiscard]] HexHash sample(HexVec3 position) const noexcept;

private:
    std::uint32_t seed_ = 0u;
};

/**
 * @brief Feature-placement thresholds of the reference project.
 *
 * A feature level (1-3) picks a collection by comparing the cell's hash against
 * these thresholds; the last entry of each row is the catch-all.
 */
[[nodiscard]] EVENGINE_API_WORLD float featureThreshold(std::int32_t level, std::int32_t index) noexcept;

/** @brief Number of threshold entries per feature level. */
inline constexpr std::int32_t kFeatureThresholdCount = 3;

/** @brief Height of a city/farm wall, matching `HexMetrics::kWallHeight`. */
inline constexpr float kWallTowerThreshold = 0.5f;
/** @brief Edge length the reference bridge model is designed for. */
inline constexpr float kBridgeDesignLength = 7.f;

/**
 * @brief Builds the wall and bridge geometry of one chunk.
 *
 * Walls appear along the boundary between a walled and an unwalled cell when
 * neither side is underwater and the two do not differ by a cliff. A bridge is
 * emitted where a road crosses a river on the chunk's own edges.
 *
 * The single texture coordinate encodes the part so one shader can shade them
 * differently; the complete code table, shared with `buildFeatureMesh`, is
 * `0` wall, `1` tower, `2` bridge, `3` urban, `4` farm, `5` plant, `6` special.
 * The second coordinate is reserved.
 *
 * @param map Source map; must contain `chunkIndex`.
 * @param chunkIndex Chunk to build.
 * @param out Destination mesh; cleared and finalized by this call.
 */
EVENGINE_API_WORLD void buildWallMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out);

/**
 * @brief Builds the urban, farm, plant and special decorations of one chunk.
 *
 * Every cell emits at most one decoration, chosen exactly like the reference
 * project: the urban, farm and plant collections are each tested against the
 * cell's hash and the one with the lowest passing hash wins. Special features
 * suppress ordinary decorations entirely.
 *
 * The single texture coordinate encodes the collection (`3` urban, `4` farm,
 * `5` plant, `6` special); the second carries the hash's orientation selector so
 * a shader could rotate the decoration.
 *
 * @param map Source map; must contain `chunkIndex`.
 * @param chunkIndex Chunk to build.
 * @param out Destination mesh; cleared and finalized by this call.
 */
EVENGINE_API_WORLD void buildFeatureMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out);

}  // namespace eve::hexmap
