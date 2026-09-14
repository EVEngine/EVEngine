#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
struct TerrainMultiTileReport;
struct TerrainStampSettings;
class GtsHeightBlendSet;
struct TerrainTextureAlignSettings;

/**
 * @brief Owning normalized terrain texture-layer weights with atomic mutation.
 * Layers share one finite width/height topology and each texel sums to one within float tolerance.
 */
class TerrainSplatmap {
public:
    TerrainSplatmap();
    ~TerrainSplatmap();
    TerrainSplatmap(TerrainSplatmap&&) noexcept;
    TerrainSplatmap& operator=(TerrainSplatmap&&) noexcept;
    TerrainSplatmap(const TerrainSplatmap&);
    TerrainSplatmap& operator=(const TerrainSplatmap&);
    /** @brief Initialize topology and assign all weight to defaultLayer. */
    [[nodiscard]] Result<int> initialize(int width, int height, int layerCount, int defaultLayer = 0);
    /** @brief Return one layer weight, or a structured range/precondition failure. */
    [[nodiscard]] Result<float> sample(int layer, int x, int y) const;
    /** @brief Copy one complete texture layer into a same-sized scalar mask atomically. */
    [[nodiscard]] Result<int> copyLayer(int layer, Heightmap& output) const;
    /** @brief Number of texels changed by the last successful paint operation. */
    int getLastChangedSamples() const noexcept;
    /** @brief Current splat width, or zero before initialization. */
    int getWidth() const noexcept;
    /** @brief Current splat height, or zero before initialization. */
    int getHeight() const noexcept;
    /** @brief Current layer count, or zero before initialization. */
    int getLayerCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend Result<int> paintTerrainSplatLayer(TerrainSplatmap&, const Heightmap&, int);
    friend Result<int> applyGtsHeightBlend(TerrainSplatmap&, const TerrainSplatmap&,
                                           const GtsHeightBlendSet&, float);
    friend Result<int> alignTerrainSplatTextures(TerrainSplatmap&, TerrainSplatmap&,
                                                  const TerrainTextureAlignSettings&);
    friend Result<TerrainMultiTileReport> paintTerrainSplatLayerMultiTile(
        const std::vector<struct TerrainSplatTile>&, const Heightmap&, int, const TerrainStampSettings&, bool,
        const std::vector<std::string>&);
};

/** @brief World-space placement and Pcg-compatible seam blending controls for two splatmaps. */
struct TerrainTextureAlignSettings {
    float terrainAOriginX = 0;
    float terrainAOriginZ = 0;
    float terrainAWidth = 1;
    float terrainADepth = 1;
    float terrainBOriginX = 1;
    float terrainBOriginZ = 0;
    float terrainBWidth = 1;
    float terrainBDepth = 1;
    float blendStrength = 1;
    int blendWidth = 8;
    float adjacencyTolerance = 1.5F;
};

/**
 * @brief Blend texture weights on adjacent terrain edges using Pcg's synchronized-noise equation.
 * @param terrainA First exclusively borrowed splatmap owner.
 * @param terrainB Second exclusively borrowed splatmap owner; must be distinct from terrainA.
 * @param settings Finite world geometry and blend controls, borrowed only for this call.
 * @return Total changed texels across both maps, or a structured failure with neither map modified.
 */
[[nodiscard]] Result<int> alignTerrainSplatTextures(TerrainSplatmap& terrainA,
                                                    TerrainSplatmap& terrainB,
                                                    const TerrainTextureAlignSettings& settings);

/** @brief Owning ordered GTS layer-height rasters and their alpha-channel transforms. */
class GtsHeightBlendSet {
public:
    GtsHeightBlendSet();
    ~GtsHeightBlendSet();
    GtsHeightBlendSet(GtsHeightBlendSet&&) noexcept;
    GtsHeightBlendSet& operator=(GtsHeightBlendSet&&) noexcept;
    GtsHeightBlendSet(const GtsHeightBlendSet&) = delete;
    GtsHeightBlendSet& operator=(const GtsHeightBlendSet&) = delete;
    /** @brief Copy one raw normalized height raster and append its contrast, brightness and increase transform. */
    [[nodiscard]] Result<int> addLayer(const Heightmap& height, float contrast = 1,
                                       float brightness = 1, float increase = 0);
    /** @brief Return the number of owned height layers. */
    int getLayerCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend Result<int> applyGtsHeightBlend(TerrainSplatmap&, const TerrainSplatmap&,
                                           const GtsHeightBlendSet&, float);
};

/**
 * @brief Recompute all splat weights using the GTS texture-height blending equation.
 * @return Changed texel count or InvalidArgument; output remains unchanged on failure.
 */
[[nodiscard]] Result<int> applyGtsHeightBlend(TerrainSplatmap& output, const TerrainSplatmap& input,
                                              const GtsHeightBlendSet& heights, float blendFactor);

/** @brief Borrowed terrain splat owner and world geometry for one synchronous texture transaction. */
struct TerrainSplatTile {
    std::string name;
    TerrainSplatmap* splatmap = nullptr;
    double originX = 0, originZ = 0, width = 1, depth = 1;
    bool worldMap = false;
};

/**
 * @brief Replace one terrain layer with a normalized paint raster and proportionally renormalize all other layers.
 * @param target Exclusively borrowed initialized owner, published only after every texel succeeds.
 * @param paint Finite normalized raster matching target topology; point sampling matches Pcg SetSplatmap.
 * @param targetLayer Layer index previously gathered by Pcg GetSplatmap.
 * @return Changed texel count or structured diagnostic. No references, callbacks, RNG or time survive the call.
 */
[[nodiscard]] Result<int> paintTerrainSplatLayer(TerrainSplatmap& target, const Heightmap& paint,
                                                int targetLayer);

/**
 * @brief Paint one shared Pcg Texture-domain operation raster across selected tiles atomically.
 * @return Affected-pixel mappings and changed texel total; no tile publishes on any validation failure.
 */
[[nodiscard]] Result<TerrainMultiTileReport> paintTerrainSplatLayerMultiTile(
    const std::vector<TerrainSplatTile>& tiles, const Heightmap& operationPaint, int targetLayer,
    const TerrainStampSettings& operationSettings, bool worldMapOperation = false,
    const std::vector<std::string>& validTerrainNames = {});

/**
 * @brief Owning script-safe multi-terrain splat transaction and all-tile undo history.
 * Input maps are copied. Paint, report and history publish through one owner-thread swap; no external references,
 * callbacks, renderer resources, RNG or time survive a call.
 */
class TerrainMultiSplatWorkspace {
public:
    TerrainMultiSplatWorkspace();
    ~TerrainMultiSplatWorkspace();
    TerrainMultiSplatWorkspace(TerrainMultiSplatWorkspace&&) noexcept;
    TerrainMultiSplatWorkspace& operator=(TerrainMultiSplatWorkspace&&) noexcept;
    TerrainMultiSplatWorkspace(const TerrainMultiSplatWorkspace&) = delete;
    TerrainMultiSplatWorkspace& operator=(const TerrainMultiSplatWorkspace&) = delete;
    /** @brief Copy one uniquely named initialized map and its world geometry into the workspace. */
    [[nodiscard]] Result<int> addTile(const std::string& name, const TerrainSplatmap& splatmap, double originX,
                                      double originZ, double width, double depth, bool worldMap = false);
    /** @brief Paint one shared operation raster into one layer of all intersecting owned tiles. */
    [[nodiscard]] Result<int> paint(const Heightmap& operationPaint, int targetLayer,
                                    const TerrainStampSettings& operationSettings,
                                    bool worldMapOperation = false);
    /** @brief Copy one named map into caller-owned output. */
    [[nodiscard]] Result<int> copyTile(const std::string& name, TerrainSplatmap& output) const;
    /** @brief Restore the previous all-map snapshot. */
    [[nodiscard]] Result<int> undo();
    /** @brief Restore the next all-map snapshot. */
    [[nodiscard]] Result<int> redo();
    /** @brief Number of owned tiles. */
    int getTileCount() const noexcept;
    /** @brief Changed texels in the current snapshot's last operation. */
    int getLastChangedSamples() const noexcept;
    /** @brief Affected tiles in the current snapshot's last operation. */
    int getLastAffectedTiles() const noexcept;
    /** @brief Successful operation count including the redo suffix. */
    int getOperationCount() const noexcept;
    /** @brief Applied operation count at the current history cursor. */
    int getAppliedCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen
