#pragma once

/** @file EvpackTerrainMaterialLoader.h @brief Runtime canonical terrain-layer semantics. */

#include "asset/EvpackResourceReader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::asset_procgen {

/** @brief One owning canonical terrain layer independent of its source engine. */
struct RuntimeTerrainLayer {
    std::string name;
    std::string diffuseSource;
    std::string normalSource;
    std::string weightSource;
    std::string normalConvention;
    float       tileSizeMeters = 1.f;
};

/** @brief Admission budgets for terrain material definitions. */
struct TerrainMaterialLoadLimits {
    std::uint32_t maximumLayers = 4096;
    std::uint32_t maximumStringBytes = 1024 * 1024;
    std::uint64_t maximumDecodedBytes = 64ull * 1024ull * 1024ull;
};

/** @brief Owning capability-selected terrain material candidate. */
struct LoadedTerrainMaterial {
    AssetRef                      asset;
    std::vector<RuntimeTerrainLayer> layers;
    asset::EvpackVariantSelection variant;
};

/** @brief Strict `eve.terrain-material/1` EVDEF loader. */
class EvpackTerrainMaterialLoader {
public:
    /** @brief Bind a borrowed immutable reader that must outlive this loader. */
    explicit EvpackTerrainMaterialLoader(const asset::EvpackResourceReader& reader) noexcept
        : reader_(reader) {}

    /**
     * @brief Decode and validate canonical layer, normal and tiling semantics transactionally.
     * @return Owning layer candidate; failure publishes no partially decoded material.
     * @thread Worker-safe when the bound reader is read concurrently.
     */
    [[nodiscard]] Result<LoadedTerrainMaterial> load(
        const AssetRef& material, const asset::EvpackCapabilities& capabilities,
        const TerrainMaterialLoadLimits& limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
};

}  // namespace eve::asset_procgen
