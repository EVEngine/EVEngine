#include "procgen/Procgen.h"

#include "image/ImageData.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/JsonExport.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/RoguelikeGenerator.h"
#include "procgen/texture/TextureRecipe.h"
#include "procgen/texture/PbrMaterial.h"

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
    PbrRecipeRegistry::instance().registerPbrBuiltins();
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

bool Procgen::autotileGrid(Grid2D *grid) {
    if (!grid) {
        lastError_ = "autotileGrid: null grid";
        return false;
    }
    return eve::procgen::autotileGridInPlace(*grid);
}

uint32_t Procgen::randomSeed() { return eve::procgen::randomSeedValue(); }

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

CloudField *Procgen::newCloudField() { return new CloudField(); }

CloudShadow *Procgen::newCloudShadow() { return new CloudShadow(); }

float Procgen::cloudCoverageAt(CloudField *field, float x, float z, float time) {
    lastError_.clear();
    if (!field) {
        lastError_ = "cloudCoverageAt: null field";
        return 0.f;
    }
    return field->coverageAt(x, z, time);
}

float Procgen::cloudShadowFactor(CloudShadow *shadow, float x, float z, float time) {
    lastError_.clear();
    if (!shadow) {
        lastError_ = "cloudShadowFactor: null shadow";
        return 1.f;
    }
    return shadow->shadowFactorAt(x, z, time);
}

void Procgen::sampleCloud(CloudField *field, float *out, int w, int h, float time, float x0,
                          float z0, float extent) {
    lastError_.clear();
    if (!field) {
        lastError_ = "sampleCloud: null field";
        return;
    }
    field->sample(out, w, h, time, x0, z0, extent);
}

void Procgen::sampleCloudShadow(CloudShadow *shadow, float *out, int w, int h, float time,
                                float x0, float z0, float extent) {
    lastError_.clear();
    if (!shadow) {
        lastError_ = "sampleCloudShadow: null shadow";
        return;
    }
    shadow->sampleCoverage(out, w, h, time, x0, z0, extent);
}

PbrTextureSet *Procgen::generatePbrMaterial(const std::string &recipeId, Params *params) {
    lastError_.clear();
    if (!params) {
        lastError_ = "generatePbrMaterial: null params";
        return nullptr;
    }
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    PbrTextureSet *set = PbrRecipeRegistry::instance().generate(recipeId, *params, lastError_);
    if (!set && lastError_.empty()) lastError_ = "generatePbrMaterial failed";
    return set;
}

int Procgen::getPbrRecipeCount() const {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    pbrRecipeIdsCache_ = PbrRecipeRegistry::instance().list();
    return int(pbrRecipeIdsCache_.size());
}

std::string Procgen::getPbrRecipeId(int index) const {
    if (pbrRecipeIdsCache_.empty()) {
        PbrRecipeRegistry::instance().registerPbrBuiltins();
        pbrRecipeIdsCache_ = PbrRecipeRegistry::instance().list();
    }
    if (index < 0 || index >= int(pbrRecipeIdsCache_.size())) return {};
    return pbrRecipeIdsCache_[size_t(index)];
}

bool Procgen::hasPbrRecipe(const std::string &recipeId) const {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    return PbrRecipeRegistry::instance().has(recipeId);
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

TerrainSampler *Procgen::newTerrainSampler() { return new TerrainSampler(); }

Heightmap *Procgen::newHeightmap(int width, int height) {
    return new Heightmap(width, height);
}

Heightmap *Procgen::generateHeightmap(Params *params) {
    lastError_.clear();
    if (!params) {
        lastError_ = "generateHeightmap: null params";
        return nullptr;
    }
    const TerrainSampler sampler = TerrainSampler::fromParams(*params);
    return new Heightmap(Heightmap::generate(sampler, params->getWidth(), params->getHeight()));
}

bool Procgen::heightmapToGrid(Heightmap *heightmap, Params *params, Grid2D *out) {
    lastError_.clear();
    if (!heightmap) {
        lastError_ = "heightmapToGrid: null heightmap";
        return false;
    }
    if (!out) {
        lastError_ = "heightmapToGrid: null grid";
        return false;
    }
    const TerrainBands bands = params ? TerrainBands::fromParams(*params) : TerrainBands();
    if (!heightmap->toGrid(*out, bands)) {
        lastError_ = "heightmapToGrid: heightmap is empty";
        return false;
    }
    return true;
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
    grid.addFunc("setDetail", &Grid2D::setDetail);
    grid.addFunc("getDetail", &Grid2D::getDetail);
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

    auto sampler = table.addClass<TerrainSampler>(
        "ProcgenTerrainSampler",
        std::function<TerrainSampler *()>([]() -> TerrainSampler * { return nullptr; }), true);
    sampler.addFunc("sample", &TerrainSampler::sample);
    sampler.addFunc("sampleTile", &TerrainSampler::sampleTile);
    sampler.addFunc("setSeed", &TerrainSampler::setSeed);
    sampler.addFunc("getSeed", &TerrainSampler::getSeed);
    sampler.addFunc("setScale", &TerrainSampler::setScale);
    sampler.addFunc("getScale", &TerrainSampler::getScale);
    sampler.addFunc("setFrequency", &TerrainSampler::setFrequency);
    sampler.addFunc("getFrequency", &TerrainSampler::getFrequency);
    sampler.addFunc("setWavelength", &TerrainSampler::setWavelength);
    sampler.addFunc("getWavelength", &TerrainSampler::getWavelength);
    sampler.addFunc("setOctaves", &TerrainSampler::setOctaves);
    sampler.addFunc("getOctaves", &TerrainSampler::getOctaves);
    sampler.addFunc("setLacunarity", &TerrainSampler::setLacunarity);
    sampler.addFunc("getLacunarity", &TerrainSampler::getLacunarity);
    sampler.addFunc("setGain", &TerrainSampler::setGain);
    sampler.addFunc("getGain", &TerrainSampler::getGain);
    sampler.addFunc("setRidge", &TerrainSampler::setRidge);
    sampler.addFunc("getRidge", &TerrainSampler::getRidge);
    sampler.addFunc("setWarp", &TerrainSampler::setWarp);
    sampler.addFunc("getWarp", &TerrainSampler::getWarp);
    sampler.addFunc("setExponent", &TerrainSampler::setExponent);
    sampler.addFunc("getExponent", &TerrainSampler::getExponent);
    sampler.addFunc("setContinent", &TerrainSampler::setContinent);
    sampler.addFunc("getContinent", &TerrainSampler::getContinent);
    sampler.addFunc("setIsland", &TerrainSampler::setIsland);
    sampler.addFunc("getIsland", &TerrainSampler::getIsland);
    sampler.addFunc("setCoastSoftness", &TerrainSampler::setCoastSoftness);
    sampler.addFunc("getCoastSoftness", &TerrainSampler::getCoastSoftness);
    sampler.addFunc("setWorldSize", &TerrainSampler::setWorldSize);
    sampler.addFunc("getWorldWidth", &TerrainSampler::getWorldWidth);
    sampler.addFunc("getWorldHeight", &TerrainSampler::getWorldHeight);
    sampler.addFunc("setBase", &TerrainSampler::setBase);
    sampler.addFunc("getBase", &TerrainSampler::getBase);
    sampler.addFunc("setAmplitude", &TerrainSampler::setAmplitude);
    sampler.addFunc("getAmplitude", &TerrainSampler::getAmplitude);
    sampler.addFunc("setClamp", &TerrainSampler::setClamp);
    sampler.addFunc("isClamped", &TerrainSampler::isClamped);
    sampler.addFunc("getClampMin", &TerrainSampler::getClampMin);
    sampler.addFunc("getClampMax", &TerrainSampler::getClampMax);

    auto heightmap = table.addClass<Heightmap>(
        "ProcgenHeightmap", std::function<Heightmap *()>([]() -> Heightmap * { return nullptr; }),
        true);
    heightmap.addFunc("resize", &Heightmap::resize);
    heightmap.addFunc("getWidth", &Heightmap::getWidth);
    heightmap.addFunc("getHeight", &Heightmap::getHeight);
    heightmap.addFunc("setHeight", &Heightmap::setHeight);
    heightmap.addFunc("height", &Heightmap::height);
    heightmap.addFunc("sampleBilinear", &Heightmap::sampleBilinear);
    heightmap.addFunc("sampleBilinearSeamless", &Heightmap::sampleBilinearSeamless);

    auto cloud = table.addClass<CloudField>(
        "ProcgenCloudField",
        std::function<CloudField *()>([]() -> CloudField * { return nullptr; }), true);
    cloud.addFunc("setSeed", &CloudField::setSeed);
    cloud.addFunc("setWorldScale", &CloudField::setWorldScale);
    cloud.addFunc("setCoverage", &CloudField::setCoverage);
    cloud.addFunc("setSoftness", &CloudField::setSoftness);
    cloud.addFunc("setDetail", &CloudField::setDetail);
    cloud.addFunc("setWind", &CloudField::setWind);
    cloud.addFunc("setOctaves", &CloudField::setOctaves);
    cloud.addFunc("setWarp", &CloudField::setWarp);
    cloud.addFunc("setSeamless", &CloudField::setSeamless);
    cloud.addFunc("coverageAt", &CloudField::coverageAt);

    auto cloudShadow = table.addClass<CloudShadow>(
        "ProcgenCloudShadow",
        std::function<CloudShadow *()>([]() -> CloudShadow * { return nullptr; }), true);
    cloudShadow.addFunc("setSunDirection", &CloudShadow::setSunDirection);
    cloudShadow.addFunc("setCloudAltitude", &CloudShadow::setCloudAltitude);
    cloudShadow.addFunc("setStrength", &CloudShadow::setStrength);
    cloudShadow.addFunc("coverageAt", &CloudShadow::coverageAt);
    cloudShadow.addFunc("shadowFactorAt", &CloudShadow::shadowFactorAt);
    auto pbr = table.addClass<PbrTextureSet>(
        "ProcgenPbrMaterial",
        std::function<PbrTextureSet *()>([]() -> PbrTextureSet * { return nullptr; }), true);
    pbr.addFunc("destroy", &PbrTextureSet::destroy);
    pbr.addFunc("getAlbedoWidth", [](const PbrTextureSet *s) { return s->albedo->getWidth(); });
    pbr.addFunc("getAlbedoHeight", [](const PbrTextureSet *s) { return s->albedo->getHeight(); });
    pbr.addFunc("getNormalWidth", [](const PbrTextureSet *s) { return s->normal->getWidth(); });
    pbr.addFunc("getRoughnessWidth", [](const PbrTextureSet *s) { return s->roughness->getWidth(); });
    pbr.addFunc("getMetallicWidth", [](const PbrTextureSet *s) { return s->metallic->getWidth(); });
    pbr.addFunc("getHeightWidth", [](const PbrTextureSet *s) { return s->height->getWidth(); });
    pbr.addFunc("getAoWidth", [](const PbrTextureSet *s) { return s->ao->getWidth(); });
    pbr.addFunc("hasAllMaps", [](const PbrTextureSet *s) {
        return s->albedo && s->normal && s->roughness && s->metallic && s->height && s->ao;
    });
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
    cls.addFunc("autotileGrid", &Procgen::autotileGrid);
    cls.addFunc("randomSeed", &Procgen::randomSeed);
    cls.addFunc("lastError", &Procgen::lastError);
    cls.addFunc("gridToJson", &Procgen::gridToJson);
    cls.addFunc("generateImage", &Procgen::generateImage);
    cls.addFunc("generateNormalImage", &Procgen::generateNormalImage);
    cls.addFunc("generateTexture", &Procgen::generateTexture);
    cls.addFunc("getTextureRecipeCount", &Procgen::getTextureRecipeCount);
    cls.addFunc("getTextureRecipeId", &Procgen::getTextureRecipeId);
    cls.addFunc("hasTextureRecipe", &Procgen::hasTextureRecipe);
    cls.addFunc("generatePbrMaterial", &Procgen::generatePbrMaterial);
    cls.addFunc("getPbrRecipeCount", &Procgen::getPbrRecipeCount);
    cls.addFunc("getPbrRecipeId", &Procgen::getPbrRecipeId);
    cls.addFunc("hasPbrRecipe", &Procgen::hasPbrRecipe);
    cls.addFunc("buildMesh", &Procgen::buildMesh);
    cls.addFunc("generateMesh", &Procgen::generateMesh);
    cls.addFunc("getMeshRecipeCount", &Procgen::getMeshRecipeCount);
    cls.addFunc("getMeshRecipeId", &Procgen::getMeshRecipeId);
    cls.addFunc("hasMeshRecipe", &Procgen::hasMeshRecipe);
    cls.addFunc("newTerrainSampler", &Procgen::newTerrainSampler);
    cls.addFunc("newHeightmap", &Procgen::newHeightmap);
    cls.addFunc("generateHeightmap", &Procgen::generateHeightmap);
    cls.addFunc("heightmapToGrid", &Procgen::heightmapToGrid);
    cls.addFunc("newCloudField", &Procgen::newCloudField);
    cls.addFunc("newCloudShadow", &Procgen::newCloudShadow);
    cls.addFunc("cloudCoverageAt", &Procgen::cloudCoverageAt);
    cls.addFunc("cloudShadowFactor", &Procgen::cloudShadowFactor);
    cls.addFunc("sampleCloud", &Procgen::sampleCloud);
    cls.addFunc("sampleCloudShadow", &Procgen::sampleCloudShadow);
}

}  // namespace eve::procgen
