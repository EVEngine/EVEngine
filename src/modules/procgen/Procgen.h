#pragma once

#include "common/Module.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/OutputSpec.h"
#include "procgen/Palette.h"
#include "procgen/Params.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainSampler.h"
#include "procgen/heightmap/TerrainPipeline.h"
#include "procgen/heightmap/TerrainMesh.h"
#include "procgen/texture/CloudField.h"
#include "procgen/texture/CloudShadow.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
class Mesh;
class Shader;
}  // namespace eve::graphics

namespace eve::image {
class ImageData;
}  // namespace eve::image

namespace eve::data {
class ByteData;
}  // namespace eve::data

namespace eve::procgen {
class PbrTextureSet;

/**
 * @brief Procedural generation module.
 * Phase A: runtime maps (semantic Grid2D → TileLayer).
 * Phase B: runtime textures (ImageData / Texture).
 * Phase C (partial): mesh recipes (Marching Cubes → MeshBuild / Mesh).
 * Script: `procgen <- eve.Procgen();`
 */
class Procgen : public Module {
public:
    Module_REG(Procgen);
    Procgen();
    ~Procgen() override = default;

    Params     *newParams();
    OutputSpec *newOutput();
    Grid2D     *newGrid(int width, int height);

    // --- Phase A: maps ---
    Grid2D *generate(const std::string &algorithmId, Params *params);
    bool    generateTo(const std::string &algorithmId, Params *params, OutputSpec *output);
    bool    applyToLayer(Grid2D *grid, const std::string &palette, map::TileLayer *layer);

    void setPaletteGid(const std::string &palette, const std::string &semantic, int gid);
    int  getPaletteGid(const std::string &palette, const std::string &semantic) const;

    int         getAlgorithmCount() const;
    std::string getAlgorithmId(int index) const;
    bool        hasAlgorithm(const std::string &algorithmId) const;

    /**
     * @brief Post-process a generated grid: fill each wall cell's detail with an
     * 8-bit neighbour mask (autotile directions). Mutates `grid` in place.
     */
    bool autotileGrid(Grid2D *grid);

    /** @brief Fresh non-zero seed for regenerating a level. */
    uint32_t randomSeed();

    std::string lastError() const;
    std::string gridToJson(Grid2D *grid) const;

    // --- Phase B: textures ---
    /** @brief RGBA8 ImageData (caller owns). Pixel-friendly recipes: tex.soil/stone/marble/water/sky_cloud. */
    image::ImageData *generateImage(const std::string &recipeId, Params *params);
    /** @brief Normal map derived from albedo luminance (caller owns). */
    image::ImageData *generateNormalImage(const std::string &recipeId, Params *params);
    /**
     * @brief Upload recipe to GPU. repeatU/V follow params "seamless" (default on).
     * Caller owns Texture*.
     */
    graphics::Texture *generateTexture(const std::string &recipeId, Params *params,
                                       graphics::Graphics *gfx);

    int         getTextureRecipeCount() const;
    std::string getTextureRecipeId(int index) const;
    bool        hasTextureRecipe(const std::string &recipeId) const;

    // --- Phase E: dynamic clouds + cloud shadows ---
    /** @brief New deterministic, tiling, time-animated cloud field (caller owns). */
    CloudField *newCloudField();
    /** @brief New sun projection over a cloud field → ground shadows (caller owns). */
    CloudShadow *newCloudShadow();
    /** @brief Cloud coverage at world (x, z) and time (via a CloudField). */
    float cloudCoverageAt(CloudField *field, float x, float z, float time);
    /** @brief Light multiplier in [0,1] from a CloudShadow at world (x, z) and time. */
    float cloudShadowFactor(CloudShadow *shadow, float x, float z, float time);
    /** Sample coverage/factor into a caller-owned float buffer (w*h). */
    void sampleCloud(CloudField *field, float *out, int w, int h, float time, float x0, float z0,
                     float extent);
    void sampleCloudShadow(CloudShadow *shadow, float *out, int w, int h, float time, float x0,
                           float z0, float extent);
    /**
     * @brief Full metallic-roughness PBR set (albedo/normal/roughness/metallic/height/ao)
     * derived from a single displacement field. Caller owns the returned set
     * (call PbrTextureSet::destroy()). Recipes: pbr.soil/stone/rock/marble/water/
     * ripple/wood/cloth/ornament/spot/zebra/wall/cement/mud/sky_cloud.
     */
    PbrTextureSet *generatePbrMaterial(const std::string &recipeId, Params *params);

    int         getPbrRecipeCount() const;
    std::string getPbrRecipeId(int index) const;
    bool        hasPbrRecipe(const std::string &recipeId) const;

    // --- Mesh recipes (Marching Cubes, …) ---
    /** @brief CPU mesh (caller owns). Recipes: mesh.marchingcubes, mesh.hexplanet. */
    MeshBuild *buildMesh(const std::string &recipeId, Params *params);
    /** @brief Build + upload to GPU Mesh (owned by Graphics). */
    graphics::Mesh *generateMesh(const std::string &recipeId, Params *params,
                                 graphics::Graphics *gfx);

    int         getMeshRecipeCount() const;
    std::string getMeshRecipeId(int index) const;
    bool        hasMeshRecipe(const std::string &recipeId) const;

    // --- Phase D: terrain height sampling ---
    /** @brief Sampling function over continuous map coordinates (caller owns). */
    TerrainSampler *newTerrainSampler();
    /** @brief Empty in-memory heightmap (caller owns). */
    Heightmap *newHeightmap(int width, int height);
    /** @brief Build a sampler from params (seed/scale/octaves/…) and materialize it (caller owns). */
    Heightmap *generateHeightmap(Params *params);
    /** @brief Classify a heightmap into a semantic Grid2D using params bands (waterMax…). */
    bool heightmapToGrid(Heightmap *heightmap, Params *params, Grid2D *out);
    /** @brief Apply mass-conserving thermal erosion in place. */
    bool erodeTerrainThermal(Heightmap *heightmap, int iterations, float talus, float strength);
    /** @brief Apply deterministic grid water/sediment erosion in place. */
    bool erodeTerrainHydraulic(Heightmap *heightmap, int iterations, float rainfall,
                               float evaporation, float capacity, float erosion,
                               float deposition);
    /** @brief Cut drainage-connected river valleys into a heightmap in place. */
    bool erodeTerrainFluvial(Heightmap *heightmap, int iterations, float riverThreshold,
                             float incision, float maxDepth, float bankWidth);
    /** @brief Cut valleys with independent limits for channel incision and spill-sill breaching. */
    bool erodeTerrainFluvialAdvanced(Heightmap *heightmap, int iterations, float riverThreshold,
                                     float incision, float maxDepth, float bankWidth,
                                     float maxBreachDepth);
    /** @brief Cut valleys with explicit raster-to-physical coordinate scaling. */
    bool erodeTerrainFluvialScaled(Heightmap *heightmap, int iterations, float riverThreshold,
                                   float incision, float maxDepth, float bankWidth,
                                   float maxBreachDepth, float coordinateScale);
    /** @brief Cut scaled river valleys and return wear/deposition diagnostic layers (caller owns). */
    TerrainErosionMap *erodeTerrainFluvialDetailed(
        Heightmap *heightmap, int iterations, float riverThreshold, float incision,
        float maxDepth, float bankWidth, float maxBreachDepth, float coordinateScale);
    /** @brief Build river drainage, climate, and biome layers (caller owns). */
    TerrainLayers *analyzeTerrain(Heightmap *heightmap, float riverThreshold, float seaLevel,
                                  float latitude);
    /** @brief Analyze terrain with resolution-independent routing perturbations. */
    TerrainLayers *analyzeTerrainScaled(Heightmap *heightmap, float riverThreshold, float seaLevel,
                                        float latitude, float coordinateScale);
    /** @brief Bake a heightmap and analyzed layers into a chunked EVTR asset (caller owns). */
    data::ByteData *bakeTerrainAsset(Heightmap *heightmap, TerrainLayers *layers, int chunkSize);
    /** @brief Build one LOD terrain render chunk with skirts and ecological splat weights. */
    TerrainMeshChunk *buildTerrainChunk(Heightmap *heightmap, TerrainLayers *layers,
                                        int originX, int originY, int cellsX, int cellsY, int lod,
                                        float cellSize, float heightScale, float skirtDepth);
    /** @brief Select terrain LOD from measured screen-space geometric error. */
    int selectTerrainLod(Heightmap *heightmap, int originX, int originY, int cellsX, int cellsY,
                         int maxLod, float cellSize, float heightScale, float cameraDistance,
                         float viewportHeight, float verticalFovDegrees, float targetPixelError);
    /** @brief Upload a built terrain chunk through the engine's standard 3D mesh path. */
    graphics::Mesh *generateTerrainChunkMesh(TerrainMeshChunk *chunk, graphics::Graphics *gfx);
    /** @brief Generate a flow-aligned water ribbon mesh for one terrain chunk. */
    graphics::Mesh *generateTerrainRiverMesh(Heightmap *heightmap, TerrainLayers *layers,
                                             graphics::Graphics *gfx, int originX, int originY,
                                             int cellsX, int cellsY, float cellSize,
                                             float heightScale, float minWidth, float maxWidth,
                                             float heightOffset);
    /** @brief Generate one slope-banded river surface batch, such as calm water or cascades. */
    graphics::Mesh *generateTerrainRiverMeshAdvanced(
        Heightmap *heightmap, TerrainLayers *layers, graphics::Graphics *gfx,
        int originX, int originY, int cellsX, int cellsY, float cellSize,
        float heightScale, float minWidth, float maxWidth, float heightOffset,
        float minSurfaceSlope, float maxSurfaceSlope);
    /** @brief Generate resolved closed-basin lake surfaces for one terrain chunk. */
    graphics::Mesh *generateTerrainLakeMesh(Heightmap *heightmap, TerrainLayers *layers,
                                            graphics::Graphics *gfx, int originX, int originY,
                                            int cellsX, int cellsY, float cellSize,
                                            float heightScale, float minimumDepth,
                                            float heightOffset);
    /** @brief Create an RGBA8 sand/vegetation/rock/snow splat map for a terrain chunk. */
    image::ImageData *generateTerrainSplatMap(TerrainMeshChunk *chunk);
    /** @brief Create an opaque diagnostic albedo map by blending the terrain material weights. */
    image::ImageData *generateTerrainAlbedoMap(TerrainMeshChunk *chunk);
    /** @brief Visualize erosion wear (orange) and deposition (cyan) as an RGBA8 image. */
    image::ImageData *generateTerrainErosionMap(TerrainErosionMap *erosion, float exposure);
    /** @brief Visualize gross erosion as an orange RGBA8 image. */
    image::ImageData *generateTerrainWearMap(TerrainErosionMap *erosion, float exposure);
    /** @brief Visualize deposited material as a cyan RGBA8 image. */
    image::ImageData *generateTerrainDepositionMap(TerrainErosionMap *erosion, float exposure);
    /** @brief Create the runtime four-layer splat/PBR terrain mesh shader. */
    graphics::Shader *createTerrainMaterialShader(graphics::Graphics *gfx);
    /** @brief Create a Fresnel/specular procedural water shader for rivers and lakes. */
    graphics::Shader *createTerrainWaterShader(graphics::Graphics *gfx);

    PaletteTable &palettes() { return palettes_; }

private:
    bool runGenerate(const std::string &algorithmId, const Params &params, Grid2D &out);

    PaletteTable                     palettes_;
    mutable std::string              lastError_;
    mutable std::vector<std::string> algorithmIdsCache_;
    mutable std::vector<std::string> textureRecipeIdsCache_;
    mutable std::vector<std::string> pbrRecipeIdsCache_;
    mutable std::vector<std::string> meshRecipeIdsCache_;
};

}  // namespace eve::procgen
