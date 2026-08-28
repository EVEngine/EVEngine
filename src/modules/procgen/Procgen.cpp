#include "procgen/Procgen.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/JsonExport.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/RoguelikeGenerator.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/texture/TextureRecipe.h"
#include "procgen/texture/PbrMaterial.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "data/ByteData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <vector>
#include <utility>

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

bool Procgen::erodeTerrainThermal(Heightmap *heightmap, int iterations, float talus,
                                  float strength) {
    lastError_.clear();
    if (!heightmap || heightmap->getWidth() < 2 || heightmap->getHeight() < 2) {
        lastError_ = "erodeTerrainThermal: heightmap must be at least 2x2";
        return false;
    }
    TerrainPipeline::erodeThermal(*heightmap, {iterations, talus, strength});
    return true;
}

bool Procgen::erodeTerrainHydraulic(Heightmap *heightmap, int iterations, float rainfall,
                                    float evaporation, float capacity, float erosion,
                                    float deposition) {
    lastError_.clear();
    if (!heightmap || heightmap->getWidth() < 2 || heightmap->getHeight() < 2) {
        lastError_ = "erodeTerrainHydraulic: heightmap must be at least 2x2";
        return false;
    }
    TerrainPipeline::erodeHydraulic(
        *heightmap, {iterations, rainfall, evaporation, capacity, erosion, deposition});
    return true;
}

bool Procgen::erodeTerrainFluvial(Heightmap *heightmap, int iterations, float riverThreshold,
                                  float incision, float maxDepth, float bankWidth) {
    return erodeTerrainFluvialAdvanced(heightmap, iterations, riverThreshold, incision,
                                       maxDepth, bankWidth, std::min(maxDepth, 0.04f));
}

bool Procgen::erodeTerrainFluvialAdvanced(Heightmap *heightmap, int iterations,
                                          float riverThreshold, float incision,
                                          float maxDepth, float bankWidth,
                                          float maxBreachDepth) {
    return erodeTerrainFluvialScaled(heightmap, iterations, riverThreshold, incision,
                                     maxDepth, bankWidth, maxBreachDepth, 1.f);
}

bool Procgen::erodeTerrainFluvialScaled(Heightmap *heightmap, int iterations,
                                        float riverThreshold, float incision,
                                        float maxDepth, float bankWidth,
                                        float maxBreachDepth, float coordinateScale) {
    lastError_.clear();
    if (!heightmap || heightmap->getWidth() < 3 || heightmap->getHeight() < 3) {
        lastError_ = "erodeTerrainFluvial: heightmap must be at least 3x3";
        return false;
    }
    if (!std::isfinite(coordinateScale) || coordinateScale <= 0.f) {
        lastError_ = "erodeTerrainFluvialScaled: coordinateScale must be positive";
        return false;
    }
    TerrainPipeline::erodeFluvial(*heightmap,
        {iterations, riverThreshold, incision, maxDepth, bankWidth,
         maxBreachDepth, coordinateScale});
    return true;
}

TerrainErosionMap *Procgen::erodeTerrainFluvialDetailed(
    Heightmap *heightmap, int iterations, float riverThreshold, float incision,
    float maxDepth, float bankWidth, float maxBreachDepth, float coordinateScale) {
    lastError_.clear();
    if (!heightmap || heightmap->getWidth() < 3 || heightmap->getHeight() < 3) {
        lastError_ = "erodeTerrainFluvialDetailed: heightmap must be at least 3x3";
        return nullptr;
    }
    if (!std::isfinite(coordinateScale) || coordinateScale <= 0.f) {
        lastError_ = "erodeTerrainFluvialDetailed: coordinateScale must be positive";
        return nullptr;
    }
    TerrainErosionMap diagnostics = TerrainPipeline::erodeFluvialDetailed(
        *heightmap, {iterations, riverThreshold, incision, maxDepth, bankWidth,
                     maxBreachDepth, coordinateScale});
    if (diagnostics.width <= 0) {
        lastError_ = "erodeTerrainFluvialDetailed: invalid erosion settings";
        return nullptr;
    }
    return new TerrainErosionMap(std::move(diagnostics));
}

TerrainLayers *Procgen::analyzeTerrain(Heightmap *heightmap, float riverThreshold,
                                       float seaLevel, float latitude) {
    return analyzeTerrainScaled(heightmap, riverThreshold, seaLevel, latitude, 1.f);
}

TerrainLayers *Procgen::analyzeTerrainScaled(Heightmap *heightmap, float riverThreshold,
                                             float seaLevel, float latitude,
                                             float coordinateScale) {
    lastError_.clear();
    if (!heightmap || heightmap->getWidth() <= 0 || heightmap->getHeight() <= 0) {
        lastError_ = "analyzeTerrain: heightmap is empty";
        return nullptr;
    }
    if (!std::isfinite(coordinateScale) || coordinateScale <= 0.f) {
        lastError_ = "analyzeTerrainScaled: coordinateScale must be positive";
        return nullptr;
    }
    HydrologyMap hydrology = TerrainPipeline::buildHydrology(
        *heightmap, riverThreshold, seaLevel, coordinateScale);
    ClimateMap climate = TerrainPipeline::buildClimate(
        *heightmap, hydrology, seaLevel, latitude, coordinateScale);
    return new TerrainLayers(std::move(hydrology), std::move(climate));
}

data::ByteData *Procgen::bakeTerrainAsset(Heightmap *heightmap, TerrainLayers *layers,
                                          int chunkSize) {
    lastError_.clear();
    if (!heightmap || !layers) {
        lastError_ = "bakeTerrainAsset: heightmap and layers are required";
        return nullptr;
    }
    std::vector<uint8_t> bytes;
    if (!TerrainAsset::bake(*heightmap, layers->hydrology(), layers->climate(), chunkSize, bytes,
                            &lastError_)) return nullptr;
    return new data::ByteData(bytes.data(), bytes.size());
}

TerrainMeshChunk *Procgen::buildTerrainChunk(Heightmap *heightmap, TerrainLayers *layers,
                                             int originX, int originY, int cellsX, int cellsY,
                                             int lod, float cellSize, float heightScale,
                                             float skirtDepth) {
    lastError_.clear();
    if (!heightmap) { lastError_ = "buildTerrainChunk: heightmap is required"; return nullptr; }
    TerrainMeshSettings settings;
    settings.originX = originX; settings.originY = originY;
    settings.cellsX = cellsX; settings.cellsY = cellsY; settings.lod = lod;
    settings.cellSize = cellSize; settings.heightScale = heightScale; settings.skirtDepth = skirtDepth;
    auto *chunk = new TerrainMeshChunk();
    if (!TerrainMeshBuilder::build(*heightmap, layers, settings, *chunk, &lastError_)) {
        delete chunk; return nullptr;
    }
    return chunk;
}

int Procgen::selectTerrainLod(Heightmap *heightmap, int originX, int originY,
                              int cellsX, int cellsY, int maxLod, float cellSize,
                              float heightScale, float cameraDistance, float viewportHeight,
                              float verticalFovDegrees, float targetPixelError) {
    lastError_.clear();
    if (!heightmap) { lastError_ = "selectTerrainLod: heightmap is required"; return -1; }
    TerrainMeshSettings settings;
    settings.originX = originX; settings.originY = originY;
    settings.cellsX = cellsX; settings.cellsY = cellsY;
    settings.cellSize = cellSize; settings.heightScale = heightScale;
    const int lod = TerrainLodSelector::select(*heightmap, settings, maxLod, cameraDistance,
                                               viewportHeight, verticalFovDegrees,
                                               targetPixelError);
    if (lod < 0) lastError_ = "selectTerrainLod: invalid bounds or projection settings";
    return lod;
}

graphics::Mesh *Procgen::generateTerrainChunkMesh(TerrainMeshChunk *chunk,
                                                   graphics::Graphics *gfx) {
    lastError_.clear();
    if (!chunk || !gfx || chunk->mesh().empty()) {
        lastError_ = "generateTerrainChunkMesh: chunk and graphics are required";
        return nullptr;
    }
    const MeshBuild &mesh = chunk->mesh();
    return gfx->newMeshFromArrays(mesh.positions().data(), mesh.normals().data(), mesh.uvs().data(),
                                  mesh.getVertexCount(), mesh.indices().data(), mesh.getIndexCount());
}

graphics::Mesh *Procgen::generateTerrainRiverMesh(Heightmap *heightmap, TerrainLayers *layers,
                                                   graphics::Graphics *gfx, int originX, int originY,
                                                   int cellsX, int cellsY, float cellSize,
                                                   float heightScale, float minWidth, float maxWidth,
                                                   float heightOffset) {
    return generateTerrainRiverMeshAdvanced(
        heightmap, layers, gfx, originX, originY, cellsX, cellsY, cellSize,
        heightScale, minWidth, maxWidth, heightOffset, 0.f, 0.30f);
}

graphics::Mesh *Procgen::generateTerrainRiverMeshAdvanced(
    Heightmap *heightmap, TerrainLayers *layers, graphics::Graphics *gfx,
    int originX, int originY, int cellsX, int cellsY, float cellSize,
    float heightScale, float minWidth, float maxWidth, float heightOffset,
    float minSurfaceSlope, float maxSurfaceSlope) {
    lastError_.clear();
    if (!heightmap || !layers || !gfx) {
        lastError_ = "generateTerrainRiverMesh: heightmap, layers, and graphics are required";
        return nullptr;
    }
    TerrainRiverMeshSettings settings;
    settings.originX = originX; settings.originY = originY;
    settings.cellsX = cellsX; settings.cellsY = cellsY;
    settings.cellSize = cellSize; settings.heightScale = heightScale;
    settings.minWidth = minWidth; settings.maxWidth = maxWidth;
    settings.heightOffset = heightOffset;
    settings.minSurfaceSlope = minSurfaceSlope;
    settings.maxSurfaceSlope = maxSurfaceSlope;
    MeshBuild river;
    if (!TerrainRiverMeshBuilder::build(*heightmap, *layers, settings, river, &lastError_) || river.empty())
        return nullptr;
    return gfx->newMeshFromArrays(river.positions().data(), river.normals().data(), river.uvs().data(),
                                  river.getVertexCount(), river.indices().data(), river.getIndexCount());
}

graphics::Mesh *Procgen::generateTerrainLakeMesh(Heightmap *heightmap, TerrainLayers *layers,
                                                  graphics::Graphics *gfx, int originX, int originY,
                                                  int cellsX, int cellsY, float cellSize,
                                                  float heightScale, float minimumDepth,
                                                  float heightOffset) {
    lastError_.clear();
    if (!heightmap || !layers || !gfx) {
        lastError_ = "generateTerrainLakeMesh: heightmap, layers, and graphics are required";
        return nullptr;
    }
    TerrainLakeMeshSettings settings;
    settings.originX = originX; settings.originY = originY;
    settings.cellsX = cellsX; settings.cellsY = cellsY;
    settings.cellSize = cellSize; settings.heightScale = heightScale;
    settings.minimumDepth = minimumDepth; settings.heightOffset = heightOffset;
    MeshBuild lake;
    if (!TerrainLakeMeshBuilder::build(*heightmap, *layers, settings, lake, &lastError_) || lake.empty())
        return nullptr;
    return gfx->newMeshFromArrays(lake.positions().data(), lake.normals().data(), lake.uvs().data(),
                                  lake.getVertexCount(), lake.indices().data(), lake.getIndexCount());
}

image::ImageData *Procgen::generateTerrainSplatMap(TerrainMeshChunk *chunk) {
    lastError_.clear();
    if (!chunk || chunk->getSplatWidth() <= 0 || chunk->getSplatHeight() <= 0) {
        lastError_ = "generateTerrainSplatMap: a built terrain chunk is required";
        return nullptr;
    }
    auto *image = new image::ImageData(chunk->getSplatWidth(), chunk->getSplatHeight(), "RGBA8");
    auto *pixels = static_cast<uint8_t *>(image->getData());
    for (int vertex = 0; vertex < chunk->getBaseVertexCount(); ++vertex) {
        std::array<int, 4> quantized{};
        std::array<float, 4> remainder{};
        int total = 0;
        for (int channel = 0; channel < 4; ++channel) {
            const float scaled = std::clamp(chunk->getMaterialWeight(vertex, channel), 0.f, 1.f) * 255.f;
            quantized[channel] = int(std::floor(scaled));
            remainder[channel] = scaled - float(quantized[channel]);
            total += quantized[channel];
        }
        while (total < 255) {
            const int channel = int(std::max_element(remainder.begin(), remainder.end()) - remainder.begin());
            ++quantized[channel]; remainder[channel] = -1.f; ++total;
        }
        for (int channel = 0; channel < 4; ++channel)
            pixels[size_t(vertex) * 4u + size_t(channel)] = uint8_t(quantized[channel]);
    }
    return image;
}

image::ImageData *Procgen::generateTerrainAlbedoMap(TerrainMeshChunk *chunk) {
    lastError_.clear();
    if (!chunk || chunk->getSplatWidth() <= 0 || chunk->getSplatHeight() <= 0) {
        lastError_ = "generateTerrainAlbedoMap: a built terrain chunk is required";
        return nullptr;
    }
    static constexpr std::array<std::array<float, 3>, 4> palette{{
        {{0.55f, 0.40f, 0.22f}}, // sand, dry soil, and river sediment
        {{0.15f, 0.38f, 0.12f}}, // vegetation
        {{0.36f, 0.35f, 0.33f}}, // exposed rock
        {{0.88f, 0.91f, 0.94f}}, // snow
    }};
    auto *image = new image::ImageData(chunk->getSplatWidth(), chunk->getSplatHeight(), "RGBA8");
    auto *pixels = static_cast<uint8_t *>(image->getData());
    for (int vertex = 0; vertex < chunk->getBaseVertexCount(); ++vertex) {
        const int biome = chunk->getBiome(vertex);
        std::array<float, 3> semanticColor{};
        bool useSemanticColor = true;
        if (biome == int(Biome::Ocean)) semanticColor = {0.035f, 0.16f, 0.25f};
        else if (biome == int(Biome::River)) semanticColor = {0.24f, 0.18f, 0.09f};
        else if (biome == int(Biome::Lake)) semanticColor = {0.12f, 0.19f, 0.14f};
        else if (biome == int(Biome::Wetland)) semanticColor = {0.12f, 0.28f, 0.07f};
        else if (biome == int(Biome::Beach)) semanticColor = {0.42f, 0.34f, 0.19f};
        else useSemanticColor = false;
        for (int component = 0; component < 3; ++component) {
            float value = semanticColor[component];
            if (!useSemanticColor) {
                value = 0.f;
                for (int channel = 0; channel < 4; ++channel)
                    value += chunk->getMaterialWeight(vertex, channel) * palette[channel][component];
            }
            pixels[size_t(vertex) * 4u + size_t(component)] =
                uint8_t(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
        }
        pixels[size_t(vertex) * 4u + 3u] = 255;
    }
    return image;
}

namespace {
float diagnosticScale(const std::vector<float> &values, float exposure) {
    if (std::isfinite(exposure) && exposure > 0.f) return exposure;
    std::vector<float> positive;
    positive.reserve(values.size());
    for (float value : values) if (std::isfinite(value) && value > 0.f) positive.push_back(value);
    if (positive.empty()) return 1.f;
    const size_t percentile = std::min(positive.size() - 1,
        size_t(std::floor(float(positive.size() - 1) * 0.99f)));
    std::nth_element(positive.begin(), positive.begin() + ptrdiff_t(percentile), positive.end());
    return 1.f / std::max(1e-8f, positive[percentile]);
}

enum class ErosionImageMode { Combined, Wear, Deposit };

image::ImageData *erosionDiagnosticImage(TerrainErosionMap *map, float exposure,
                                         ErosionImageMode mode) {
    if (!map || map->width <= 0 || map->height <= 0 ||
        map->wear.size() != size_t(map->width) * size_t(map->height) ||
        map->deposition.size() != map->wear.size()) return nullptr;
    const float wearScale = diagnosticScale(map->wear, exposure);
    const float depositScale = diagnosticScale(map->deposition, exposure);
    auto *result = new image::ImageData(map->width, map->height, "RGBA8");
    auto *pixels = static_cast<uint8_t *>(result->getData());
    for (size_t i = 0; i < map->wear.size(); ++i) {
        const float wear = std::sqrt(std::clamp(map->wear[i] * wearScale, 0.f, 1.f));
        const float deposit = std::sqrt(std::clamp(map->deposition[i] * depositScale, 0.f, 1.f));
        float r = 0.025f, g = 0.035f, b = 0.050f;
        if (mode == ErosionImageMode::Wear) {
            r += 0.95f * wear; g += 0.30f * wear; b += 0.035f * wear;
        } else if (mode == ErosionImageMode::Deposit) {
            r += 0.035f * deposit; g += 0.78f * deposit; b += 0.95f * deposit;
        } else {
            r += 0.95f * wear + 0.035f * deposit;
            g += 0.30f * wear + 0.78f * deposit;
            b += 0.035f * wear + 0.95f * deposit;
        }
        pixels[i * 4u] = uint8_t(std::lround(std::clamp(r, 0.f, 1.f) * 255.f));
        pixels[i * 4u + 1u] = uint8_t(std::lround(std::clamp(g, 0.f, 1.f) * 255.f));
        pixels[i * 4u + 2u] = uint8_t(std::lround(std::clamp(b, 0.f, 1.f) * 255.f));
        pixels[i * 4u + 3u] = 255;
    }
    return result;
}
}  // namespace

image::ImageData *Procgen::generateTerrainErosionMap(TerrainErosionMap *erosion, float exposure) {
    lastError_.clear();
    image::ImageData *result = erosionDiagnosticImage(erosion, exposure, ErosionImageMode::Combined);
    if (!result) lastError_ = "generateTerrainErosionMap: valid erosion diagnostics are required";
    return result;
}

image::ImageData *Procgen::generateTerrainWearMap(TerrainErosionMap *erosion, float exposure) {
    lastError_.clear();
    image::ImageData *result = erosionDiagnosticImage(erosion, exposure, ErosionImageMode::Wear);
    if (!result) lastError_ = "generateTerrainWearMap: valid erosion diagnostics are required";
    return result;
}

image::ImageData *Procgen::generateTerrainDepositionMap(
    TerrainErosionMap *erosion, float exposure) {
    lastError_.clear();
    image::ImageData *result = erosionDiagnosticImage(erosion, exposure, ErosionImageMode::Deposit);
    if (!result) lastError_ = "generateTerrainDepositionMap: valid erosion diagnostics are required";
    return result;
}

graphics::Shader *Procgen::createTerrainMaterialShader(graphics::Graphics *gfx) {
    lastError_.clear();
    if (!gfx) {
        lastError_ = "createTerrainMaterialShader: graphics is required";
        return nullptr;
    }
    static const char *fragment = R"GLSL(#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
struct Light3D { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind;
} ubo;
layout(set=0,binding=1) uniform sampler2D splatSampler;
layout(set=0,binding=4,std140) uniform ShadowFrame {
    mat4 lightVP[3]; vec4 splits; vec4 bias; vec4 cascadeBias; vec4 cascadeTexel;
} shadow;
layout(set=0,binding=5) uniform sampler2DArrayShadow shadowMap;
layout(location=0) out vec4 outColor;

float hash21(vec2 p) {
    uvec2 q = uvec2(ivec2(floor(p)));
    uint n = q.x * 1597334677u ^ q.y * 3812015801u;
    n = (n ^ (n >> 15u)) * 2246822519u;
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float terrainShadow(vec3 worldPos, float viewDepth) {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4)
        return 1.0;
    int cascade = viewDepth < shadow.splits.x ? 0 : (viewDepth < shadow.splits.y ? 1 : 2);
    vec4 clip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 ||
        ndc.z <= 0.0 || ndc.z >= 1.0) return 1.0;
    float compareBias = cascade == 0 ? shadow.cascadeBias.x :
                        (cascade == 1 ? shadow.cascadeBias.y : shadow.cascadeBias.z);
    if (compareBias <= 1e-8) compareBias = shadow.bias.x;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
        visibility += texture(shadowMap,
            vec4(uv + vec2(x, y) * texel * 0.65, float(cascade), ndc.z - compareBias));
    return visibility / 9.0;
}
void main() {
    vec4 weights = max(texture(splatSampler, vUV), vec4(0.0));
    weights /= max(dot(weights, vec4(1.0)), 1e-5);
    const vec3 sand = vec3(0.34, 0.235, 0.115);
    const vec3 vegetation = vec3(0.075, 0.255, 0.055);
    const vec3 rock = vec3(0.285, 0.275, 0.255);
    const vec3 snow = vec3(0.78, 0.82, 0.86);
    vec3 albedo = sand * weights.r + vegetation * weights.g +
                  rock * weights.b + snow * weights.a;
    float macro = mix(0.88, 1.12, valueNoise(vWorldPos.xz * 0.55));
    float fine = mix(0.94, 1.06, valueNoise(vWorldPos.xz * 5.0 + vec2(17.0, 31.0)));
    albedo *= macro * fine * vTint.rgb;
    float roughness = dot(weights, vec4(0.82, 0.94, 0.68, 0.52));
    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0) N = -N;
    vec2 detailP = vWorldPos.xz * 4.0;
    float detail0 = valueNoise(detailP);
    float detailX = valueNoise(detailP + vec2(0.08, 0.0));
    float detailZ = valueNoise(detailP + vec2(0.0, 0.08));
    N = normalize(N + vec3(detail0 - detailX, 0.0, detail0 - detailZ) *
                  mix(0.16, 0.045, weights.a));
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndl = max(dot(N, L), 0.0);
    float wrap = ndl * 0.88 + 0.12;
    float specPower = mix(96.0, 5.0, roughness);
    float spec = pow(max(dot(N, H), 0.0), specPower) * mix(0.22, 0.035, roughness);
    vec3 sky = ubo.ambient.rgb * mix(vec3(0.72, 0.64, 0.55), vec3(1.05), N.y * 0.5 + 0.5);
    float viewDepth = max(-vViewPos.z, 0.0);
    float visibility = terrainShadow(vWorldPos, viewDepth);
    vec3 color = albedo * (sky + ubo.lightColor.rgb * wrap * visibility) +
                 ubo.lightColor.rgb * spec * visibility;
    color = color / (color + vec3(0.72));
    float nearZ = max(ubo.clipInfo.x, 1e-4);
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    float linearDepth = clamp((viewDepth - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outColor = vec4(color, linearDepth);
}
)GLSL";
    try {
        return gfx->newMeshShader(fragment);
    } catch (const std::exception &e) {
        lastError_ = std::string("createTerrainMaterialShader: ") + e.what();
        return nullptr;
    }
}

graphics::Shader *Procgen::createTerrainWaterShader(graphics::Graphics *gfx) {
    lastError_.clear();
    if (!gfx) {
        lastError_ = "createTerrainWaterShader: graphics is required";
        return nullptr;
    }
    static const char *fragment = R"GLSL(#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
struct Light3D { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind;
} ubo;
layout(location=0) out vec4 outColor;
void main() {
    vec2 p = vWorldPos.xz;
    float wx = sin(p.x * 1.7 + p.y * 0.43) + 0.55 * sin(p.x * 4.1 - p.y * 1.3);
    float wz = cos(p.y * 1.9 - p.x * 0.37) + 0.55 * cos(p.y * 3.7 + p.x * 1.1);
    vec3 N = normalize(vec3(wx * 0.055, 1.0, wz * 0.055));
    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndv = max(dot(N, V), 0.0);
    float ndl = max(dot(N, L), 0.0);
    float fresnel = 0.035 + 0.50 * pow(1.0 - ndv, 4.0);
    float glint = pow(max(dot(N, H), 0.0), 150.0) * 1.15;
    vec3 deep = vec3(0.018, 0.16, 0.205);
    vec3 shallow = vec3(0.045, 0.36, 0.39);
    vec3 water = mix(deep, shallow, 0.35 + 0.25 * N.y);
    water *= mix(vec3(1.0), max(vTint.rgb, vec3(0.12)), 0.18);
    vec3 sky = ubo.ambient.rgb * mix(vec3(0.55, 0.72, 0.82), vec3(1.0), fresnel);
    vec3 color = water * (0.72 + 0.58 * ndl) + sky * fresnel +
                 ubo.lightColor.rgb * glint;
    color = color / (color + vec3(0.68));
    float nearZ = max(ubo.clipInfo.x, 1e-4);
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    float viewDepth = max(-vViewPos.z, 0.0);
    float linearDepth = clamp((viewDepth - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outColor = vec4(color, linearDepth);
}
)GLSL";
    try {
        return gfx->newMeshShader(fragment);
    } catch (const std::exception &e) {
        lastError_ = std::string("createTerrainWaterShader: ") + e.what();
        return nullptr;
    }
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

    auto terrainLayers = table.addClass<TerrainLayers>(
        "ProcgenTerrainLayers",
        std::function<TerrainLayers *()>([]() -> TerrainLayers * { return nullptr; }), true);
    terrainLayers.addFunc("getWidth", &TerrainLayers::getWidth);
    terrainLayers.addFunc("getHeight", &TerrainLayers::getHeight);
    terrainLayers.addFunc("getFlowAccumulation", &TerrainLayers::getFlowAccumulation);
    terrainLayers.addFunc("getFlowDirection", &TerrainLayers::getFlowDirection);
    terrainLayers.addFunc("getFlowVectorX", &TerrainLayers::getFlowVectorX);
    terrainLayers.addFunc("getFlowVectorY", &TerrainLayers::getFlowVectorY);
    terrainLayers.addFunc("getStreamOrder", &TerrainLayers::getStreamOrder);
    terrainLayers.addFunc("isRiver", &TerrainLayers::isRiver);
    terrainLayers.addFunc("getLakeDepth", &TerrainLayers::getLakeDepth);
    terrainLayers.addFunc("isLake", &TerrainLayers::isLake);
    terrainLayers.addFunc("getTemperature", &TerrainLayers::getTemperature);
    terrainLayers.addFunc("getMoisture", &TerrainLayers::getMoisture);
    terrainLayers.addFunc("getBiome", &TerrainLayers::getBiome);
    terrainLayers.addFunc("getBiomeName", &TerrainLayers::getBiomeName);

    auto erosionMap = table.addClass<TerrainErosionMap>(
        "ProcgenTerrainErosionMap",
        std::function<TerrainErosionMap *()>([]() -> TerrainErosionMap * { return nullptr; }), true);
    erosionMap.addFunc("getWidth", &TerrainErosionMap::getWidth);
    erosionMap.addFunc("getHeight", &TerrainErosionMap::getHeight);
    erosionMap.addFunc("getWear", &TerrainErosionMap::getWear);
    erosionMap.addFunc("getDeposition", &TerrainErosionMap::getDeposition);
    erosionMap.addFunc("getHeightDelta", &TerrainErosionMap::getHeightDelta);

    auto terrainMesh = table.addClass<TerrainMeshChunk>(
        "ProcgenTerrainMeshChunk",
        std::function<TerrainMeshChunk *()>([]() -> TerrainMeshChunk * { return nullptr; }), true);
    terrainMesh.addFunc("getVertexCount", &TerrainMeshChunk::getVertexCount);
    terrainMesh.addFunc("getIndexCount", &TerrainMeshChunk::getIndexCount);
    terrainMesh.addFunc("getBaseVertexCount", &TerrainMeshChunk::getBaseVertexCount);
    terrainMesh.addFunc("getLodStep", &TerrainMeshChunk::getLodStep);
    terrainMesh.addFunc("getOriginX", &TerrainMeshChunk::getOriginX);
    terrainMesh.addFunc("getOriginY", &TerrainMeshChunk::getOriginY);
    terrainMesh.addFunc("getSplatWidth", &TerrainMeshChunk::getSplatWidth);
    terrainMesh.addFunc("getSplatHeight", &TerrainMeshChunk::getSplatHeight);
    terrainMesh.addFunc("getGeometricError", &TerrainMeshChunk::getGeometricError);
    terrainMesh.addFunc("getBiome", &TerrainMeshChunk::getBiome);
    terrainMesh.addFunc("getMaterialWeight", &TerrainMeshChunk::getMaterialWeight);
    terrainMesh.addFunc("getPositionX", [](const TerrainMeshChunk *c, int i) { return c ? c->mesh().getPositionX(i) : 0.f; });
    terrainMesh.addFunc("getPositionY", [](const TerrainMeshChunk *c, int i) { return c ? c->mesh().getPositionY(i) : 0.f; });
    terrainMesh.addFunc("getPositionZ", [](const TerrainMeshChunk *c, int i) { return c ? c->mesh().getPositionZ(i) : 0.f; });
    terrainMesh.addFunc("getNormalX", [](const TerrainMeshChunk *c, int i) { return c ? c->mesh().getNormalX(i) : 0.f; });
    terrainMesh.addFunc("getNormalY", [](const TerrainMeshChunk *c, int i) { return c ? c->mesh().getNormalY(i) : 0.f; });
    terrainMesh.addFunc("getNormalZ", [](const TerrainMeshChunk *c, int i) { return c ? c->mesh().getNormalZ(i) : 0.f; });
    terrainMesh.addFunc("getIndex", [](const TerrainMeshChunk *c, int i) { return c ? c->mesh().getIndex(i) : 0; });

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
    cls.addFunc("erodeTerrainThermal", &Procgen::erodeTerrainThermal);
    cls.addFunc("erodeTerrainHydraulic", &Procgen::erodeTerrainHydraulic);
    cls.addFunc("erodeTerrainFluvial", &Procgen::erodeTerrainFluvial);
    cls.addFunc("erodeTerrainFluvialAdvanced", &Procgen::erodeTerrainFluvialAdvanced);
    cls.addFunc("erodeTerrainFluvialScaled", &Procgen::erodeTerrainFluvialScaled);
    cls.addFunc("erodeTerrainFluvialDetailed", &Procgen::erodeTerrainFluvialDetailed);
    cls.addFunc("analyzeTerrain", &Procgen::analyzeTerrain);
    cls.addFunc("analyzeTerrainScaled", &Procgen::analyzeTerrainScaled);
    cls.addFunc("bakeTerrainAsset", &Procgen::bakeTerrainAsset);
    cls.addFunc("buildTerrainChunk", &Procgen::buildTerrainChunk);
    cls.addFunc("selectTerrainLod", &Procgen::selectTerrainLod);
    cls.addFunc("generateTerrainChunkMesh", &Procgen::generateTerrainChunkMesh);
    cls.addFunc("generateTerrainRiverMesh", &Procgen::generateTerrainRiverMesh);
    cls.addFunc("generateTerrainRiverMeshAdvanced", &Procgen::generateTerrainRiverMeshAdvanced);
    cls.addFunc("generateTerrainLakeMesh", &Procgen::generateTerrainLakeMesh);
    cls.addFunc("generateTerrainSplatMap", &Procgen::generateTerrainSplatMap);
    cls.addFunc("generateTerrainAlbedoMap", &Procgen::generateTerrainAlbedoMap);
    cls.addFunc("generateTerrainErosionMap", &Procgen::generateTerrainErosionMap);
    cls.addFunc("generateTerrainWearMap", &Procgen::generateTerrainWearMap);
    cls.addFunc("generateTerrainDepositionMap", &Procgen::generateTerrainDepositionMap);
    cls.addFunc("createTerrainMaterialShader", &Procgen::createTerrainMaterialShader);
    cls.addFunc("createTerrainWaterShader", &Procgen::createTerrainWaterShader);
    cls.addFunc("newCloudField", &Procgen::newCloudField);
    cls.addFunc("newCloudShadow", &Procgen::newCloudShadow);
    cls.addFunc("cloudCoverageAt", &Procgen::cloudCoverageAt);
    cls.addFunc("cloudShadowFactor", &Procgen::cloudShadowFactor);
    cls.addFunc("sampleCloud", &Procgen::sampleCloud);
    cls.addFunc("sampleCloudShadow", &Procgen::sampleCloudShadow);
}

}  // namespace eve::procgen
