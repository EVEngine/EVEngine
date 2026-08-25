#include "procgen/Procgen.h"

#include "procgen/ProcgenCapabilities.h"

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

#include <sstream>
#include <vector>

namespace eve::procgen {

Module_IMPL(Procgen, new Procgen());

Procgen::Procgen() {
    registerProcgenCapabilities();
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
    setPaletteGid("default", "road", 3);
    setPaletteGid("dungeon_default", "empty", 0);
    setPaletteGid("dungeon_default", "wall", 1);
    setPaletteGid("dungeon_default", "floor", 2);
    setPaletteGid("dungeon_default", "corridor", 2);
    setPaletteGid("dungeon_default", "door", 3);
    setPaletteGid("dungeon_default", "road", 3);
}

Params *Procgen::newParams() { return new Params(); }
OutputSpec *Procgen::newOutput() { return new OutputSpec(); }

Grid2D *Procgen::newGrid(int width, int height) {
    auto *g = new Grid2D();
    g->resize(width, height);
    return g;
}

PointSet* Procgen::newPointSet() { return new PointSet(); }

PointSet* Procgen::sampleGrid(int width, int depth, float spacing, uint32_t seed, float jitter) {
    return new PointSet(sampleGridPoints(width, depth, spacing, seed, jitter));
}

PointSet* Procgen::filterHeight(PointSet* input, float minHeight, float maxHeight) {
    if (!input) {
        lastError_ = "filterHeight: null input";
        return nullptr;
    }
    return new PointSet(filterPointHeight(*input, minHeight, maxHeight));
}

PointSet* Procgen::filterDensity(PointSet* input, float minDensity, float maxDensity) {
    if (!input) {
        lastError_ = "filterDensity: null input";
        return nullptr;
    }
    return new PointSet(filterPointDensity(*input, minDensity, maxDensity));
}

PointSet* Procgen::filterBox(PointSet* input, float minX, float minY, float minZ, float maxX,
                             float maxY, float maxZ) {
    if (!input) {
        lastError_ = "filterBox: null input";
        return nullptr;
    }
    return new PointSet(filterPointBox(*input, minX, minY, minZ, maxX, maxY, maxZ, false));
}

PointSet* Procgen::excludeBox(PointSet* input, float minX, float minY, float minZ, float maxX,
                              float maxY, float maxZ) {
    if (!input) {
        lastError_ = "excludeBox: null input";
        return nullptr;
    }
    return new PointSet(filterPointBox(*input, minX, minY, minZ, maxX, maxY, maxZ, true));
}

PointSet* Procgen::filterSlope(PointSet* input, float minDegrees, float maxDegrees) {
    if (!input) {
        lastError_ = "filterSlope: null input";
        return nullptr;
    }
    return new PointSet(filterPointSlope(*input, minDegrees, maxDegrees));
}

PointSet* Procgen::filterPolygon(PointSet* input, PointSet* polygon) {
    if (!input || !polygon) {
        lastError_ = "filterPolygon: null input";
        return nullptr;
    }
    return new PointSet(filterPointsByPolygon(*input, *polygon, false));
}

PointSet* Procgen::excludePolygon(PointSet* input, PointSet* polygon) {
    if (!input || !polygon) {
        lastError_ = "excludePolygon: null input";
        return nullptr;
    }
    return new PointSet(filterPointsByPolygon(*input, *polygon, true));
}

PointSet* Procgen::filterSplineDistance(PointSet* input, PointSet* controlPoints,
                                        float minDistance, float maxDistance) {
    if (!input || !controlPoints) {
        lastError_ = "filterSplineDistance: null input";
        return nullptr;
    }
    return new PointSet(
        filterPointsBySplineDistance(*input, *controlPoints, minDistance, maxDistance));
}

PointSet* Procgen::excludeRadius(PointSet* input, float x, float z, float radius) {
    if (!input) {
        lastError_ = "excludeRadius: null input";
        return nullptr;
    }
    return new PointSet(excludePointRadius(*input, x, z, radius));
}

PointSet* Procgen::jitterPoints(PointSet* input, uint32_t seed, float amountX, float amountZ) {
    if (!input) {
        lastError_ = "jitterPoints: null input";
        return nullptr;
    }
    return new PointSet(jitterPointPositions(*input, seed, amountX, amountZ));
}

PointSet* Procgen::selfPrune(PointSet* input, float radius) {
    if (!input) {
        lastError_ = "selfPrune: null input";
        return nullptr;
    }
    return new PointSet(selfPrunePoints(*input, radius));
}

PointSet* Procgen::projectToHeightmap(PointSet* input, Heightmap* heightmap, float originX,
                                      float originZ, float cellSize, float heightScale) {
    if (!input || !heightmap) {
        lastError_ = "projectToHeightmap: null input";
        return nullptr;
    }
    if (cellSize <= 0.f) {
        lastError_ = "projectToHeightmap: cellSize must be positive";
        return nullptr;
    }
    return new PointSet(
        projectPointsToHeightmap(*input, *heightmap, originX, originZ, cellSize, heightScale));
}

PointSet* Procgen::sampleSpline(PointSet* controlPoints, float spacing, uint32_t seed,
                                float lateralJitter) {
    if (!controlPoints) {
        lastError_ = "sampleSpline: null control points";
        return nullptr;
    }
    if (spacing <= 0.f) {
        lastError_ = "sampleSpline: spacing must be positive";
        return nullptr;
    }
    return new PointSet(samplePolylinePoints(*controlPoints, spacing, seed, lateralJitter));
}

uint32_t Procgen::deriveSeed(uint32_t parent, const std::string& scope) const {
    return eve::procgen::deriveSeed(parent, scope);
}

ProcgenContext* Procgen::beginSystem(const std::string& name, uint32_t seed) {
    lastError_.clear();
    if (name.empty()) {
        lastError_ = "beginSystem: name is empty";
        return nullptr;
    }
    auto*      context  = new ProcgenContext(name, seed);
    const auto previous = systems_.find(name);
    if (previous != systems_.end()) context->stageCache_ = previous->second.stageCache;
    return context;
}

ProcgenContext* Procgen::beginCachedSystem(const std::string& name, uint32_t seed, const std::string& buildKey) {
    lastError_.clear();
    if (name.empty()) {
        lastError_ = "beginCachedSystem: name is empty";
        return nullptr;
    }
    if (buildKey.empty()) {
        lastError_ = "beginCachedSystem: build key is empty";
        return nullptr;
    }
    const uint32_t normalizedSeed = seed ? seed : 1u;
    const auto     found          = systems_.find(name);
    const bool     cacheHit =
        found != systems_.end() && found->second.seed == normalizedSeed && found->second.buildKey == buildKey;
    auto* context = new ProcgenContext(name, normalizedSeed, buildKey, cacheHit);
    if (found != systems_.end()) context->stageCache_ = found->second.stageCache;
    return context;
}

bool Procgen::commitSystem(ProcgenContext* context) {
    lastError_.clear();
    if (!context) {
        lastError_ = "commitSystem: null context";
        return false;
    }
    if (!context->isActive()) {
        lastError_ = "commitSystem: transaction is closed";
        return false;
    }
    if (context->hasFailed()) {
        lastError_ = "commitSystem: " + context->getError();
        context->close();
        return false;
    }
    if (!context->openTraces_.empty()) {
        lastError_ = "commitSystem: unfinished trace '" + context->openTraces_.back().name + "'";
        context->close();
        return false;
    }

    const auto current = systems_.find(context->name_);
    if (current != systems_.end()) previousSystems_[context->name_] = current->second;
    auto& snapshot            = systems_[context->name_];
    snapshot.seed             = context->seed_;
    snapshot.revision         = snapshot.revision + 1u;
    snapshot.buildKey         = context->buildKey_;
    snapshot.outputs          = context->outputs_;
    snapshot.outputOrder      = context->outputOrder_;
    snapshot.debugStages      = context->debugStages_;
    snapshot.debugStageOrder  = context->debugStageOrder_;
    snapshot.stageCache       = context->stageCache_;
    snapshot.stageCacheHits   = context->stageCacheHits_;
    snapshot.stageCacheMisses = context->stageCacheMisses_;
    snapshot.traces           = context->traces_;
    context->close();
    return true;
}

void Procgen::abortSystem(ProcgenContext* context) {
    if (context) context->abort();
}

bool Procgen::removeSystem(const std::string& name) {
    previousSystems_.erase(name);
    return systems_.erase(name) != 0;
}

bool Procgen::hasSystem(const std::string& name) const {
    return systems_.find(name) != systems_.end();
}

uint64_t Procgen::getSystemRevision(const std::string& name) const {
    const auto found = systems_.find(name);
    return found == systems_.end() ? 0u : found->second.revision;
}

uint32_t Procgen::getSystemSeed(const std::string& name) const {
    const auto found = systems_.find(name);
    return found == systems_.end() ? 0u : found->second.seed;
}

std::string Procgen::getSystemBuildKey(const std::string& name) const {
    const auto found = systems_.find(name);
    return found == systems_.end() ? std::string() : found->second.buildKey;
}

int Procgen::getSystemOutputCount(const std::string& name) const {
    const auto found = systems_.find(name);
    return found == systems_.end() ? 0 : int(found->second.outputOrder.size());
}

std::string Procgen::getSystemOutputName(const std::string& name, int index) const {
    const auto found = systems_.find(name);
    if (found == systems_.end() || index < 0 || index >= int(found->second.outputOrder.size()))
        return {};
    return found->second.outputOrder[size_t(index)];
}

PointSet* Procgen::getSystemOutput(const std::string& name,
                                   const std::string& outputName) const {
    const auto system = systems_.find(name);
    if (system == systems_.end()) return nullptr;
    const auto output = system->second.outputs.find(outputName);
    return output == system->second.outputs.end() ? nullptr : new PointSet(output->second);
}

int Procgen::getSystemDebugStageCount(const std::string& name) const {
    const auto found = systems_.find(name);
    return found == systems_.end() ? 0 : int(found->second.debugStageOrder.size());
}

std::string Procgen::getSystemDebugStageName(const std::string& name, int index) const {
    const auto found = systems_.find(name);
    if (found == systems_.end() || index < 0 || index >= int(found->second.debugStageOrder.size()))
        return {};
    return found->second.debugStageOrder[size_t(index)];
}

PointSet* Procgen::getSystemDebugStage(const std::string& name,
                                       const std::string& stageName) const {
    const auto system = systems_.find(name);
    if (system == systems_.end()) return nullptr;
    const auto stage = system->second.debugStages.find(stageName);
    return stage == system->second.debugStages.end() ? nullptr : new PointSet(stage->second);
}

PointSet* Procgen::getPreviousSystemDebugStage(const std::string& name,
                                               const std::string& stageName) const {
    const auto system = previousSystems_.find(name);
    if (system == previousSystems_.end()) return nullptr;
    const auto stage = system->second.debugStages.find(stageName);
    return stage == system->second.debugStages.end() ? nullptr : new PointSet(stage->second);
}

uint64_t Procgen::getPreviousSystemRevision(const std::string& name) const {
    const auto found = previousSystems_.find(name);
    return found == previousSystems_.end() ? 0u : found->second.revision;
}

std::string Procgen::getSystemDebugReport(const std::string& name) const {
    const auto found = systems_.find(name);
    if (found == systems_.end()) return "system '" + name + "' is not committed";
    const auto&        snapshot = found->second;
    std::ostringstream report;
    report << name << " revision=" << snapshot.revision << " seed=" << snapshot.seed;
    if (!snapshot.buildKey.empty()) report << " buildKey=" << snapshot.buildKey;
    report << " stageCache=" << snapshot.stageCacheHits << " hit/" << snapshot.stageCacheMisses << " miss";
    for (const auto& trace : snapshot.traces) {
        report << "\n  " << trace.name << " input=" << trace.inputCount << " output=" << trace.outputCount
               << " ms=" << trace.milliseconds;
    }
    for (const auto& outputName : snapshot.outputOrder) {
        report << "\n  output " << outputName << " points="
               << snapshot.outputs.at(outputName).getCount();
    }
    for (const auto& stageName : snapshot.debugStageOrder) {
        report << "\n  debug " << stageName << " points="
               << snapshot.debugStages.at(stageName).getCount();
    }
    return report.str();
}

std::string Procgen::getSystemDebugDiffReport(const std::string& name) const {
    const auto current = systems_.find(name);
    if (current == systems_.end()) return "system '" + name + "' is not committed";
    const auto previous = previousSystems_.find(name);
    if (previous == previousSystems_.end()) return name + " has no previous revision";

    std::ostringstream report;
    report << name << " revision=" << previous->second.revision << " -> " << current->second.revision;
    for (const auto& stageName : current->second.debugStageOrder) {
        const int  currentCount = current->second.debugStages.at(stageName).getCount();
        const auto oldStage     = previous->second.debugStages.find(stageName);
        const int oldCount = oldStage == previous->second.debugStages.end() ? 0 : oldStage->second.getCount();
        report << "\n  debug " << stageName << " points=" << currentCount << " delta=";
        if (currentCount >= oldCount) report << "+";
        report << currentCount - oldCount;
    }
    for (const auto& stageName : previous->second.debugStageOrder) {
        if (current->second.debugStages.find(stageName) != current->second.debugStages.end()) continue;
        report << "\n  debug " << stageName << " removed delta=-"
               << previous->second.debugStages.at(stageName).getCount();
    }
    return report.str();
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

namespace {

const ParamDescriptor *algorithmParam(const std::string &algorithmId, int index) {
    const GeneratorDescriptor *descriptor = GeneratorRegistry::instance().descriptor(algorithmId);
    if (!descriptor || index < 0 || index >= int(descriptor->params.size())) return nullptr;
    return &descriptor->params[size_t(index)];
}

}  // namespace

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

RecipeDescriptor *Procgen::getAlgorithmSchema(const std::string &algorithmId) const {
    const RecipeDescriptor *schema = GeneratorRegistry::instance().descriptor(algorithmId);
    return schema ? new RecipeDescriptor(*schema) : nullptr;
}

std::string Procgen::getAlgorithmDisplayName(const std::string &algorithmId) const {
    const GeneratorDescriptor *descriptor = GeneratorRegistry::instance().descriptor(algorithmId);
    return descriptor ? descriptor->displayName : std::string{};
}

std::string Procgen::getAlgorithmCategory(const std::string &algorithmId) const {
    const GeneratorDescriptor *descriptor = GeneratorRegistry::instance().descriptor(algorithmId);
    return descriptor ? descriptor->category : std::string{};
}

int Procgen::getAlgorithmParamCount(const std::string &algorithmId) const {
    const GeneratorDescriptor *descriptor = GeneratorRegistry::instance().descriptor(algorithmId);
    return descriptor ? int(descriptor->params.size()) : 0;
}

std::string Procgen::getAlgorithmParamKey(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? param->key : std::string{};
}

std::string Procgen::getAlgorithmParamLabel(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? param->displayName : std::string{};
}

std::string Procgen::getAlgorithmParamDescription(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? param->description : std::string{};
}

std::string Procgen::getAlgorithmParamCategory(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? param->category : std::string{};
}

std::string Procgen::getAlgorithmParamKind(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    if (!param) return {};
    switch (param->kind) {
        case ParamKind::Integer: return "int";
        case ParamKind::Float: return "float";
        case ParamKind::Boolean: return "bool";
        case ParamKind::String: return "string";
        case ParamKind::Choice: return "choice";
    }
    return {};
}

std::string Procgen::getAlgorithmParamDefault(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? param->defaultValue : std::string{};
}

bool Procgen::algorithmParamHasMinimum(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param && param->hasMinimum;
}

bool Procgen::algorithmParamHasMaximum(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param && param->hasMaximum;
}

float Procgen::getAlgorithmParamMinimum(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? float(param->minimum) : 0.f;
}

float Procgen::getAlgorithmParamMaximum(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? float(param->maximum) : 0.f;
}

float Procgen::getAlgorithmParamStep(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? float(param->step) : 0.f;
}

bool Procgen::isAlgorithmParamAdvanced(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param && param->advanced;
}

int Procgen::getAlgorithmParamChoiceCount(const std::string &algorithmId, int index) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, index);
    return param ? int(param->choices.size()) : 0;
}

std::string Procgen::getAlgorithmParamChoice(const std::string &algorithmId, int paramIndex,
                                             int choiceIndex) const {
    const ParamDescriptor *param = algorithmParam(algorithmId, paramIndex);
    if (!param || choiceIndex < 0 || choiceIndex >= int(param->choices.size())) return {};
    return param->choices[size_t(choiceIndex)];
}

bool Procgen::applyAlgorithmDefaults(const std::string &algorithmId, Params *params) const {
    return params && GeneratorRegistry::instance().applyDefaults(algorithmId, *params);
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

RecipeDescriptor *Procgen::getTextureRecipeSchema(const std::string &recipeId) const {
    TextureRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor *schema = TextureRecipeRegistry::instance().descriptor(recipeId);
    return schema ? new RecipeDescriptor(*schema) : nullptr;
}

bool Procgen::applyTextureRecipeDefaults(const std::string &recipeId, Params *params) const {
    if (!params) return false;
    TextureRecipeRegistry::instance().registerBuiltins();
    return TextureRecipeRegistry::instance().applyDefaults(recipeId, *params);
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

RecipeDescriptor *Procgen::getPbrRecipeSchema(const std::string &recipeId) const {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    const RecipeDescriptor *schema = PbrRecipeRegistry::instance().descriptor(recipeId);
    return schema ? new RecipeDescriptor(*schema) : nullptr;
}

bool Procgen::applyPbrRecipeDefaults(const std::string &recipeId, Params *params) const {
    if (!params) return false;
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    return PbrRecipeRegistry::instance().applyDefaults(recipeId, *params);
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
    graphics::Mesh *gpu = uploadMesh(cpu, gfx);
    delete cpu;
    return gpu;
}

graphics::Mesh *Procgen::uploadMesh(MeshBuild *mesh, graphics::Graphics *gfx) {
    lastError_.clear();
    if (!mesh || mesh->empty()) { lastError_ = "uploadMesh: null or empty MeshBuild"; return nullptr; }
    if (!gfx) { lastError_ = "uploadMesh: null Graphics"; return nullptr; }
    return gfx->newMeshFromArrays(mesh->positions().data(), mesh->normals().data(), mesh->uvs().data(),
                                  mesh->getVertexCount(), mesh->indices().data(), mesh->getIndexCount());
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

RecipeDescriptor *Procgen::getMeshRecipeSchema(const std::string &recipeId) const {
    MeshRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor *schema = MeshRecipeRegistry::instance().descriptor(recipeId);
    return schema ? new RecipeDescriptor(*schema) : nullptr;
}

bool Procgen::applyMeshRecipeDefaults(const std::string &recipeId, Params *params) const {
    if (!params) return false;
    MeshRecipeRegistry::instance().registerBuiltins();
    return MeshRecipeRegistry::instance().applyDefaults(recipeId, *params);
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

    auto recipe = table.addClass<RecipeDescriptor>(
        "ProcgenRecipeSchema",
        std::function<RecipeDescriptor *()>([]() -> RecipeDescriptor * { return nullptr; }), true);
    recipe.addFunc("getId", &RecipeDescriptor::getId);
    recipe.addFunc("getDisplayName", &RecipeDescriptor::getDisplayName);
    recipe.addFunc("getCategory", &RecipeDescriptor::getCategory);
    recipe.addFunc("getParamCount", &RecipeDescriptor::getParamCount);
    recipe.addFunc("getParamKey", &RecipeDescriptor::getParamKey);
    recipe.addFunc("getParamLabel", &RecipeDescriptor::getParamLabel);
    recipe.addFunc("getParamDescription", &RecipeDescriptor::getParamDescription);
    recipe.addFunc("getParamCategory", &RecipeDescriptor::getParamCategory);
    recipe.addFunc("getParamKind", &RecipeDescriptor::getParamKind);
    recipe.addFunc("getParamDefault", &RecipeDescriptor::getParamDefault);
    recipe.addFunc("paramHasMinimum", &RecipeDescriptor::paramHasMinimum);
    recipe.addFunc("paramHasMaximum", &RecipeDescriptor::paramHasMaximum);
    recipe.addFunc("getParamMinimum", &RecipeDescriptor::getParamMinimum);
    recipe.addFunc("getParamMaximum", &RecipeDescriptor::getParamMaximum);
    recipe.addFunc("getParamStep", &RecipeDescriptor::getParamStep);
    recipe.addFunc("isParamAdvanced", &RecipeDescriptor::isParamAdvanced);
    recipe.addFunc("getParamChoiceCount", &RecipeDescriptor::getParamChoiceCount);
    recipe.addFunc("getParamChoice", &RecipeDescriptor::getParamChoice);

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

    auto points = table.addClass<PointSet>("ProcgenPointSet",
                                           std::function<PointSet*()>([]() -> PointSet* { return nullptr; }), true);
    points.addFunc("getCount", &PointSet::getCount);
    points.addFunc("empty", &PointSet::empty);
    points.addFunc("clear", &PointSet::clear);
    points.addFunc("add", &PointSet::add);
    points.addFunc("setPosition", &PointSet::setPosition);
    points.addFunc("getX", &PointSet::getX);
    points.addFunc("getY", &PointSet::getY);
    points.addFunc("getZ", &PointSet::getZ);
    points.addFunc("setNormal", &PointSet::setNormal);
    points.addFunc("getNormalX", &PointSet::getNormalX);
    points.addFunc("getNormalY", &PointSet::getNormalY);
    points.addFunc("getNormalZ", &PointSet::getNormalZ);
    points.addFunc("setYaw", &PointSet::setYaw);
    points.addFunc("getYaw", &PointSet::getYaw);
    points.addFunc("setScale", &PointSet::setScale);
    points.addFunc("getScaleX", &PointSet::getScaleX);
    points.addFunc("getScaleY", &PointSet::getScaleY);
    points.addFunc("getScaleZ", &PointSet::getScaleZ);
    points.addFunc("setDensity", &PointSet::setDensity);
    points.addFunc("getDensity", &PointSet::getDensity);
    points.addFunc("setPointSeed", &PointSet::setPointSeed);
    points.addFunc("getPointSeed", &PointSet::getPointSeed);
    points.addFunc("setFloatAttribute", &PointSet::setFloatAttribute);
    points.addFunc("getFloatAttribute", &PointSet::getFloatAttribute);
    points.addFunc("hasFloatAttribute", &PointSet::hasFloatAttribute);
    points.addFunc("setStringAttribute", &PointSet::setStringAttribute);
    points.addFunc("getStringAttribute", &PointSet::getStringAttribute);
    points.addFunc("hasStringAttribute", &PointSet::hasStringAttribute);

    auto context = table.addClass<ProcgenContext>(
        "ProcgenContext",
        std::function<ProcgenContext*()>([]() -> ProcgenContext* { return nullptr; }), true);
    context.addFunc("getName", &ProcgenContext::getName);
    context.addFunc("getSeed", &ProcgenContext::getSeed);
    context.addFunc("seedFor", &ProcgenContext::seedFor);
    context.addFunc("isActive", &ProcgenContext::isActive);
    context.addFunc("hasFailed", &ProcgenContext::hasFailed);
    context.addFunc("isCacheHit", &ProcgenContext::isCacheHit);
    context.addFunc("getError", &ProcgenContext::getError);
    context.addFunc("getBuildKey", &ProcgenContext::getBuildKey);
    context.addFunc("publish", &ProcgenContext::publish);
    context.addFunc("hasOutput", &ProcgenContext::hasOutput);
    context.addFunc("getOutputCount", &ProcgenContext::getOutputCount);
    context.addFunc("getOutputName", &ProcgenContext::getOutputName);
    context.addFunc("getOutput", &ProcgenContext::getOutput);
    context.addFunc("captureDebug", &ProcgenContext::captureDebug);
    context.addFunc("getDebugStageCount", &ProcgenContext::getDebugStageCount);
    context.addFunc("getDebugStageName", &ProcgenContext::getDebugStageName);
    context.addFunc("getDebugStage", &ProcgenContext::getDebugStage);
    context.addFunc("reuseStage", &ProcgenContext::reuseStage);
    context.addFunc("cacheStage", &ProcgenContext::cacheStage);
    context.addFunc("getStageCacheHitCount", &ProcgenContext::getStageCacheHitCount);
    context.addFunc("getStageCacheMissCount", &ProcgenContext::getStageCacheMissCount);
    context.addFunc("trace", &ProcgenContext::trace);
    context.addFunc("beginTrace", &ProcgenContext::beginTrace);
    context.addFunc("endTrace", &ProcgenContext::endTrace);
    context.addFunc("getOpenTraceCount", &ProcgenContext::getOpenTraceCount);
    context.addFunc("getTraceCount", &ProcgenContext::getTraceCount);
    context.addFunc("getTraceName", &ProcgenContext::getTraceName);
    context.addFunc("getTraceInputCount", &ProcgenContext::getTraceInputCount);
    context.addFunc("getTraceOutputCount", &ProcgenContext::getTraceOutputCount);
    context.addFunc("getTraceMilliseconds", &ProcgenContext::getTraceMilliseconds);
    context.addFunc("fail", &ProcgenContext::fail);
    context.addFunc("abort", &ProcgenContext::abort);

    auto mesh = table.addClass<MeshBuild>(
        "ProcgenMeshBuild", std::function<MeshBuild *()>([]() -> MeshBuild * { return nullptr; }),
        true);
    mesh.addFunc("clear", &MeshBuild::clear);
    mesh.addFunc("appendTransformed", &MeshBuild::appendTransformed);
    mesh.addFunc("setActiveGroup", &MeshBuild::setActiveGroup);
    mesh.addFunc("getGroupCount", &MeshBuild::getGroupCount);
    mesh.addFunc("getGroupName", &MeshBuild::getGroupName);
    mesh.addFunc("getTriangleGroup", &MeshBuild::getTriangleGroup);
    mesh.addFunc("copyGroup", &MeshBuild::copyGroup);
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
    pbr.addFunc("getAlbedo", &PbrTextureSet::getAlbedo);
    pbr.addFunc("getNormal", &PbrTextureSet::getNormal);
    pbr.addFunc("getRoughness", &PbrTextureSet::getRoughness);
    pbr.addFunc("getMetallic", &PbrTextureSet::getMetallic);
    pbr.addFunc("getHeight", &PbrTextureSet::getHeight);
    pbr.addFunc("getAo", &PbrTextureSet::getAo);
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
    cls.addFunc("newPointSet", &Procgen::newPointSet);
    cls.addFunc("sampleGrid", &Procgen::sampleGrid);
    cls.addFunc("filterHeight", &Procgen::filterHeight);
    cls.addFunc("filterDensity", &Procgen::filterDensity);
    cls.addFunc("filterBox", &Procgen::filterBox);
    cls.addFunc("excludeBox", &Procgen::excludeBox);
    cls.addFunc("filterSlope", &Procgen::filterSlope);
    cls.addFunc("filterPolygon", &Procgen::filterPolygon);
    cls.addFunc("excludePolygon", &Procgen::excludePolygon);
    cls.addFunc("filterSplineDistance", &Procgen::filterSplineDistance);
    cls.addFunc("excludeRadius", &Procgen::excludeRadius);
    cls.addFunc("jitterPoints", &Procgen::jitterPoints);
    cls.addFunc("selfPrune", &Procgen::selfPrune);
    cls.addFunc("projectToHeightmap", &Procgen::projectToHeightmap);
    cls.addFunc("sampleSpline", &Procgen::sampleSpline);
    cls.addFunc("deriveSeed", &Procgen::deriveSeed);
    cls.addFunc("beginSystem", &Procgen::beginSystem);
    cls.addFunc("beginCachedSystem", &Procgen::beginCachedSystem);
    cls.addFunc("commitSystem", &Procgen::commitSystem);
    cls.addFunc("abortSystem", &Procgen::abortSystem);
    cls.addFunc("removeSystem", &Procgen::removeSystem);
    cls.addFunc("hasSystem", &Procgen::hasSystem);
    cls.addFunc("getSystemRevision", &Procgen::getSystemRevision);
    cls.addFunc("getSystemSeed", &Procgen::getSystemSeed);
    cls.addFunc("getSystemBuildKey", &Procgen::getSystemBuildKey);
    cls.addFunc("getSystemOutputCount", &Procgen::getSystemOutputCount);
    cls.addFunc("getSystemOutputName", &Procgen::getSystemOutputName);
    cls.addFunc("getSystemOutput", &Procgen::getSystemOutput);
    cls.addFunc("getSystemDebugStageCount", &Procgen::getSystemDebugStageCount);
    cls.addFunc("getSystemDebugStageName", &Procgen::getSystemDebugStageName);
    cls.addFunc("getSystemDebugStage", &Procgen::getSystemDebugStage);
    cls.addFunc("getPreviousSystemDebugStage", &Procgen::getPreviousSystemDebugStage);
    cls.addFunc("getPreviousSystemRevision", &Procgen::getPreviousSystemRevision);
    cls.addFunc("getSystemDebugReport", &Procgen::getSystemDebugReport);
    cls.addFunc("getSystemDebugDiffReport", &Procgen::getSystemDebugDiffReport);
    cls.addFunc("generate", &Procgen::generate);
    cls.addFunc("generateTo", &Procgen::generateTo);
    cls.addFunc("applyToLayer", &Procgen::applyToLayer);
    cls.addFunc("setPaletteGid", &Procgen::setPaletteGid);
    cls.addFunc("getPaletteGid", &Procgen::getPaletteGid);
    cls.addFunc("getAlgorithmCount", &Procgen::getAlgorithmCount);
    cls.addFunc("getAlgorithmId", &Procgen::getAlgorithmId);
    cls.addFunc("hasAlgorithm", &Procgen::hasAlgorithm);
    cls.addFunc("getAlgorithmSchema", &Procgen::getAlgorithmSchema);
    cls.addFunc("getAlgorithmDisplayName", &Procgen::getAlgorithmDisplayName);
    cls.addFunc("getAlgorithmCategory", &Procgen::getAlgorithmCategory);
    cls.addFunc("getAlgorithmParamCount", &Procgen::getAlgorithmParamCount);
    cls.addFunc("getAlgorithmParamKey", &Procgen::getAlgorithmParamKey);
    cls.addFunc("getAlgorithmParamLabel", &Procgen::getAlgorithmParamLabel);
    cls.addFunc("getAlgorithmParamDescription", &Procgen::getAlgorithmParamDescription);
    cls.addFunc("getAlgorithmParamCategory", &Procgen::getAlgorithmParamCategory);
    cls.addFunc("getAlgorithmParamKind", &Procgen::getAlgorithmParamKind);
    cls.addFunc("getAlgorithmParamDefault", &Procgen::getAlgorithmParamDefault);
    cls.addFunc("algorithmParamHasMinimum", &Procgen::algorithmParamHasMinimum);
    cls.addFunc("algorithmParamHasMaximum", &Procgen::algorithmParamHasMaximum);
    cls.addFunc("getAlgorithmParamMinimum", &Procgen::getAlgorithmParamMinimum);
    cls.addFunc("getAlgorithmParamMaximum", &Procgen::getAlgorithmParamMaximum);
    cls.addFunc("getAlgorithmParamStep", &Procgen::getAlgorithmParamStep);
    cls.addFunc("isAlgorithmParamAdvanced", &Procgen::isAlgorithmParamAdvanced);
    cls.addFunc("getAlgorithmParamChoiceCount", &Procgen::getAlgorithmParamChoiceCount);
    cls.addFunc("getAlgorithmParamChoice", &Procgen::getAlgorithmParamChoice);
    cls.addFunc("applyAlgorithmDefaults", &Procgen::applyAlgorithmDefaults);
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
    cls.addFunc("getTextureRecipeSchema", &Procgen::getTextureRecipeSchema);
    cls.addFunc("applyTextureRecipeDefaults", &Procgen::applyTextureRecipeDefaults);
    cls.addFunc("generatePbrMaterial", &Procgen::generatePbrMaterial);
    cls.addFunc("getPbrRecipeCount", &Procgen::getPbrRecipeCount);
    cls.addFunc("getPbrRecipeId", &Procgen::getPbrRecipeId);
    cls.addFunc("hasPbrRecipe", &Procgen::hasPbrRecipe);
    cls.addFunc("getPbrRecipeSchema", &Procgen::getPbrRecipeSchema);
    cls.addFunc("applyPbrRecipeDefaults", &Procgen::applyPbrRecipeDefaults);
    cls.addFunc("buildMesh", &Procgen::buildMesh);
    cls.addFunc("uploadMesh", &Procgen::uploadMesh);
    cls.addFunc("generateMesh", &Procgen::generateMesh);
    cls.addFunc("getMeshRecipeCount", &Procgen::getMeshRecipeCount);
    cls.addFunc("getMeshRecipeId", &Procgen::getMeshRecipeId);
    cls.addFunc("hasMeshRecipe", &Procgen::hasMeshRecipe);
    cls.addFunc("getMeshRecipeSchema", &Procgen::getMeshRecipeSchema);
    cls.addFunc("applyMeshRecipeDefaults", &Procgen::applyMeshRecipeDefaults);
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
