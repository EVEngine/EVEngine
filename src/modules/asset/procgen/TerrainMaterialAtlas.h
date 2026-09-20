#pragma once

/** @file TerrainMaterialAtlas.h @brief CPU assembly of grouped terrain material atlases. */

#include "asset/EvpackImageDecoder.h"
#include "asset/procgen/EvpackTerrainMaterialLoader.h"

namespace eve::graphics {
class IResourceFactory;
class Shader;
class Texture;
}  // namespace eve::graphics
namespace eve::asset_procgen {

/** @brief Owning base-level linear RGBA8 image ready for backend upload. */
struct TerrainAtlasImage {
    std::uint32_t             width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};

/** @brief Four-layer atlas group driven by one RGBA control image. */
struct TerrainMaterialAtlasGroup {
    std::uint32_t     firstLayer = 0, layerCount = 0, tileWidth = 0, tileHeight = 0;
    TerrainAtlasImage albedo, normal, mask, control;
};

/** @brief Complete terrain material image set; up to four groups cover sixteen layers. */
struct TerrainMaterialAtlases {
    std::vector<TerrainMaterialAtlasGroup> groups;
    TerrainAtlasImage                      holes;
};

/** @brief Single-draw terrain textures covering up to sixteen layers. */
struct PackedTerrainMaterialAtlases {
    TerrainAtlasImage albedo, normal, mask, controls, parameters;
    std::uint32_t     layerCount = 0;
    bool              hasHoles   = false;
};

/** @brief Bounds for deterministic terrain atlas construction. */
struct TerrainMaterialAtlasLimits {
    std::uint32_t                  maximumLayers        = 16;
    std::uint32_t                  maximumTileDimension = 4096;
    std::uint64_t                  maximumOutputBytes   = 1024ull * 1024ull * 1024ull;
    asset::EvpackImageDecodeLimits image{};
};

/** @brief Repack grouped CPU atlases and material parameters for one terrain draw. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<PackedTerrainMaterialAtlases> packTerrainMaterialAtlases(
    const TerrainMaterialAtlases& atlases, const LoadedTerrainMaterial& material,
    const TerrainMaterialAtlasLimits& limits = {});

/**
 * @brief Resolve canonical image references and assemble one 2x2 atlas per control map.
 * @param reader Borrowed immutable package reader valid for the call.
 * @param material Owning loaded terrain metadata; image references are observed only for the call.
 * @param capabilities Runtime capability selection used for every image.
 * @param limits Layer, dimension and allocation budgets.
 * @return Detached linear RGBA8 atlases. Missing layer images use documented neutral texels;
 *         group zero without a control map uses red, and missing holes use white.
 * @thread Worker-safe when reader is read concurrently; performs no GPU calls or callbacks.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<TerrainMaterialAtlases> buildTerrainMaterialAtlases(const asset::EvpackResourceReader& reader,
                                                                         const LoadedTerrainMaterial&       material,
                                                                         const asset::EvpackCapabilities&  capabilities,
                                                                         const TerrainMaterialAtlasLimits& limits = {});


/** @brief Four backend-owned textures for one uploaded terrain group. */
struct TerrainMaterialGpuGroup {
    graphics::Texture* albedo  = nullptr;
    graphics::Texture* normal  = nullptr;
    graphics::Texture* mask    = nullptr;
    graphics::Texture* control = nullptr;
};

/** @brief Backend-owned terrain textures released together on their graphics thread. */
struct TerrainMaterialGpuSet {
    std::vector<TerrainMaterialGpuGroup> groups;
    graphics::Texture*                   holes = nullptr;
};

/** @brief Backend-owned texture set for a single draw of up to sixteen layers. */
struct PackedTerrainMaterialGpuSet {
    graphics::Texture* albedo     = nullptr;
    graphics::Texture* normal     = nullptr;
    graphics::Texture* mask       = nullptr;
    graphics::Texture* controls   = nullptr;
    graphics::Texture* parameters = nullptr;
    std::uint32_t      layerCount = 0;
    bool               hasHoles   = false;
};

/** @brief Upload every atlas transactionally; partial failure releases earlier textures. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<TerrainMaterialGpuSet> uploadTerrainMaterialAtlases(graphics::IResourceFactory&   factory,
                                                                         const TerrainMaterialAtlases& atlases);

/** @brief Release every texture in a GPU set and clear it even if one backend release fails. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<void> releaseTerrainMaterialAtlases(graphics::IResourceFactory& factory,
                                                         TerrainMaterialGpuSet&      set);

/** @brief Upload a packed terrain set transactionally. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<PackedTerrainMaterialGpuSet> uploadPackedTerrainMaterialAtlases(
    graphics::IResourceFactory& factory, const PackedTerrainMaterialAtlases& atlases);

/** @brief Release and clear a packed terrain GPU set. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<void> releasePackedTerrainMaterialAtlases(graphics::IResourceFactory&  factory,
                                                               PackedTerrainMaterialGpuSet& set);

/** @brief Bind a packed terrain set for one draw and publish its feature counts. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<void> bindPackedTerrainMaterial(graphics::Shader& shader, const PackedTerrainMaterialGpuSet& gpu);

/**
 * @brief Bind one uploaded group and its four layer values to a terrain shader.
 * @param shader Borrowed shader configured by Procgen::createTerrainMaterialShader.
 * @param group Borrowed uploaded group retained through the draw.
 * @param material Owning metadata retained for the call.
 * @param groupIndex Group in [0, groups); selects layers groupIndex*4 through +3.
 * @return Success after all bindings are published, or failure before an invalid index is used.
 * @thread Graphics thread only; synchronous and non-reentrant.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<void> bindTerrainMaterialGroup(graphics::Shader& shader, const TerrainMaterialGpuSet& gpu,
                                                    const LoadedTerrainMaterial& material, std::size_t groupIndex);
}  // namespace eve::asset_procgen
