#pragma once

#include "common/Module.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/OutputSpec.h"
#include "procgen/Palette.h"
#include "procgen/Params.h"
#include "procgen/PointSet.h"
#include "procgen/ProcgenSystem.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainSampler.h"
#include "procgen/texture/CloudField.h"
#include "procgen/texture/CloudShadow.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
class Mesh;
}  // namespace eve::graphics

namespace eve::image {
class ImageData;
}  // namespace eve::image

namespace eve::procgen {
struct PbrTextureSet;

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

    // --- Script-first point pipelines ---
    PointSet* newPointSet();
    PointSet* sampleGrid(int width, int depth, float spacing, uint32_t seed, float jitter);
    PointSet* filterHeight(PointSet* input, float minHeight, float maxHeight);
    PointSet* filterDensity(PointSet* input, float minDensity, float maxDensity);
    PointSet* filterBox(PointSet* input, float minX, float minY, float minZ, float maxX,
                        float maxY, float maxZ);
    PointSet* excludeBox(PointSet* input, float minX, float minY, float minZ, float maxX,
                         float maxY, float maxZ);
    PointSet* filterSlope(PointSet* input, float minDegrees, float maxDegrees);
    PointSet* excludeRadius(PointSet* input, float x, float z, float radius);
    PointSet* jitterPoints(PointSet* input, uint32_t seed, float amountX, float amountZ);
    PointSet* selfPrune(PointSet* input, float radius);
    PointSet* projectToHeightmap(PointSet* input, Heightmap* heightmap, float originX,
                                 float originZ, float cellSize, float heightScale);
    uint32_t  deriveSeed(uint32_t parent, const std::string& scope) const;

    // --- Atomic script rebuilds ---
    ProcgenContext* beginSystem(const std::string& name, uint32_t seed);
    bool            commitSystem(ProcgenContext* context);
    void            abortSystem(ProcgenContext* context);
    bool            removeSystem(const std::string& name);
    bool            hasSystem(const std::string& name) const;
    uint64_t        getSystemRevision(const std::string& name) const;
    uint32_t        getSystemSeed(const std::string& name) const;
    int             getSystemOutputCount(const std::string& name) const;
    std::string     getSystemOutputName(const std::string& name, int index) const;
    PointSet*       getSystemOutput(const std::string& name,
                                    const std::string& outputName) const;
    int             getSystemDebugStageCount(const std::string& name) const;
    std::string     getSystemDebugStageName(const std::string& name, int index) const;
    PointSet*       getSystemDebugStage(const std::string& name,
                                        const std::string& stageName) const;
    std::string     getSystemDebugReport(const std::string& name) const;

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

    PaletteTable &palettes() { return palettes_; }

private:
    bool runGenerate(const std::string &algorithmId, const Params &params, Grid2D &out);

    PaletteTable                     palettes_;
    mutable std::string              lastError_;
    mutable std::vector<std::string> algorithmIdsCache_;
    mutable std::vector<std::string> textureRecipeIdsCache_;
    mutable std::vector<std::string> pbrRecipeIdsCache_;
    mutable std::vector<std::string> meshRecipeIdsCache_;
    std::unordered_map<std::string, ProcgenSystemSnapshot> systems_;
};

}  // namespace eve::procgen
