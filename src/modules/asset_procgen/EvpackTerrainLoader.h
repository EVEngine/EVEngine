#pragma once

/** @file EvpackTerrainLoader.h @brief Runtime decoding of canonical terrain assets. */

#include "asset/EvpackResourceReader.h"
#include "procgen/SpatialData.h"
#include "procgen/heightmap/Heightmap.h"

namespace eve::asset_procgen {

/** @brief Explicit resource limits applied before terrain allocation. */
struct TerrainAssetLoadLimits {
    std::uint32_t maximumDimension = 32768;
    std::uint64_t maximumSamples = 268'435'456;
    std::uint64_t maximumDecodedBytes = 1024ull * 1024ull * 1024ull;
};

/** @brief Owning heightmap plus queryable procgen spatial representation. */
struct LoadedTerrain {
    AssetRef                       asset;
    procgen::Heightmap             heightmap;
    procgen::SpatialData           spatial;
    float                          spacingX = 1.f;
    float                          spacingZ = 1.f;
    asset::EvpackVariantSelection  variant;
};

/** @brief Capability-aware decoder from `eve.terrain/1` to procgen terrain data. */
class EvpackTerrainLoader {
public:
    /** @brief Bind a borrowed immutable reader that must outlive this loader. */
    explicit EvpackTerrainLoader(const asset::EvpackResourceReader& reader) noexcept
        : reader_(reader) {}

    /**
     * @brief Decode and validate one canonical terrain without mutating a world.
     * @return Owning Heightmap and SpatialData; failure publishes no partial result.
     * @thread Worker-safe when the bound reader is read concurrently.
     */
    [[nodiscard]] Result<LoadedTerrain> load(
        const AssetRef& terrain, const asset::EvpackCapabilities& capabilities,
        const TerrainAssetLoadLimits& limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
};

}  // namespace eve::asset_procgen
