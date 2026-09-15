#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
class TerrainWorldWorkspace;
struct TerrainDetailTile;
struct TerrainMultiTileReport;
struct TerrainStampSettings;
/** @brief Native detail-density operation corresponding to Pcg SpawnMode. */
enum class TerrainDetailMode { Replace, Add, Remove };
/** @brief Effective detail rule settings; density is rule density times global spawn density. */
struct TerrainDetailSettings {
    float             minimumFitness = 0.5F;
    float             fadeStart      = 0.6F;
    float             density        = 16;
    /** @brief Stable resource namespace; zero addresses the compatibility/default layer. */
    std::uint64_t     namespaceId    = 0;
    TerrainDetailMode mode           = TerrainDetailMode::Replace;
};
/**
 * @brief Owned integer terrain-detail layer, independent of graphics and resource registries.
 * Each instance is an independent owning value; explicit copies deep-copy all counts for transaction snapshots.
 * Caller serializes access on its owning thread. Every operation evaluates privately and
 * publishes once; no callback, borrowed raster or mutable view survives a call. Each apply
 * owns a named detail-thinning RNG stream initialized by its explicit signed seed. Replay
 * uses same-build float arithmetic, X-major iteration and conditional source RNG draws.
 * This layer covers one complete aligned terrain and owns independent density rasters keyed by stable resource
 * namespace. Multi-terrain windows and renderer instancing remain in their existing domain owners.
 */
class TerrainDetailLayer {
public:
    /** @brief Construct an empty layer. */
    TerrainDetailLayer();
    /** @brief Release owned counts. */
    ~TerrainDetailLayer();
    /** @brief Transfer ownership; moved-from layer is empty and resettable. */
    TerrainDetailLayer(TerrainDetailLayer&&) noexcept;
    /** @brief Transfer ownership, releasing previous counts. */
    TerrainDetailLayer& operator=(TerrainDetailLayer&&) noexcept;
    /** @brief Create an independent deep value copy for transactional snapshots. */
    TerrainDetailLayer(const TerrainDetailLayer&);
    /** @brief Replace with an independent deep value copy. */
    TerrainDetailLayer& operator=(const TerrainDetailLayer&);
    /** @brief Reset positive dimensions and uniform nonnegative count atomically.
     * @return Initialized cell count or InvalidArgument; dimension product must fit int. */
    [[nodiscard]] Result<int> reset(int width, int height, int count = 0);
    /** @brief Apply a finite aligned fitness map with source thinning and nearest-even integer rounding.
     * @param fitness Borrowed matching finite raster; scalar values may exceed [0,1].
     * @param settings Finite unit thresholds and nonnegative density below INT_MAX; mode must be known.
     * @param seed Explicit detail-thinning stream seed; zero is normalized to one, negative uses uint32 bits.
     * @return Changed cell count or InvalidArgument; failure/allocation exception preserves every count.
     * Replace clears the selected resource layer before generation. Add/Remove preserve rejected cells but clamp
     * accepted cells to the target density, even if the previous count was already above that density.
     */
    [[nodiscard]] Result<int> apply(const Heightmap& fitness, const TerrainDetailSettings& settings, int32_t seed);
    /** @brief Read an integer value copy; empty state or invalid coordinates return InvalidArgument. */
    [[nodiscard]] Result<int> sample(int x, int z) const;
    /** @brief Read one resource layer without exposing mutable storage; an absent namespace reads as zero. */
    [[nodiscard]] Result<int> sampleResource(std::uint64_t namespaceId, int x, int z) const;
    /** @brief Remove one resource layer atomically and return its changed cell count. */
    [[nodiscard]] Result<int> clearResource(std::uint64_t namespaceId);
    /** @brief Return width, zero when empty or moved-from. */
    int getWidth() const noexcept;
    /** @brief Return height, zero when empty or moved-from. */
    int getHeight() const noexcept;

private:
    friend class TerrainWorldWorkspace;
    friend class TerrainMultiDetailWorkspace;
    friend Result<TerrainMultiTileReport> applyTerrainDetailMultiTile(
        const std::vector<TerrainDetailTile>&, const Heightmap&, const TerrainDetailSettings&,
        const TerrainStampSettings&, std::int32_t, bool, const std::vector<std::string>&);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen
