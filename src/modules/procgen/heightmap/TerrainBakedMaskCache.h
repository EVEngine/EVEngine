#pragma once

#include <memory>
#include <string>
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;

/**
 * @brief Own Pcg-style per-terrain baked scalar masks keyed by stable terrain and mask identities.
 * Entries contain copied CPU rasters. Scene objects, providers, GPU resources, callbacks, clocks and RNG are not retained.
 */
class TerrainBakedMaskCache {
public:
    TerrainBakedMaskCache();
    ~TerrainBakedMaskCache();
    TerrainBakedMaskCache(TerrainBakedMaskCache&&) noexcept;
    TerrainBakedMaskCache& operator=(TerrainBakedMaskCache&&) noexcept;
    TerrainBakedMaskCache(const TerrainBakedMaskCache&) = delete;
    TerrainBakedMaskCache& operator=(const TerrainBakedMaskCache&) = delete;

    /** @brief Copy or atomically replace one finite baked raster and mark it fresh. */
    [[nodiscard]] Result<int> store(const std::string& terrainId, const std::string& maskGuid,
                                    const Heightmap& raster);
    /** @brief Mark every matching GUID entry stale; stale reads fail until a provider stores a replacement. */
    [[nodiscard]] Result<int> markDirty(const std::string& maskGuid);
    /** @brief Remove one exact entry. */
    [[nodiscard]] Result<int> erase(const std::string& terrainId, const std::string& maskGuid);
    /** @brief Remove all entries. */
    void clear();
    /** @brief Return the number of retained entries, including dirty entries. */
    [[nodiscard]] int getEntryCount() const noexcept;
    /**
     * @brief Copy one fresh entry into any positive output resolution using Clamp/Bilinear sampling.
     * @return Changed output samples, or NotFound for missing/dirty data; failure never modifies output.
     * @throws std::bad_alloc Output remains unchanged.
     * @thread Synchronous owner-thread mutation; output is exclusively borrowed and no references escape.
     */
    [[nodiscard]] Result<int> copyMask(const std::string& terrainId, const std::string& maskGuid,
                                       Heightmap& output) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen
