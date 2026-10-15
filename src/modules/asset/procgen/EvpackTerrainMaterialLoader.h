#pragma once
#include "common/Export.h"

/** @file EvpackTerrainMaterialLoader.h @brief Runtime canonical terrain-layer semantics. */

#include "asset/EvpackResourceReader.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::asset_procgen {

/** @brief One owning canonical terrain layer independent of its source engine. */
struct RuntimeTerrainLayer {
    std::string             name;
    std::string             diffuseSource;
    std::string             normalSource;
    std::string             weightSource;
    std::string             maskSource;
    std::optional<AssetRef> diffuseAsset, normalAsset, weightAsset, maskAsset;
    std::string             normalConvention;
    float                   tileSizeMeters = 1.f;
    std::array<float, 2>    tileScaleMeters{1.f, 1.f};
    std::array<float, 2>    tileOffsetMeters{};
    std::array<float, 4>    maskRemapMinimum{};
    std::array<float, 4>    maskRemapMaximum{1.f, 1.f, 1.f, 1.f};
    std::array<float, 4>    specular{};
    float                   metallic    = 0.f;
    float                   normalScale = 1.f;
    float                   smoothness  = 0.f;
};

/** @brief Admission budgets for terrain material definitions. */
struct TerrainMaterialLoadLimits {
    std::uint32_t maximumLayers       = 4096;
    std::uint32_t maximumStringBytes  = 1024 * 1024;
    std::uint64_t maximumDecodedBytes = 64ull * 1024ull * 1024ull;
};

/** @brief Owning capability-selected terrain material candidate. */
struct LoadedTerrainMaterial {
    AssetRef                               asset;
    std::vector<RuntimeTerrainLayer>       layers;
    std::string                            holesSource;
    std::optional<AssetRef>                holesAsset;
    std::array<std::string, 4>             controlSources;
    std::array<std::optional<AssetRef>, 4> controlAssets;
    float                                  boundsMultiplier = 1.f;
    asset::EvpackVariantSelection          variant;
};

/** @brief Strict `eve.terrain-material/3` EVDEF loader with one-version compatibility. */
class EVENGINE_API_ORCHESTRATION EvpackTerrainMaterialLoader {
public:
    /** @brief Bind a borrowed immutable reader that must outlive this loader. */
    explicit EvpackTerrainMaterialLoader(const asset::EvpackResourceReader& reader) noexcept : reader_(reader) {}

    /**
     * @brief Decode and validate canonical layer, normal and tiling semantics transactionally.
     * @return Owning layer candidate; failure publishes no partially decoded material.
     * @thread Worker-safe when the bound reader is read concurrently.
     */
    [[nodiscard]] Result<LoadedTerrainMaterial> load(const AssetRef&                  material,
                                                     const asset::EvpackCapabilities& capabilities,
                                                     const TerrainMaterialLoadLimits& limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
};

}  // namespace eve::asset_procgen
