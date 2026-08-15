#pragma once

#include "common/Module.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/OutputSpec.h"
#include "procgen/Palette.h"
#include "procgen/Params.h"

#include <string>
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

/**
 * Procedural generation module.
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

    std::string lastError() const;
    std::string gridToJson(Grid2D *grid) const;

    // --- Phase B: textures ---
    /** RGBA8 ImageData (caller owns). Pixel-friendly recipes: tex.soil/stone/marble/water/sky_cloud. */
    image::ImageData *generateImage(const std::string &recipeId, Params *params);
    /** Normal map derived from albedo luminance (caller owns). */
    image::ImageData *generateNormalImage(const std::string &recipeId, Params *params);
    /**
     * Upload recipe to GPU. repeatU/V follow params "seamless" (default on).
     * Caller owns Texture*.
     */
    graphics::Texture *generateTexture(const std::string &recipeId, Params *params,
                                       graphics::Graphics *gfx);

    int         getTextureRecipeCount() const;
    std::string getTextureRecipeId(int index) const;
    bool        hasTextureRecipe(const std::string &recipeId) const;

    // --- Mesh recipes (Marching Cubes, …) ---
    /** CPU mesh (caller owns). Recipes: mesh.marchingcubes, mesh.hexplanet. */
    MeshBuild *buildMesh(const std::string &recipeId, Params *params);
    /** Build + upload to GPU Mesh (owned by Graphics). */
    graphics::Mesh *generateMesh(const std::string &recipeId, Params *params,
                                 graphics::Graphics *gfx);

    int         getMeshRecipeCount() const;
    std::string getMeshRecipeId(int index) const;
    bool        hasMeshRecipe(const std::string &recipeId) const;

    PaletteTable &palettes() { return palettes_; }

private:
    bool runGenerate(const std::string &algorithmId, const Params &params, Grid2D &out);

    PaletteTable                     palettes_;
    mutable std::string              lastError_;
    mutable std::vector<std::string> algorithmIdsCache_;
    mutable std::vector<std::string> textureRecipeIdsCache_;
    mutable std::vector<std::string> meshRecipeIdsCache_;
};

}  // namespace eve::procgen
