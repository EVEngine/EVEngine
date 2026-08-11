#include "procgen/Procgen.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/JsonExport.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/texture/TextureRecipe.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <vector>

namespace eve::procgen {

Module_IMPL(Procgen, new Procgen());

Procgen::Procgen() {
    GeneratorRegistry::instance().registerBuiltins();
    TextureRecipeRegistry::instance().registerBuiltins();
    MeshRecipeRegistry::instance().registerBuiltins();
    // Sensible pixel-RPG default palette (games override GIDs to match tileset).
    setPaletteGid("default", "empty", 0);
    setPaletteGid("default", "wall", 1);
    setPaletteGid("default", "floor", 2);
    setPaletteGid("default", "corridor", 2);
    setPaletteGid("default", "door", 3);
    setPaletteGid("default", "water", 4);
    setPaletteGid("default", "sand", 5);
    setPaletteGid("default", "grass", 6);
    setPaletteGid("default", "dirt", 7);
    setPaletteGid("default", "stone", 8);
    setPaletteGid("default", "snow", 9);
    setPaletteGid("dungeon_default", "empty", 0);
    setPaletteGid("dungeon_default", "wall", 1);
    setPaletteGid("dungeon_default", "floor", 2);
    setPaletteGid("dungeon_default", "corridor", 2);
    setPaletteGid("dungeon_default", "door", 3);
}

Params *Procgen::newParams() { return new Params(); }
OutputSpec *Procgen::newOutput() { return new OutputSpec(); }

Grid2D *Procgen::newGrid(int width, int height) {
    auto *g = new Grid2D();
    g->resize(width, height);
    return g;
}

bool Procgen::runGenerate(const std::string &algorithmId, const Params &params, Grid2D &out) {
    lastError_.clear();
    GeneratorRegistry::instance().registerBuiltins();
    if (!GeneratorRegistry::instance().generate(algorithmId, params, out, lastError_)) {
        if (lastError_.empty()) lastError_ = "generate failed";
        return false;
    }
    return true;
}

Grid2D *Procgen::generate(const std::string &algorithmId, Params *params) {
    if (!params) {
        lastError_ = "generate: null params";
        return nullptr;
    }
    auto *grid = new Grid2D();
    if (!runGenerate(algorithmId, *params, *grid)) {
        delete grid;
        return nullptr;
    }
    return grid;
}

bool Procgen::generateTo(const std::string &algorithmId, Params *params, OutputSpec *output) {
    if (!params) {
        lastError_ = "generateTo: null params";
        return false;
    }
    if (!output) {
        lastError_ = "generateTo: null output";
        return false;
    }
    Grid2D grid;
    if (!runGenerate(algorithmId, *params, grid)) return false;

    const std::string target = output->getTarget();
    if (target == "grid") {
        lastError_ = "generateTo: target 'grid' has no sink; use generate()";
        return false;
    }
    if (target == "tilelayer") {
        return applyToLayer(&grid, output->getPalette(), output->getLayer());
    }
    if (target == "json") {
        if (!writeGridJson(grid, output->getPath(), &lastError_)) return false;
        return true;
    }
    lastError_ = "generateTo: unknown target '" + target + "' (use grid|tilelayer|json)";
    return false;
}

bool Procgen::applyToLayer(Grid2D *grid, const std::string &palette, map::TileLayer *layer) {
    if (!grid) {
        lastError_ = "applyToLayer: null grid";
        return false;
    }
    if (!palettes_.applyToLayer(*grid, palette, layer, &lastError_)) return false;
    return true;
}

void Procgen::setPaletteGid(const std::string &palette, const std::string &semantic, int gid) {
    palettes_.setGid(palette, semantic, gid);
}

int Procgen::getPaletteGid(const std::string &palette, const std::string &semantic) const {
    return palettes_.getGid(palette, semantic);
}

int Procgen::getAlgorithmCount() const {
    algorithmIdsCache_ = GeneratorRegistry::instance().list();
    return int(algorithmIdsCache_.size());
}

std::string Procgen::getAlgorithmId(int index) const {
    if (algorithmIdsCache_.empty()) algorithmIdsCache_ = GeneratorRegistry::instance().list();
    if (index < 0 || index >= int(algorithmIdsCache_.size())) return {};
    return algorithmIdsCache_[size_t(index)];
}

bool Procgen::hasAlgorithm(const std::string &algorithmId) const {
    return GeneratorRegistry::instance().has(algorithmId);
}

std::string Procgen::lastError() const { return lastError_; }

std::string Procgen::gridToJson(Grid2D *grid) const {
    if (!grid) return "{}";
    return eve::procgen::gridToJson(*grid);
}

image::ImageData *Procgen::generateImage(const std::string &recipeId, Params *params) {
    lastError_.clear();
    if (!params) {
        lastError_ = "generateImage: null params";
        return nullptr;
    }
    TextureRecipeRegistry::instance().registerBuiltins();
    image::ImageData *img =
        TextureRecipeRegistry::instance().generate(recipeId, *params, lastError_);
    if (!img && lastError_.empty()) lastError_ = "generateImage failed";
    return img;
}

image::ImageData *Procgen::generateNormalImage(const std::string &recipeId, Params *params) {
    image::ImageData *albedo = generateImage(recipeId, params);
    if (!albedo) return nullptr;
    const int w = albedo->getWidth();
    const int h = albedo->getHeight();
    auto *px    = static_cast<const uint8_t *>(albedo->getData());
    std::vector<float> height(size_t(w * h));
    for (int i = 0; i < w * h; ++i) {
        const size_t o = size_t(i) * 4u;
        height[size_t(i)] =
            (float(px[o]) * 0.299f + float(px[o + 1]) * 0.587f + float(px[o + 2]) * 0.114f) /
            255.f;
    }
    const bool seamless = params->getInt("seamless", 1) != 0;
    const float strength = params->getFloat("normalStrength", 4.f);
    image::ImageData *nrm = heightToNormalImage(height, w, h, strength, seamless);
    delete albedo;
    return nrm;
}

graphics::Texture *Procgen::generateTexture(const std::string &recipeId, Params *params,
                                            graphics::Graphics *gfx) {
    if (!gfx) {
        lastError_ = "generateTexture: null Graphics";
        return nullptr;
    }
    image::ImageData *img = generateImage(recipeId, params);
    if (!img) return nullptr;
    const bool seamless = !params || params->getInt("seamless", 1) != 0;
    auto *tex = gfx->newTexture(img->getWidth(), img->getHeight(),
                                static_cast<const uint8_t *>(img->getData()), seamless, seamless);
    delete img;
    return tex;
}

int Procgen::getTextureRecipeCount() const {
    TextureRecipeRegistry::instance().registerBuiltins();
    textureRecipeIdsCache_ = TextureRecipeRegistry::instance().list();
    return int(textureRecipeIdsCache_.size());
}

std::string Procgen::getTextureRecipeId(int index) const {
    if (textureRecipeIdsCache_.empty()) {
        TextureRecipeRegistry::instance().registerBuiltins();
        textureRecipeIdsCache_ = TextureRecipeRegistry::instance().list();
    }
    if (index < 0 || index >= int(textureRecipeIdsCache_.size())) return {};
    return textureRecipeIdsCache_[size_t(index)];
}

bool Procgen::hasTextureRecipe(const std::string &recipeId) const {
    TextureRecipeRegistry::instance().registerBuiltins();
    return TextureRecipeRegistry::instance().has(recipeId);
}

MeshBuild *Procgen::buildMesh(const std::string &recipeId, Params *params) {
    lastError_.clear();
    if (!params) {
        lastError_ = "buildMesh: null params";
        return nullptr;
    }
    MeshRecipeRegistry::instance().registerBuiltins();
    auto *mesh = new MeshBuild();
    if (!MeshRecipeRegistry::instance().generate(recipeId, *params, *mesh, lastError_)) {
        if (lastError_.empty()) lastError_ = "buildMesh failed";
        delete mesh;
        return nullptr;
    }
    return mesh;
}

graphics::Mesh *Procgen::generateMesh(const std::string &recipeId, Params *params,
                                      graphics::Graphics *gfx) {
    if (!gfx) {
        lastError_ = "generateMesh: null Graphics";
        return nullptr;
    }
    MeshBuild *cpu = buildMesh(recipeId, params);
    if (!cpu) return nullptr;
    graphics::Mesh *gpu =
        gfx->newMeshFromArrays(cpu->positions().data(), cpu->normals().data(), cpu->uvs().data(),
                               cpu->getVertexCount(), cpu->indices().data(), cpu->getIndexCount());
    delete cpu;
    return gpu;
}

int Procgen::getMeshRecipeCount() const {
    MeshRecipeRegistry::instance().registerBuiltins();
    meshRecipeIdsCache_ = MeshRecipeRegistry::instance().list();
    return int(meshRecipeIdsCache_.size());
}

std::string Procgen::getMeshRecipeId(int index) const {
    if (meshRecipeIdsCache_.empty()) {
        MeshRecipeRegistry::instance().registerBuiltins();
        meshRecipeIdsCache_ = MeshRecipeRegistry::instance().list();
    }
    if (index < 0 || index >= int(meshRecipeIdsCache_.size())) return {};
    return meshRecipeIdsCache_[size_t(index)];
}

bool Procgen::hasMeshRecipe(const std::string &recipeId) const {
    MeshRecipeRegistry::instance().registerBuiltins();
    return MeshRecipeRegistry::instance().has(recipeId);
}

void Procgen::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Procgen::create, false);
    expose(cls);

    auto params = table.addClass<Params>(
        "ProcgenParams", std::function<Params *()>([]() -> Params * { return nullptr; }), true);
    params.addFunc("setSeed", &Params::setSeed);
    params.addFunc("getSeed", &Params::getSeed);
    params.addFunc("setSize", &Params::setSize);
    params.addFunc("getWidth", &Params::getWidth);
    params.addFunc("getHeight", &Params::getHeight);
    params.addFunc("setInt", &Params::setInt);
    params.addFunc("setFloat", &Params::setFloat);
    params.addFunc("setString", &Params::setString);
    params.addFunc("has", &Params::has);
    params.addFunc("getInt", &Params::getInt);
    params.addFunc("getFloat", &Params::getFloat);
    params.addFunc("getString", &Params::getString);

    auto output = table.addClass<OutputSpec>(
        "ProcgenOutput", std::function<OutputSpec *()>([]() -> OutputSpec * { return nullptr; }),
        true);
    output.addFunc("setTarget", &OutputSpec::setTarget);
    output.addFunc("getTarget", &OutputSpec::getTarget);
    output.addFunc("setLayer", &OutputSpec::setLayer);
    output.addFunc("getLayer", &OutputSpec::getLayer);
    output.addFunc("setPalette", &OutputSpec::setPalette);
    output.addFunc("getPalette", &OutputSpec::getPalette);
    output.addFunc("setPath", &OutputSpec::setPath);
    output.addFunc("getPath", &OutputSpec::getPath);

    auto grid = table.addClass<Grid2D>(
        "ProcgenGrid2D", std::function<Grid2D *()>([]() -> Grid2D * { return nullptr; }), true);
    grid.addFunc("resize", &Grid2D::resize);
    grid.addFunc("getWidth", &Grid2D::getWidth);
    grid.addFunc("getHeight", &Grid2D::getHeight);
    grid.addFunc("setCell", &Grid2D::setCell);
    grid.addFunc("getCell", &Grid2D::getCell);
    grid.addFunc("fill", &Grid2D::fill);
    grid.addFunc("setMeta", &Grid2D::setMeta);
    grid.addFunc("getMeta", &Grid2D::getMeta);
    grid.addFunc("clearObjects", &Grid2D::clearObjects);
    grid.addFunc("addObjectAt", &Grid2D::addObjectAt);
    grid.addFunc("addObject", &Grid2D::addObject);
    grid.addFunc("getObjectCount", &Grid2D::getObjectCount);
    grid.addFunc("getObjectName", &Grid2D::getObjectName);
    grid.addFunc("getObjectType", &Grid2D::getObjectType);
    grid.addFunc("getObjectX", &Grid2D::getObjectX);
    grid.addFunc("getObjectY", &Grid2D::getObjectY);
    grid.addFunc("getObjectWidth", &Grid2D::getObjectWidth);
    grid.addFunc("getObjectHeight", &Grid2D::getObjectHeight);
    grid.addFunc("getObjectGid", &Grid2D::getObjectGid);

    auto mesh = table.addClass<MeshBuild>(
        "ProcgenMeshBuild", std::function<MeshBuild *()>([]() -> MeshBuild * { return nullptr; }),
        true);
    mesh.addFunc("clear", &MeshBuild::clear);
    mesh.addFunc("getVertexCount", &MeshBuild::getVertexCount);
    mesh.addFunc("getIndexCount", &MeshBuild::getIndexCount);
    mesh.addFunc("empty", &MeshBuild::empty);
    mesh.addFunc("getPositionX", &MeshBuild::getPositionX);
    mesh.addFunc("getPositionY", &MeshBuild::getPositionY);
    mesh.addFunc("getPositionZ", &MeshBuild::getPositionZ);
    mesh.addFunc("getNormalX", &MeshBuild::getNormalX);
    mesh.addFunc("getNormalY", &MeshBuild::getNormalY);
    mesh.addFunc("getNormalZ", &MeshBuild::getNormalZ);
    mesh.addFunc("getUvU", &MeshBuild::getUvU);
    mesh.addFunc("getUvV", &MeshBuild::getUvV);
    mesh.addFunc("getIndex", &MeshBuild::getIndex);
    mesh.addFunc("setMeta", &MeshBuild::setMeta);
    mesh.addFunc("getMeta", &MeshBuild::getMeta);
}

void Procgen::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Procgen::getName);
    cls.addFunc("newParams", &Procgen::newParams);
    cls.addFunc("newOutput", &Procgen::newOutput);
    cls.addFunc("newGrid", &Procgen::newGrid);
    cls.addFunc("generate", &Procgen::generate);
    cls.addFunc("generateTo", &Procgen::generateTo);
    cls.addFunc("applyToLayer", &Procgen::applyToLayer);
    cls.addFunc("setPaletteGid", &Procgen::setPaletteGid);
    cls.addFunc("getPaletteGid", &Procgen::getPaletteGid);
    cls.addFunc("getAlgorithmCount", &Procgen::getAlgorithmCount);
    cls.addFunc("getAlgorithmId", &Procgen::getAlgorithmId);
    cls.addFunc("hasAlgorithm", &Procgen::hasAlgorithm);
    cls.addFunc("lastError", &Procgen::lastError);
    cls.addFunc("gridToJson", &Procgen::gridToJson);
    cls.addFunc("generateImage", &Procgen::generateImage);
    cls.addFunc("generateNormalImage", &Procgen::generateNormalImage);
    cls.addFunc("generateTexture", &Procgen::generateTexture);
    cls.addFunc("getTextureRecipeCount", &Procgen::getTextureRecipeCount);
    cls.addFunc("getTextureRecipeId", &Procgen::getTextureRecipeId);
    cls.addFunc("hasTextureRecipe", &Procgen::hasTextureRecipe);
    cls.addFunc("buildMesh", &Procgen::buildMesh);
    cls.addFunc("generateMesh", &Procgen::generateMesh);
    cls.addFunc("getMeshRecipeCount", &Procgen::getMeshRecipeCount);
    cls.addFunc("getMeshRecipeId", &Procgen::getMeshRecipeId);
    cls.addFunc("hasMeshRecipe", &Procgen::hasMeshRecipe);
}

}  // namespace eve::procgen
