#include "procgen/Procgen.h"
#include "procgen/ProcgenLive.h"
#include "procgen/ProcgenScriptSupport.h"

#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "common/SquirrelBinding.h"
#include "procgen/BiomeScript.h"
#include "procgen/GridMeshGraphScript.h"
#include "procgen/ModuleAssemblyScript.h"
#include "procgen/PointGraphScript.h"
#include "procgen/ProcgenCapabilities.h"
#include "procgen/ProcgenScriptObjects.h"
#include "procgen/RuntimeGenerationScript.h"
#include "procgen/ScriptGeneratorHost.h"
#include "procgen/ShapeGrammarScript.h"
#include "procgen/algorithms/HexTerrainBindings.h"
#include "procgen/mesh/DynamicMeshUvPaintSession.h"
#include "procgen/mesh/MeshAdhereLive.h"
#include "procgen/mesh/MeshMerge.h"
#include "procgen/mesh/MeshModifierGraphScript.h"

#include "image/ImageData.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/GtsMeshSplitter.h"
#include "procgen/GtsTerrainExportSettings.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/GtsTerrainLodRuntime.h"
#include "procgen/JsonExport.h"
#include "procgen/PcgFrameRateManager.h"
#include "procgen/PcgMeshLod.h"
#include "procgen/PcgMeshLodBackup.h"
#include "procgen/PcgTaskQueue.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/RoguelikeGenerator.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainCurveTexture.h"
#include "procgen/heightmap/TerrainFile.h"
#include "procgen/heightmap/TerrainGrassAdapter.h"
#include "procgen/heightmap/TerrainImageAdapter.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/house/HouseGen.h"
#include "procgen/shaders/terrain_material_compat_frag_spv.inc"
#include "procgen/shaders/terrain_water_compat_frag_spv.inc"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"

#include "data/ByteData.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <any>
#include <charconv>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace eve::procgen {

struct Procgen::OwnershipState {
    eve::script::RuntimeObjectRegistry<OutputSpec, ProcgenOutputHandleTag>             outputs;
    eve::script::RuntimeObjectRegistry<PointSet, ProcgenPointSetHandleTag>             points;
    eve::script::RuntimeObjectRegistry<TerrainSampler, ProcgenTerrainSamplerHandleTag> samplers;
    eve::script::RuntimeObjectRegistry<Heightmap, ProcgenHeightmapHandleTag>           heightmaps;
    eve::script::RuntimeObjectRegistry<CloudField, ProcgenCloudFieldHandleTag>         clouds;
    eve::script::RuntimeObjectRegistry<CloudShadow, ProcgenCloudShadowHandleTag>       shadows;
    eve::script::RuntimeObjectRegistry<PbrTextureSet, ProcgenPbrMaterialHandleTag>     pbr;
    eve::script::RuntimeObjectRegistry<MeshBuild, ProcgenMeshBuildHandleTag>           meshes;
    eve::script::RuntimeObjectRegistry<image::ImageData, ProcgenImageHandleTag>        images;
    eve::script::RuntimeObjectRegistry<image::ImageData, ProcgenNormalImageHandleTag>  normalImages;
};

Module_IMPL(Procgen, new Procgen());

Procgen::Procgen() : ownership_(std::make_unique<OwnershipState>()) {
    publishLiveProcgen(this);
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

Procgen::~Procgen() { clearLiveProcgen(this); }

eve::Result<ProcgenSpatialDataHandleRef> Procgen::pointDataHandle(ProcgenPointSetHandleRef points) {
    auto view = resolvePointSet(points);
    if (!view.isBound())
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "pointData point-set handle is stale", "points", {}, "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::fromPoints(*view)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::boxVolumeHandle(float minX, float minY, float minZ, float maxX,
                                                                  float maxY, float maxZ) {
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::box(minX, minY, minZ, maxX, maxY, maxZ)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::sphereVolumeHandle(float x, float y, float z, float radius) {
    if (radius <= 0.f)
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "sphereVolume radius must be positive",
                                   "radius", {}, "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::sphere(x, y, z, radius)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::polygonVolumeHandle(ProcgenPointSetHandleRef controlPoints,
                                                                      float minY, float maxY) {
    auto points = resolvePointSet(controlPoints);
    if (!points.isBound() || points->getCount() < 3 || !std::isfinite(minY) || !std::isfinite(maxY))
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "polygonVolume requires a live set with at least three points and finite heights",
                                   "controlPoints", {}, "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::polygon(*points, minY, maxY)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::splineDataHandle(ProcgenPointSetHandleRef controlPoints,
                                                                   float                    radius) {
    auto view = resolvePointSet(controlPoints);
    if (!view.isBound() || view->getCount() < 2 || radius < 0.f)
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "splineData requires a live set with at least two points and non-negative radius", "controlPoints", {},
            "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::spline(*view, radius)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::heightfieldDataHandle(ProcgenHeightmapHandleRef heightmap,
                                                                        float originX, float originZ, float cellSize,
                                                                        float heightScale) {
    auto view = resolveHeightmap(heightmap);
    if (!view.isBound() || view->getWidth() <= 0 || view->getHeight() <= 0 || cellSize <= 0.f)
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "heightfieldData requires a live non-empty heightmap and positive cell size", "heightmap", {},
            "procgen.squirrel"));
    return spatialData_.emplace(
        std::make_unique<SpatialData>(SpatialData::heightfield(*view, originX, originZ, cellSize, heightScale)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::textureMaskDataHandle(ProcgenHeightmapHandleRef values, float originX,
                                                                        float originZ, float cellSize, float minValue,
                                                                        float maxValue, float minY, float maxY) {
    auto view = resolveHeightmap(values);
    if (!view.isBound() || view->getWidth() <= 0 || view->getHeight() <= 0 || cellSize <= 0.f)
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "textureMaskData requires a live non-empty scalar map and positive cell size",
                                   "values", {}, "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(
        SpatialData::textureMask(*view, originX, originZ, cellSize, minValue, maxValue, minY, maxY)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::meshSurfaceDataHandle(ProcgenMeshBuildHandleRef mesh,
                                                                        float                     tolerance) {
    auto view = resolveMeshBuild(mesh);
    if (!view.isBound() || view->getVertexCount() < 3 || view->getIndexCount() < 3 || tolerance < 0.f)
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "meshSurfaceData requires a live triangle mesh and non-negative tolerance", "mesh",
                                   {}, "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::meshSurface(*view, tolerance)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::unionSpatialHandle(ProcgenSpatialDataHandleRef left,
                                                                     ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left);
    auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "unionSpatial input handle is stale", "spatial", {}, "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::unite(*a, *b)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::intersectSpatialHandle(ProcgenSpatialDataHandleRef left,
                                                                         ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left);
    auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "intersectSpatial input handle is stale",
                                   "spatial", {}, "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::intersect(*a, *b)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::differenceSpatialHandle(ProcgenSpatialDataHandleRef left,
                                                                          ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left);
    auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return eve::Result<ProcgenSpatialDataHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "differenceSpatial input handle is stale",
                                   "spatial", {}, "procgen.squirrel"));
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::subtract(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleSpatialHandle(ProcgenSpatialDataHandleRef spatial, float spacing,
                                                                   uint32_t seed, float jitter) {
    auto view = resolveSpatialData(spatial);
    if (!view.isBound() || spacing <= 0.f)
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "sampleSpatial requires live spatial data and positive spacing", "spatial", {}, "procgen.squirrel"));
    return ownership_->points.emplace(std::make_unique<PointSet>(view->sample(spacing, seed, jitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSpatialHandle(ProcgenPointSetHandleRef    input,
                                                                   ProcgenSpatialDataHandleRef spatial, bool invert) {
    auto points = resolvePointSet(input);
    auto domain = resolveSpatialData(spatial);
    if (!points.isBound() || !domain.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "filterSpatial input handle is stale", "input", {}, "procgen.squirrel"));
    return ownership_->points.emplace(std::make_unique<PointSet>(domain->filter(*points, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::projectToSpatialHandle(ProcgenPointSetHandleRef    input,
                                                                      ProcgenSpatialDataHandleRef spatial) {
    auto points = resolvePointSet(input);
    auto domain = resolveSpatialData(spatial);
    if (!points.isBound() || !domain.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "projectToSpatial input handle is stale", "input",
                                   {}, "procgen.squirrel"));
    return ownership_->points.emplace(std::make_unique<PointSet>(domain->project(*points)));
}

eve::script::Borrowed<SpatialData> Procgen::resolveSpatialData(ProcgenSpatialDataHandleRef reference) noexcept {
    return spatialData_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenSpatialDataHandleRef reference) { return spatialData_.erase(reference); }

bool Procgen::isStale(ProcgenSpatialDataHandleRef reference) const noexcept { return spatialData_.isStale(reference); }

eve::Result<ProcgenRuntimeGenerationHandleRef> Procgen::newRuntimeGenerationHandle(uint32_t worldSeed) {
    return runtimeGenerations_.emplace(std::make_unique<RuntimeGeneration>(worldSeed));
}

eve::Result<ProcgenLSystemHandleRef> Procgen::newLSystemHandle() {
    return lsystems_.emplace(std::make_unique<LSystem>());
}

eve::script::Borrowed<RuntimeGeneration> Procgen::resolveRuntimeGeneration(
    ProcgenRuntimeGenerationHandleRef reference) noexcept {
    return runtimeGenerations_.resolve(reference);
}

eve::script::Borrowed<PointGraph> Procgen::resolvePointGraph(ProcgenPointGraphHandleRef reference) noexcept {
    return pointGraphs_.resolve(reference);
}

eve::script::Borrowed<MeshModifierGraph> Procgen::resolveMeshModifierGraph(
    ProcgenMeshModifierGraphHandleRef reference) noexcept {
    return meshModifierGraphs_.resolve(reference);
}

eve::script::Borrowed<MeshDeformationSession> Procgen::resolveMeshDeformationSession(
    ProcgenMeshDeformationSessionHandleRef reference) noexcept {
    return meshDeformationSessions_.resolve(reference);
}

eve::script::Borrowed<DynamicMeshUvPaintSession> Procgen::resolveDynamicMeshUvPaintSession(
    ProcgenDynamicMeshUvPaintSessionHandleRef reference) noexcept {
    return dynamicMeshUvPaintSessions_.resolve(reference);
}

eve::script::Borrowed<SplinePath> Procgen::resolveSplinePath(ProcgenSplinePathHandleRef reference) noexcept {
    return splinePaths_.resolve(reference);
}

eve::script::Borrowed<GridGraph> Procgen::resolveGridGraph(ProcgenGridGraphHandleRef reference) noexcept {
    return gridGraphs_.resolve(reference);
}

eve::script::Borrowed<MeshGraph> Procgen::resolveMeshGraph(ProcgenMeshGraphHandleRef reference) noexcept {
    return meshGraphs_.resolve(reference);
}

eve::script::Borrowed<BiomeRules> Procgen::resolveBiomeRules(ProcgenBiomeRulesHandleRef reference) noexcept {
    return biomeRules_.resolve(reference);
}

eve::script::Borrowed<ShapeGrammar> Procgen::resolveShapeGrammar(ProcgenShapeGrammarHandleRef reference) noexcept {
    return shapeGrammars_.resolve(reference);
}

eve::script::Borrowed<LSystem> Procgen::resolveLSystem(ProcgenLSystemHandleRef reference) noexcept {
    return lsystems_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenRuntimeGenerationHandleRef reference) {
    return runtimeGenerations_.erase(reference);
}
eve::Result<void> Procgen::release(ProcgenPointGraphHandleRef reference) { return pointGraphs_.erase(reference); }
eve::Result<void> Procgen::release(ProcgenMeshModifierGraphHandleRef reference) {
    return meshModifierGraphs_.erase(reference);
}
eve::Result<void> Procgen::release(ProcgenMeshDeformationSessionHandleRef reference) {
    return meshDeformationSessions_.erase(reference);
}
eve::Result<void> Procgen::release(ProcgenDynamicMeshUvPaintSessionHandleRef reference) {
    return dynamicMeshUvPaintSessions_.erase(reference);
}
eve::Result<void> Procgen::release(ProcgenSplinePathHandleRef reference) { return splinePaths_.erase(reference); }
eve::Result<void> Procgen::release(ProcgenGridGraphHandleRef reference) { return gridGraphs_.erase(reference); }
eve::Result<void> Procgen::release(ProcgenMeshGraphHandleRef reference) { return meshGraphs_.erase(reference); }
eve::Result<void> Procgen::release(ProcgenBiomeRulesHandleRef reference) { return biomeRules_.erase(reference); }
eve::Result<void> Procgen::release(ProcgenShapeGrammarHandleRef reference) { return shapeGrammars_.erase(reference); }
eve::Result<void> Procgen::release(ProcgenLSystemHandleRef reference) { return lsystems_.erase(reference); }

bool Procgen::isStale(ProcgenRuntimeGenerationHandleRef reference) const noexcept {
    return runtimeGenerations_.isStale(reference);
}
bool Procgen::isStale(ProcgenPointGraphHandleRef reference) const noexcept { return pointGraphs_.isStale(reference); }
bool Procgen::isStale(ProcgenMeshModifierGraphHandleRef reference) const noexcept {
    return meshModifierGraphs_.isStale(reference);
}
bool Procgen::isStale(ProcgenMeshDeformationSessionHandleRef reference) const noexcept {
    return meshDeformationSessions_.isStale(reference);
}
bool Procgen::isStale(ProcgenDynamicMeshUvPaintSessionHandleRef reference) const noexcept {
    return dynamicMeshUvPaintSessions_.isStale(reference);
}
bool Procgen::isStale(ProcgenSplinePathHandleRef reference) const noexcept { return splinePaths_.isStale(reference); }
bool Procgen::isStale(ProcgenGridGraphHandleRef reference) const noexcept { return gridGraphs_.isStale(reference); }
bool Procgen::isStale(ProcgenMeshGraphHandleRef reference) const noexcept { return meshGraphs_.isStale(reference); }
bool Procgen::isStale(ProcgenBiomeRulesHandleRef reference) const noexcept { return biomeRules_.isStale(reference); }
bool Procgen::isStale(ProcgenShapeGrammarHandleRef reference) const noexcept {
    return shapeGrammars_.isStale(reference);
}
bool Procgen::isStale(ProcgenLSystemHandleRef reference) const noexcept { return lsystems_.isStale(reference); }

uint32_t Procgen::deriveSeed(uint32_t parent, const std::string& scope) const {
    return eve::procgen::deriveSeed(parent, scope);
}

eve::Result<ProcgenContextHandleRef> Procgen::beginSystemHandle(const std::string& name, uint32_t seed) {
    Procgen* module = Procgen::create();
    module->lastError_.clear();

    if (name.empty()) {
        module->lastError_ = "beginSystem: name is empty";
        return eve::Result<ProcgenContextHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, module->lastError_, "context", {}, "procgen.squirrel"));
    }
    auto       context  = std::make_unique<ProcgenContext>(name, seed);
    const auto previous = module->systems_.find(name);
    if (previous != module->systems_.end()) context->stageCache_ = previous->second.stageCache;
    return module->contexts_.emplace(std::move(context));
}

eve::Result<ProcgenContextHandleRef> Procgen::beginCachedSystemHandle(const std::string& name, uint32_t seed,
                                                                      const std::string& buildKey) {
    Procgen* module = Procgen::create();
    module->lastError_.clear();
    if (name.empty()) {
        module->lastError_ = "beginCachedSystem: name is empty";
        return eve::Result<ProcgenContextHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, module->lastError_, "context", {}, "procgen.squirrel"));
    }
    if (buildKey.empty()) {
        module->lastError_ = "beginCachedSystem: build key is empty";
        return eve::Result<ProcgenContextHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, module->lastError_, "buildKey", {}, "procgen.squirrel"));
    }
    const uint32_t normalizedSeed = seed ? seed : 1u;
    const auto     found          = module->systems_.find(name);
    const bool     cacheHit =
        found != module->systems_.end() && found->second.seed == normalizedSeed && found->second.buildKey == buildKey;
    auto context = std::make_unique<ProcgenContext>(name, normalizedSeed, buildKey, cacheHit);
    if (found != module->systems_.end()) context->stageCache_ = found->second.stageCache;
    return module->contexts_.emplace(std::move(context));
}

eve::script::Borrowed<ProcgenContext> Procgen::resolve(ProcgenContextHandleRef reference) noexcept {
    Procgen* module = liveProcgen();
    if (!module) return {};
    return module->contexts_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenContextHandleRef reference) {
    Procgen* module = liveProcgen();
    if (!module)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded", "context", {}, "procgen.squirrel"));
    return module->contexts_.erase(reference);
}

bool Procgen::isStale(ProcgenContextHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = liveProcgen();
    return !module || module->contexts_.isStale(reference);
}

eve::Result<void> Procgen::commitSystem(ProcgenContextHandleRef reference) {
    auto view = Procgen::resolve(reference);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                 "commitSystem context handle is stale", "context", {},
                                                                 "procgen.squirrel"));
    ProcgenContext* context = view.get();
    if (!context->isActive())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "commitSystem: transaction is closed", "context", {},
                                                                 "procgen.squirrel"));
    if (context->hasFailed()) {
        const std::string error = context->getError();
        context->close();
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "commitSystem: " + error,
                                                                 "context", {}, "procgen.squirrel"));
    }
    if (!context->openTraces_.empty()) {
        const std::string trace = context->openTraces_.back().name;
        context->close();
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "commitSystem: unfinished trace '" + trace + "'",
                                                                 "context", {}, "procgen.squirrel"));
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
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::abortSystem(ProcgenContextHandleRef reference) {
    auto view = Procgen::resolve(reference);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                 "abortSystem context handle is stale", "context", {},
                                                                 "procgen.squirrel"));
    view->abort();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::removeSystem(const std::string& name) {
    previousSystems_.erase(name);
    if (systems_.erase(name) == 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "procgen system was not committed", "system", {}, "procgen.squirrel"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool Procgen::hasSystem(const std::string& name) const { return systems_.find(name) != systems_.end(); }

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
    if (found == systems_.end() || index < 0 || index >= int(found->second.outputOrder.size())) return {};
    return found->second.outputOrder[size_t(index)];
}

int Procgen::getSystemDebugStageCount(const std::string& name) const {
    const auto found = systems_.find(name);
    return found == systems_.end() ? 0 : int(found->second.debugStageOrder.size());
}

std::string Procgen::getSystemDebugStageName(const std::string& name, int index) const {
    const auto found = systems_.find(name);
    if (found == systems_.end() || index < 0 || index >= int(found->second.debugStageOrder.size())) return {};
    return found->second.debugStageOrder[size_t(index)];
}

eve::Result<ProcgenPointSetHandleRef> Procgen::getSystemOutputHandle(const std::string& name,
                                                                     const std::string& outputName) const {
    const auto system = systems_.find(name);
    if (system == systems_.end())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "procgen system is not committed", "system", {}, "procgen.squirrel"));
    const auto output = system->second.outputs.find(outputName);
    if (output == system->second.outputs.end())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "procgen system output was not found", "output", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(output->second));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::getSystemDebugStageHandle(const std::string& name,
                                                                         const std::string& stageName) const {
    const auto system = systems_.find(name);
    if (system == systems_.end())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "procgen system is not committed", "system", {}, "procgen.squirrel"));
    const auto stage = system->second.debugStages.find(stageName);
    if (stage == system->second.debugStages.end())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "procgen debug stage was not found", "stage", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(stage->second));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::getPreviousSystemDebugStageHandle(const std::string& name,
                                                                                 const std::string& stageName) const {
    const auto system = previousSystems_.find(name);
    if (system == previousSystems_.end())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "procgen system has no previous revision", "system",
                                   {}, "procgen.squirrel"));
    const auto stage = system->second.debugStages.find(stageName);
    if (stage == system->second.debugStages.end())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "previous procgen debug stage was not found", "stage",
                                   {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(stage->second));
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
        report << "\n  output " << outputName << " points=" << snapshot.outputs.at(outputName).getCount();
    }
    for (const auto& stageName : snapshot.debugStageOrder) {
        report << "\n  debug " << stageName << " points=" << snapshot.debugStages.at(stageName).getCount();
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
        const int  oldCount     = oldStage == previous->second.debugStages.end() ? 0 : oldStage->second.getCount();
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

bool Procgen::runGenerate(const std::string& algorithmId, const Params& params, Grid2D& out) {
    lastError_.clear();
    GeneratorRegistry::instance().registerBuiltins();
    if (!GeneratorRegistry::instance().generate(algorithmId, params, out, lastError_)) {
        if (lastError_.empty()) lastError_ = "generate failed";
        return false;
    }
    return true;
}

eve::Result<ProcgenGridHandleRef> Procgen::generateHandle(const std::string&     algorithmId,
                                                          ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return eve::Result<ProcgenGridHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "generate parameters handle is stale", "params", {}, "procgen.squirrel"));
    auto grid = std::make_unique<Grid2D>();
    if (!runGenerate(algorithmId, *input, *grid))
        return eve::Result<ProcgenGridHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, lastError_.empty() ? "generate failed" : lastError_,
                                   "algorithm", {}, "procgen.squirrel"));
    return ownProcgenObject(grids_, std::move(grid));
}

eve::Result<void> Procgen::generateTo(const std::string& algorithmId, ProcgenParamsHandleRef params,
                                      ProcgenOutputHandleRef output) {
    auto paramsView = Procgen::resolve(params);
    auto outputView = resolveOutput(output);
    if (!paramsView.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                 "generateTo parameter handle is stale", "params", {},
                                                                 "procgen.squirrel"));
    if (!outputView.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "generateTo output handle is stale", "output", {}, "procgen.squirrel"));
    Grid2D grid;
    if (!runGenerate(algorithmId, *paramsView, grid))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                                                 lastError_.empty() ? "generateTo failed" : lastError_,
                                                                 "algorithm", {}, "procgen.squirrel"));

    const std::string target = outputView->getTarget();
    if (target == "grid") {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "generateTo: target 'grid' has no sink; use generateHandle()",
            "output.target", {}, "procgen.squirrel"));
    }
    if (target == "tilelayer") {
        auto* layer = outputView->getLayer();
        if (!layer)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "generateTo: tilelayer target has no layer",
                                                                     "output.layer", {}, "procgen.squirrel"));
        if (!palettes_.applyToLayer(grid, outputView->getPalette(), layer, &lastError_))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, lastError_, "output", {}, "procgen.squirrel"));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    if (target == "json") {
        if (!writeGridJson(grid, outputView->getPath(), &lastError_))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, lastError_, "output.path", {}, "procgen.squirrel"));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "generateTo: unknown target '" + target + "' (use grid|tilelayer|json)",
        "output.target", {}, "procgen.squirrel"));
}

eve::Result<void> Procgen::applyToLayer(ProcgenGridHandleRef grid, const std::string& palette, map::TileLayer& layer) {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "applyToLayer grid handle is stale", "grid", {}, "procgen.squirrel"));
    if (!palettes_.applyToLayer(*view, palette, &layer, &lastError_))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, lastError_, "palette", {}, "procgen.squirrel"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void Procgen::setPaletteGid(const std::string& palette, const std::string& semantic, int gid) {
    palettes_.setGid(palette, semantic, gid);
}

int Procgen::getPaletteGid(const std::string& palette, const std::string& semantic) const {
    return palettes_.getGid(palette, semantic);
}

namespace {

const ParamDescriptor* algorithmParam(const std::string& algorithmId, int index) {
    const GeneratorDescriptor* descriptor = GeneratorRegistry::instance().descriptor(algorithmId);
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

bool Procgen::hasAlgorithm(const std::string& algorithmId) const {
    return GeneratorRegistry::instance().has(algorithmId);
}

eve::Result<RecipeDescriptor> Procgen::getAlgorithmSchema(const std::string& algorithmId) const {
    const RecipeDescriptor* schema = GeneratorRegistry::instance().descriptor(algorithmId);
    if (!schema)
        return eve::Result<RecipeDescriptor>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "algorithm schema was not found", "algorithm", {}, "procgen.squirrel"));
    return eve::Result<RecipeDescriptor>::success(*schema);
}

std::string Procgen::getAlgorithmDisplayName(const std::string& algorithmId) const {
    const GeneratorDescriptor* descriptor = GeneratorRegistry::instance().descriptor(algorithmId);
    return descriptor ? descriptor->displayName : std::string{};
}

std::string Procgen::getAlgorithmCategory(const std::string& algorithmId) const {
    const GeneratorDescriptor* descriptor = GeneratorRegistry::instance().descriptor(algorithmId);
    return descriptor ? descriptor->category : std::string{};
}

int Procgen::getAlgorithmParamCount(const std::string& algorithmId) const {
    const GeneratorDescriptor* descriptor = GeneratorRegistry::instance().descriptor(algorithmId);
    return descriptor ? int(descriptor->params.size()) : 0;
}

std::string Procgen::getAlgorithmParamKey(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? param->key : std::string{};
}

std::string Procgen::getAlgorithmParamLabel(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? param->displayName : std::string{};
}

std::string Procgen::getAlgorithmParamDescription(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? param->description : std::string{};
}

std::string Procgen::getAlgorithmParamCategory(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? param->category : std::string{};
}

std::string Procgen::getAlgorithmParamKind(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
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

std::string Procgen::getAlgorithmParamDefault(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? param->defaultValue : std::string{};
}

bool Procgen::algorithmParamHasMinimum(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param && param->hasMinimum;
}

bool Procgen::algorithmParamHasMaximum(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param && param->hasMaximum;
}

float Procgen::getAlgorithmParamMinimum(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? float(param->minimum) : 0.f;
}

float Procgen::getAlgorithmParamMaximum(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? float(param->maximum) : 0.f;
}

float Procgen::getAlgorithmParamStep(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? float(param->step) : 0.f;
}

bool Procgen::isAlgorithmParamAdvanced(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param && param->advanced;
}

int Procgen::getAlgorithmParamChoiceCount(const std::string& algorithmId, int index) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, index);
    return param ? int(param->choices.size()) : 0;
}

std::string Procgen::getAlgorithmParamChoice(const std::string& algorithmId, int paramIndex, int choiceIndex) const {
    const ParamDescriptor* param = algorithmParam(algorithmId, paramIndex);
    if (!param || choiceIndex < 0 || choiceIndex >= int(param->choices.size())) return {};
    return param->choices[size_t(choiceIndex)];
}

eve::Result<void> Procgen::applyAlgorithmDefaults(const std::string& algorithmId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                 "algorithm default parameters handle is stale",
                                                                 "params", {}, "procgen.squirrel"));
    if (!GeneratorRegistry::instance().applyDefaults(algorithmId, *view))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "algorithm schema was not found", "algorithm", {}, "procgen.squirrel"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::autotileGrid(ProcgenGridHandleRef grid) {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "autotileGrid grid handle is stale", "grid", {}, "procgen.squirrel"));
    if (!eve::procgen::autotileGridInPlace(*view))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "autotileGrid failed", "grid", {}, "procgen.squirrel"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

uint32_t Procgen::randomSeed() { return eve::procgen::randomSeedValue(); }

eve::Result<std::string> Procgen::gridToJson(ProcgenGridHandleRef grid) const {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "gridToJson grid handle is stale", "grid", {}, "procgen.squirrel"));
    return eve::Result<std::string>::success(eve::procgen::gridToJson(*view));
}

eve::Result<ProcgenImageHandleRef> Procgen::generateImageHandle(const std::string&     recipeId,
                                                                ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return eve::Result<ProcgenImageHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "generateImage parameters handle is stale",
                                   "params", {}, "procgen.squirrel"));
    lastError_.clear();
    TextureRecipeRegistry::instance().registerBuiltins();
    auto image = TextureRecipeRegistry::instance().generate(recipeId, *input, lastError_);
    if (!image)
        return eve::Result<ProcgenImageHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, lastError_.empty() ? "generateImage failed" : lastError_, "recipe", {},
            "procgen.squirrel"));
    return ownProcgenObject(ownership_->images, std::move(image));
}

eve::script::Borrowed<image::ImageData> Procgen::resolve(ProcgenImageHandleRef reference) noexcept {
    Procgen* module = liveProcgen();
    return module ? module->ownership_->images.resolve(reference) : eve::script::Borrowed<image::ImageData>();
}

eve::Result<void> Procgen::release(ProcgenImageHandleRef reference) {
    Procgen* module = liveProcgen();
    if (!module)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded", "image", {}, "procgen.squirrel"));
    return module->ownership_->images.erase(reference);
}

bool Procgen::isStale(ProcgenImageHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = liveProcgen();
    return !module || module->ownership_->images.isStale(reference);
}

eve::Result<ProcgenNormalImageHandleRef> Procgen::generateNormalImageHandle(const std::string&     recipeId,
                                                                            ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return eve::Result<ProcgenNormalImageHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "generateNormalImage parameters handle is stale",
                                   "params", {}, "procgen.squirrel"));
    auto image = generateImageHandle(recipeId, params);
    if (!image)
        return eve::Result<ProcgenNormalImageHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, image.status().describe(), "recipe", {}, "procgen.squirrel"));
    const auto imageRef = std::move(image).takeValue();
    auto       albedo   = ownership_->images.resolve(imageRef);
    if (!albedo.isBound()) {
        ownership_->images.erase(imageRef).ignore("release unresolvable temporary albedo image");
        return eve::Result<ProcgenNormalImageHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "generated albedo image handle is stale", "image",
                                   {}, "procgen.squirrel"));
    }
    const int          w  = albedo->getWidth();
    const int          h  = albedo->getHeight();
    auto*              px = static_cast<const uint8_t*>(albedo->getData());
    std::vector<float> height(size_t(w * h));
    for (int i = 0; i < w * h; ++i) {
        const size_t o    = size_t(i) * 4u;
        height[size_t(i)] = (float(px[o]) * 0.299f + float(px[o + 1]) * 0.587f + float(px[o + 2]) * 0.114f) / 255.f;
    }
    const bool  seamless = input->getInt("seamless", 1) != 0;
    const float strength = input->getFloat("normalStrength", 4.f);
    auto        normal   = heightToNormalImage(height, w, h, strength, seamless);
    ownership_->images.erase(imageRef).ignore("release temporary albedo image");
    if (!normal)
        return eve::Result<ProcgenNormalImageHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "generateNormalImage failed", "recipe", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->normalImages, std::move(normal));
}

eve::script::Borrowed<image::ImageData> Procgen::resolve(ProcgenNormalImageHandleRef reference) noexcept {
    Procgen* module = liveProcgen();
    return module ? module->ownership_->normalImages.resolve(reference) : eve::script::Borrowed<image::ImageData>();
}

eve::Result<void> Procgen::release(ProcgenNormalImageHandleRef reference) {
    Procgen* module = liveProcgen();
    if (!module)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                 "Procgen module is no longer loaded", "normalImage",
                                                                 {}, "procgen.squirrel"));
    return module->ownership_->normalImages.erase(reference);
}

bool Procgen::isStale(ProcgenNormalImageHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = liveProcgen();
    return !module || module->ownership_->normalImages.isStale(reference);
}

eve::script::Borrowed<graphics::Texture> Procgen::generateTextureBorrowed(const std::string&     recipeId,
                                                                          ProcgenParamsHandleRef params,
                                                                          graphics::Graphics*    gfx) {
    auto input = Procgen::resolve(params);
    if (!input.isBound() || !gfx) return {};
    auto generated = generateImageHandle(recipeId, params);
    if (!generated) return {};
    const auto imageRef = std::move(generated).takeValue();
    auto       image    = ownership_->images.resolve(imageRef);
    if (!image.isBound()) {
        ownership_->images.erase(imageRef).ignore("release unresolvable temporary image");
        return {};
    }
    const bool         seamless = input->getInt("seamless", 1) != 0;
    graphics::Texture* texture  = gfx->newTexture(image->getWidth(), image->getHeight(),
                                                  static_cast<const uint8_t*>(image->getData()), seamless, seamless);
    ownership_->images.erase(imageRef).ignore("release temporary generated image");
    return eve::script::Borrowed<graphics::Texture>(texture,
                                                    static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(gfx)));
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

bool Procgen::hasTextureRecipe(const std::string& recipeId) const {
    TextureRecipeRegistry::instance().registerBuiltins();
    return TextureRecipeRegistry::instance().has(recipeId);
}

eve::Result<RecipeDescriptor> Procgen::getTextureRecipeSchema(const std::string& recipeId) const {
    TextureRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor* schema = TextureRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return eve::Result<RecipeDescriptor>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "texture recipe schema was not found", "recipe", {}, "procgen.squirrel"));
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyTextureRecipeDefaults(const std::string&     recipeId,
                                                      ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                 "texture default parameters handle is stale", "params",
                                                                 {}, "procgen.squirrel"));
    TextureRecipeRegistry::instance().registerBuiltins();
    if (!TextureRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "texture recipe schema was not found", "recipe", {}, "procgen.squirrel"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenCloudFieldHandleRef> Procgen::newCloudFieldHandle() {
    return ownProcgenObject(ownership_->clouds, std::make_unique<CloudField>());
}

eve::script::Borrowed<CloudField> Procgen::resolveCloudField(ProcgenCloudFieldHandleRef reference) noexcept {
    return ownership_->clouds.resolve(reference);
}

eve::Result<void> Procgen::releaseCloudField(ProcgenCloudFieldHandleRef reference) {
    return ownership_->clouds.erase(reference);
}

bool Procgen::isCloudFieldStale(ProcgenCloudFieldHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->clouds.isStale(reference);
}

eve::Result<ProcgenCloudShadowHandleRef> Procgen::newCloudShadowHandle() {
    return ownProcgenObject(ownership_->shadows, std::make_unique<CloudShadow>());
}

eve::script::Borrowed<CloudShadow> Procgen::resolveCloudShadow(ProcgenCloudShadowHandleRef reference) noexcept {
    return ownership_->shadows.resolve(reference);
}

eve::Result<void> Procgen::releaseCloudShadow(ProcgenCloudShadowHandleRef reference) {
    return ownership_->shadows.erase(reference);
}

bool Procgen::isCloudShadowStale(ProcgenCloudShadowHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->shadows.isStale(reference);
}

eve::Result<float> Procgen::cloudCoverageAt(ProcgenCloudFieldHandleRef field, float x, float z, float time) {
    auto view = resolveCloudField(field);
    if (!view.isBound())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "cloud field handle is stale", "field", {}, "procgen.squirrel"));
    return eve::Result<float>::success(view->coverageAt(x, z, time));
}

eve::Result<float> Procgen::cloudShadowFactor(ProcgenCloudShadowHandleRef shadow, float x, float z, float time) {
    auto view = resolveCloudShadow(shadow);
    if (!view.isBound())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "cloud shadow handle is stale", "shadow", {}, "procgen.squirrel"));
    return eve::Result<float>::success(view->shadowFactorAt(x, z, time));
}

eve::Result<void> Procgen::sampleCloud(ProcgenCloudFieldHandleRef field, std::span<float> out, int w, int h, float time,
                                       float x0, float z0, float extent) {
    auto view = resolveCloudField(field);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "cloud field handle is stale", "field", {}, "procgen.squirrel"));
    if (w <= 0 || h <= 0 || out.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "cloud output buffer is smaller than width*height",
                                                                 "out", {}, "procgen.squirrel"));
    view->sample(out.data(), w, h, time, x0, z0, extent);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::sampleCloudShadow(ProcgenCloudShadowHandleRef shadow, std::span<float> out, int w, int h,
                                             float time, float x0, float z0, float extent) {
    auto view = resolveCloudShadow(shadow);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "cloud shadow handle is stale", "shadow", {}, "procgen.squirrel"));
    if (w <= 0 || h <= 0 || out.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "cloud output buffer is smaller than width*height",
                                                                 "out", {}, "procgen.squirrel"));
    view->sampleCoverage(out.data(), w, h, time, x0, z0, extent);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenPbrMaterialHandleRef> Procgen::generatePbrMaterialHandle(const std::string&     recipeId,
                                                                            ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return eve::Result<ProcgenPbrMaterialHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "generatePbrMaterial parameters handle is stale",
                                   "params", {}, "procgen.squirrel"));
    lastError_.clear();
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    auto set = PbrRecipeRegistry::instance().generate(recipeId, *input, lastError_);
    if (!set)
        return eve::Result<ProcgenPbrMaterialHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, lastError_.empty() ? "generatePbrMaterial failed" : lastError_, "recipe", {},
            "procgen.squirrel"));
    return ownProcgenObject(ownership_->pbr, std::move(set));
}

eve::script::Borrowed<PbrTextureSet> Procgen::resolvePbrMaterial(ProcgenPbrMaterialHandleRef reference) noexcept {
    return ownership_->pbr.resolve(reference);
}

eve::Result<void> Procgen::releasePbrMaterial(ProcgenPbrMaterialHandleRef reference) {
    return ownership_->pbr.erase(reference);
}

bool Procgen::isPbrMaterialStale(ProcgenPbrMaterialHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->pbr.isStale(reference);
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

bool Procgen::hasPbrRecipe(const std::string& recipeId) const {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    return PbrRecipeRegistry::instance().has(recipeId);
}

eve::Result<RecipeDescriptor> Procgen::getPbrRecipeSchema(const std::string& recipeId) const {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    const RecipeDescriptor* schema = PbrRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return eve::Result<RecipeDescriptor>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "PBR recipe schema was not found", "recipe", {}, "procgen.squirrel"));
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyPbrRecipeDefaults(const std::string& recipeId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                 "PBR default parameters handle is stale", "params", {},
                                                                 "procgen.squirrel"));
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    if (!PbrRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "PBR recipe schema was not found", "recipe", {}, "procgen.squirrel"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenMeshBuildHandleRef> Procgen::buildMeshHandle(const std::string&     recipeId,
                                                                ProcgenParamsHandleRef params) {
    auto built = buildArtifact(recipeId, params, nextCompatibilityArtifactId());
    if (!built)
        return eve::Result<ProcgenMeshBuildHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, built.status().describe(), "recipe", {}, "procgen.squirrel"));
    GeneratedArtifact artifact = std::move(built).takeValue();
    const MeshBuild*  source   = nullptr;
    if (artifact.type == ArtifactType::MeshData) {
        source = &std::get<MeshData>(artifact.payload);
    } else if (artifact.type == ArtifactType::Composite) {
        const ArtifactPart* part = std::get<CompositeArtifact>(artifact.payload).find("mesh");
        if (part && part->type == ArtifactType::MeshData) source = &std::get<MeshData>(part->payload);
    }
    if (!source)
        return eve::Result<ProcgenMeshBuildHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "generated artifact has no mesh payload", "recipe", {}, "procgen.squirrel"));
    auto              mesh = std::make_unique<MeshBuild>(*source);
    ArtifactPublisher publisher(artifactStore_);
    auto              published = publisher.publish(std::move(artifact), {});
    if (!published)
        return eve::Result<ProcgenMeshBuildHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, published.status().describe(), "artifact", {}, "procgen.squirrel"));
    std::move(published).takeValue();
    return ownProcgenObject(ownership_->meshes, std::move(mesh));
}

eve::script::Borrowed<MeshBuild> Procgen::resolveMeshBuild(ProcgenMeshBuildHandleRef reference) noexcept {
    return ownership_->meshes.resolve(reference);
}

eve::Result<void> Procgen::releaseMeshBuild(ProcgenMeshBuildHandleRef reference) {
    return ownership_->meshes.erase(reference);
}

bool Procgen::isMeshBuildStale(ProcgenMeshBuildHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->meshes.isStale(reference);
}

eve::Result<GeneratedArtifact> Procgen::buildArtifact(const std::string& recipeId, ProcgenParamsHandleRef params,
                                                      ArtifactId id) {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return eve::Result<GeneratedArtifact>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "buildArtifact parameters handle is stale",
                                   "params", {}, "procgen.squirrel"));
    return generateMeshArtifact(recipeId, *view, id);
}

eve::Result<ArtifactPublishReceipt> Procgen::publishArtifact(const std::string& recipeId, ProcgenParamsHandleRef params,
                                                             ArtifactId id, ArtifactPublishOptions options) {
    auto artifact = buildArtifact(recipeId, params, id);
    if (!artifact.ok()) return eve::Result<ArtifactPublishReceipt>::failure(artifact.status());
    ArtifactPublisher publisher(artifactStore_);
    return publisher.publish(std::move(artifact).takeValue(), options);
}

ArtifactId Procgen::nextCompatibilityArtifactId() noexcept {
    static const ArtifactId root = [] {
        const auto parsed = ArtifactId::parse("6bdf48e2-fca4-4f62-8a66-d952bd6af046");
        return parsed ? *parsed : ArtifactId::nil();
    }();
    return root.child("legacy-facade-" + std::to_string(nextArtifactSequence_++));
}

eve::script::Borrowed<graphics::Mesh> Procgen::generateMeshBorrowed(const std::string&     recipeId,
                                                                    ProcgenParamsHandleRef params,
                                                                    graphics::Graphics*    gfx) {
    auto input = Procgen::resolve(params);
    if (!input.isBound() || !gfx) return {};
    auto built = buildMeshHandle(recipeId, params);
    if (!built) return {};
    auto cpu = ownership_->meshes.resolve(std::move(built).takeValue());
    if (!cpu.isBound()) return {};
    return uploadMeshBorrowed(*cpu, *gfx);
}

eve::script::Borrowed<graphics::Mesh> Procgen::uploadMeshBorrowed(const MeshBuild& mesh, graphics::Graphics& gfx) {
    if (mesh.empty()) return {};
    graphics::Mesh* uploaded = gfx.newMeshFromArraysColored(mesh.positions().data(), mesh.normals().data(),
        mesh.uvs().data(),mesh.hasVertexColors()?mesh.colors().data():nullptr,mesh.getVertexCount(),
        mesh.indices().data(),mesh.getIndexCount());
    return eve::script::Borrowed<graphics::Mesh>(uploaded,
                                                 static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&gfx)));
}

eve::Result<void> Procgen::configureGtsTerrainTileLods(
    const GtsTerrainLodSet& lods,int tileIndex,graphics::Renderable3D& renderable,graphics::Graphics& gfx,
    float worldDiameter,float verticalFovDegrees,float originX,float originY,float originZ) {
    const auto* tile=lods.tileAt(tileIndex);
    if(!tile||tile->levels.empty()||tile->levels.size()>graphics::Renderable3D::MeshRenderer::kMaxLodLevels||
       !std::isfinite(worldDiameter)||worldDiameter<=0.f||!std::isfinite(verticalFovDegrees)||
       verticalFovDegrees<=0.f||verticalFovDegrees>=180.f||!std::isfinite(originX)||!std::isfinite(originY)||!std::isfinite(originZ))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
            "GTS terrain tile render configuration is invalid","procgen.configureGtsTerrainTileLods"));
    std::vector<graphics::Mesh*> uploaded;uploaded.reserve(tile->levels.size());
    for(const auto& level:tile->levels){auto mesh=uploadMeshBorrowed(level,gfx);if(!mesh.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
            "GTS terrain tile contains an empty or unuploadable LOD","procgen.configureGtsTerrainTileLods"));uploaded.push_back(mesh.get());}
    renderable.clearMeshLod();
    for(int level=0;level<static_cast<int>(uploaded.size());++level){float distance=level==0?0.f:lods.getLevelSwitchDistance(level-1,worldDiameter,verticalFovDegrees);renderable.setMeshLod(level,uploaded[level],distance);}
    renderable.setMeshLodCullDistance(lods.getLevelSwitchDistance(static_cast<int>(uploaded.size())-1,worldDiameter,verticalFovDegrees));
    renderable.setPosition(originX+tile->offsetX,originY,originZ+tile->offsetZ);
    return eve::Result<void>::success();
}

eve::Result<void> Procgen::configurePcgMeshLods(const PcgMeshLodSet& lods,
                                                  graphics::Renderable3D& renderable,
                                                  graphics::Graphics& gfx, float worldDiameter,
                                                  float verticalFovDegrees) {
    const int count = lods.getLevelCount();
    if (count <= 0 || count > graphics::Renderable3D::MeshRenderer::kMaxLodLevels ||
        !std::isfinite(worldDiameter) || worldDiameter <= 0.F || !std::isfinite(verticalFovDegrees) ||
        verticalFovDegrees <= 0.F || verticalFovDegrees >= 180.F)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Pcg mesh LOD render configuration is invalid",
            "procgen.configurePcgMeshLods"));
    std::vector<graphics::Mesh*> uploaded;
    uploaded.reserve(static_cast<std::size_t>(count));
    for (int level = 0; level < count; ++level) {
        const MeshBuild* source = lods.meshAt(level);
        if (!source) return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Pcg mesh LOD level is missing",
            "procgen.configurePcgMeshLods"));
        auto mesh = uploadMeshBorrowed(*source, gfx);
        if (!mesh.isBound()) return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "Pcg mesh LOD level could not be uploaded",
            "procgen.configurePcgMeshLods"));
        uploaded.push_back(mesh.get());
    }
    auto candidate = *renderable.meshRenderer();
    candidate.lodCount = 0;
    for (int level = 0; level < graphics::Renderable3D::MeshRenderer::kMaxLodLevels; ++level) {
        candidate.lodMeshes[level] = nullptr;
        candidate.lodRendererStates[level] = {};
    }
    for (int level = 0; level < count; ++level) {
        const float distance = level == 0 ? 0.F : lods.getSwitchDistance(level - 1, worldDiameter, verticalFovDegrees);
        candidate.lodMeshes[level] = uploaded[static_cast<std::size_t>(level)];
        candidate.lodCount = level + 1;
        if (level == 0) candidate.mesh = uploaded[0];
        if (level > 0) candidate.lodDistances[level - 1] = distance;
        const auto* state = lods.rendererStateAt(level);
        if (!state) return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Pcg mesh LOD renderer state is missing",
            "procgen.configurePcgMeshLods"));
        auto& target = candidate.lodRendererStates[level];
        target.configured = true;
        target.skinQuality = state->skinQuality;
        target.shadowCastingMode = state->shadowCastingMode;
        target.receiveShadows = state->receiveShadows;
        target.motionVectorMode = state->motionVectorMode;
        target.skinnedMotionVectors = state->skinnedMotionVectors;
        target.lightProbeUsage = state->lightProbeUsage;
        target.reflectionProbeUsage = state->reflectionProbeUsage;
        candidate.lodFadeWidths[level] = lods.getFadeWidth(level);
    }
    const float cullDistance = lods.getSwitchDistance(count - 1, worldDiameter, verticalFovDegrees);
    candidate.lodCullDistance = cullDistance > 0.F ? cullDistance : 0.F;
    candidate.lodFadeMode = lods.getFadeMode();
    candidate.lodAnimateCrossFading = lods.getAnimateCrossFading();
    candidate.lodCrossFadeDuration = lods.getCrossFadeAnimationDuration();
    candidate.lodAnimatedCurrent=-2;candidate.lodAnimatedPrevious=-2;candidate.lodAnimatedProgress=1.F;
    *renderable.meshRenderer() = candidate;
    return eve::Result<void>::success();
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

bool Procgen::hasMeshRecipe(const std::string& recipeId) const {
    MeshRecipeRegistry::instance().registerBuiltins();
    return MeshRecipeRegistry::instance().has(recipeId);
}

eve::Result<RecipeDescriptor> Procgen::getMeshRecipeSchema(const std::string& recipeId) const {
    MeshRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor* schema = MeshRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return eve::Result<RecipeDescriptor>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "mesh recipe schema was not found", "recipe", {}, "procgen.squirrel"));
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyMeshRecipeDefaults(const std::string& recipeId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                 "mesh default parameters handle is stale", "params",
                                                                 {}, "procgen.squirrel"));
    MeshRecipeRegistry::instance().registerBuiltins();
    if (!MeshRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "mesh recipe schema was not found", "recipe", {}, "procgen.squirrel"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenTerrainSamplerHandleRef> Procgen::newTerrainSamplerHandle() {
    return ownProcgenObject(ownership_->samplers, std::make_unique<TerrainSampler>());
}

eve::script::Borrowed<TerrainSampler> Procgen::resolveTerrainSampler(
    ProcgenTerrainSamplerHandleRef reference) noexcept {
    return ownership_->samplers.resolve(reference);
}

eve::Result<void> Procgen::releaseTerrainSampler(ProcgenTerrainSamplerHandleRef reference) {
    return ownership_->samplers.erase(reference);
}

bool Procgen::isTerrainSamplerStale(ProcgenTerrainSamplerHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->samplers.isStale(reference);
}

eve::Result<ProcgenHeightmapHandleRef> Procgen::newHeightmapHandle(int width, int height) {
    if (width <= 0 || height <= 0)
        return eve::Result<ProcgenHeightmapHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "heightmap dimensions must be positive",
                                   "heightmap", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->heightmaps, std::make_unique<Heightmap>(width, height));
}

eve::Result<ProcgenHeightmapHandleRef> Procgen::adoptHeightmap(Heightmap heightmap) {
    if (heightmap.getWidth() <= 0 || heightmap.getHeight() <= 0)
        return eve::Result<ProcgenHeightmapHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "decoded heightmap has no samples",
                                   "heightmap", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->heightmaps, std::make_unique<Heightmap>(std::move(heightmap)));
}

eve::script::Borrowed<Heightmap> Procgen::resolveHeightmap(ProcgenHeightmapHandleRef reference) noexcept {
    return ownership_->heightmaps.resolve(reference);
}

eve::Result<void> Procgen::releaseHeightmap(ProcgenHeightmapHandleRef reference) {
    return ownership_->heightmaps.erase(reference);
}

bool Procgen::isHeightmapStale(ProcgenHeightmapHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->heightmaps.isStale(reference);
}

eve::Result<ProcgenHeightmapHandleRef> Procgen::generateHeightmapHandle(ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return eve::Result<ProcgenHeightmapHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "generateHeightmap parameters handle is stale",
                                   "params", {}, "procgen.squirrel"));
    const TerrainSampler sampler = TerrainSampler::fromParams(*input);
    return ownProcgenObject(ownership_->heightmaps, std::make_unique<Heightmap>(Heightmap::generate(
                                                        sampler, input->getWidth(), input->getHeight())));
}

eve::Result<ProcgenGridHandleRef> Procgen::heightmapToGrid(ProcgenHeightmapHandleRef heightmap,
                                                           ProcgenParamsHandleRef    params) {
    auto map   = resolveHeightmap(heightmap);
    auto input = Procgen::resolve(params);
    if (!map.isBound())
        return eve::Result<ProcgenGridHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "heightmap handle is stale", "heightmap", {}, "procgen.squirrel"));
    if (!input.isBound())
        return eve::Result<ProcgenGridHandleRef>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                                 "heightmap parameters handle is stale",
                                                                                 "params", {}, "procgen.squirrel"));
    auto               grid  = std::make_unique<Grid2D>();
    const TerrainBands bands = TerrainBands::fromParams(*input);
    if (!map->toGrid(*grid, bands))
        return eve::Result<ProcgenGridHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "heightmap is empty", "heightmap", {}, "procgen.squirrel"));
    Procgen* module = Procgen::create();
    return module->grids_.emplace(std::move(grid));
}

bool Procgen::erodeTerrainThermal(Heightmap* heightmap, int iterations, float talus, float strength) {
    lastError_.clear();
    if (!heightmap || heightmap->getWidth() < 2 || heightmap->getHeight() < 2) {
        lastError_ = "erodeTerrainThermal: heightmap must be at least 2x2";
        return false;
    }
    TerrainPipeline::erodeThermal(*heightmap, {iterations, talus, strength});
    return true;
}

bool Procgen::erodeTerrainHydraulic(Heightmap* heightmap, int iterations, float rainfall, float evaporation,
                                    float capacity, float erosion, float deposition) {
    lastError_.clear();
    if (!heightmap || heightmap->getWidth() < 2 || heightmap->getHeight() < 2) {
        lastError_ = "erodeTerrainHydraulic: heightmap must be at least 2x2";
        return false;
    }
    TerrainPipeline::erodeHydraulic(*heightmap, {iterations, rainfall, evaporation, capacity, erosion, deposition});
    return true;
}

bool Procgen::erodeTerrainFluvial(Heightmap* heightmap, int iterations, float riverThreshold, float incision,
                                  float maxDepth, float bankWidth) {
    return erodeTerrainFluvialAdvanced(heightmap, iterations, riverThreshold, incision, maxDepth, bankWidth,
                                       std::min(maxDepth, 0.04f));
}

bool Procgen::erodeTerrainFluvialAdvanced(Heightmap* heightmap, int iterations, float riverThreshold, float incision,
                                          float maxDepth, float bankWidth, float maxBreachDepth) {
    return erodeTerrainFluvialScaled(heightmap, iterations, riverThreshold, incision, maxDepth, bankWidth,
                                     maxBreachDepth, 1.f);
}

bool Procgen::erodeTerrainFluvialScaled(Heightmap* heightmap, int iterations, float riverThreshold, float incision,
                                        float maxDepth, float bankWidth, float maxBreachDepth, float coordinateScale) {
    lastError_.clear();
    if (!heightmap || heightmap->getWidth() < 3 || heightmap->getHeight() < 3) {
        lastError_ = "erodeTerrainFluvial: heightmap must be at least 3x3";
        return false;
    }
    if (!std::isfinite(coordinateScale) || coordinateScale <= 0.f) {
        lastError_ = "erodeTerrainFluvialScaled: coordinateScale must be positive";
        return false;
    }
    TerrainPipeline::erodeFluvial(
        *heightmap, {iterations, riverThreshold, incision, maxDepth, bankWidth, maxBreachDepth, coordinateScale});
    return true;
}

TerrainErosionMap* Procgen::erodeTerrainFluvialDetailed(Heightmap* heightmap, int iterations, float riverThreshold,
                                                        float incision, float maxDepth, float bankWidth,
                                                        float maxBreachDepth, float coordinateScale) {
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
        *heightmap, {iterations, riverThreshold, incision, maxDepth, bankWidth, maxBreachDepth, coordinateScale});
    if (diagnostics.width <= 0) {
        lastError_ = "erodeTerrainFluvialDetailed: invalid erosion settings";
        return nullptr;
    }
    return new TerrainErosionMap(std::move(diagnostics));
}

TerrainLayers* Procgen::analyzeTerrain(Heightmap* heightmap, float riverThreshold, float seaLevel, float latitude) {
    return analyzeTerrainScaled(heightmap, riverThreshold, seaLevel, latitude, 1.f);
}

TerrainLayers* Procgen::analyzeTerrainScaled(Heightmap* heightmap, float riverThreshold, float seaLevel, float latitude,
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
    HydrologyMap hydrology = TerrainPipeline::buildHydrology(*heightmap, riverThreshold, seaLevel, coordinateScale);
    ClimateMap   climate   = TerrainPipeline::buildClimate(*heightmap, hydrology, seaLevel, latitude, coordinateScale);
    return new TerrainLayers(std::move(hydrology), std::move(climate));
}

data::ByteData* Procgen::bakeTerrainAsset(Heightmap* heightmap, TerrainLayers* layers, int chunkSize) {
    lastError_.clear();
    if (!heightmap || !layers) {
        lastError_ = "bakeTerrainAsset: heightmap and layers are required";
        return nullptr;
    }
    std::vector<uint8_t> bytes;
    if (!TerrainAsset::bake(*heightmap, layers->hydrology(), layers->climate(), chunkSize, bytes, &lastError_))
        return nullptr;
    return new data::ByteData(bytes.data(), bytes.size());
}

TerrainMeshChunk* Procgen::buildTerrainChunk(Heightmap* heightmap, TerrainLayers* layers, int originX, int originY,
                                             int cellsX, int cellsY, int lod, float cellSize, float heightScale,
                                             float skirtDepth) {
    lastError_.clear();
    if (!heightmap) {
        lastError_ = "buildTerrainChunk: heightmap is required";
        return nullptr;
    }
    TerrainMeshSettings settings;
    settings.originX     = originX;
    settings.originY     = originY;
    settings.cellsX      = cellsX;
    settings.cellsY      = cellsY;
    settings.lod         = lod;
    settings.cellSize    = cellSize;
    settings.heightScale = heightScale;
    settings.skirtDepth  = skirtDepth;
    auto* chunk          = new TerrainMeshChunk();
    if (!TerrainMeshBuilder::build(*heightmap, layers, settings, *chunk, &lastError_)) {
        delete chunk;
        return nullptr;
    }
    return chunk;
}

int Procgen::selectTerrainLod(Heightmap* heightmap, int originX, int originY, int cellsX, int cellsY, int maxLod,
                              float cellSize, float heightScale, float cameraDistance, float viewportHeight,
                              float verticalFovDegrees, float targetPixelError) {
    lastError_.clear();
    if (!heightmap) {
        lastError_ = "selectTerrainLod: heightmap is required";
        return -1;
    }
    TerrainMeshSettings settings;
    settings.originX     = originX;
    settings.originY     = originY;
    settings.cellsX      = cellsX;
    settings.cellsY      = cellsY;
    settings.cellSize    = cellSize;
    settings.heightScale = heightScale;
    const int lod        = TerrainLodSelector::select(*heightmap, settings, maxLod, cameraDistance, viewportHeight,
                                                      verticalFovDegrees, targetPixelError);
    if (lod < 0) lastError_ = "selectTerrainLod: invalid bounds or projection settings";
    return lod;
}

graphics::Mesh* Procgen::generateTerrainChunkMesh(TerrainMeshChunk* chunk, graphics::Graphics* gfx) {
    lastError_.clear();
    if (!chunk || !gfx || chunk->mesh().empty()) {
        lastError_ = "generateTerrainChunkMesh: chunk and graphics are required";
        return nullptr;
    }
    const MeshBuild& mesh = chunk->mesh();
    return gfx->newMeshFromArrays(mesh.positions().data(), mesh.normals().data(), mesh.uvs().data(),
                                  mesh.getVertexCount(), mesh.indices().data(), mesh.getIndexCount());
}

graphics::Mesh* Procgen::generateTerrainRiverMesh(Heightmap* heightmap, TerrainLayers* layers, graphics::Graphics* gfx,
                                                  int originX, int originY, int cellsX, int cellsY, float cellSize,
                                                  float heightScale, float minWidth, float maxWidth,
                                                  float heightOffset) {
    return generateTerrainRiverMeshAdvanced(heightmap, layers, gfx, originX, originY, cellsX, cellsY, cellSize,
                                            heightScale, minWidth, maxWidth, heightOffset, 0.f, 0.30f);
}

graphics::Mesh* Procgen::generateTerrainRiverMeshAdvanced(Heightmap* heightmap, TerrainLayers* layers,
                                                          graphics::Graphics* gfx, int originX, int originY, int cellsX,
                                                          int cellsY, float cellSize, float heightScale, float minWidth,
                                                          float maxWidth, float heightOffset, float minSurfaceSlope,
                                                          float maxSurfaceSlope) {
    lastError_.clear();
    if (!heightmap || !layers || !gfx) {
        lastError_ = "generateTerrainRiverMesh: heightmap, layers, and graphics are required";
        return nullptr;
    }
    TerrainRiverMeshSettings settings;
    settings.originX         = originX;
    settings.originY         = originY;
    settings.cellsX          = cellsX;
    settings.cellsY          = cellsY;
    settings.cellSize        = cellSize;
    settings.heightScale     = heightScale;
    settings.minWidth        = minWidth;
    settings.maxWidth        = maxWidth;
    settings.heightOffset    = heightOffset;
    settings.minSurfaceSlope = minSurfaceSlope;
    settings.maxSurfaceSlope = maxSurfaceSlope;
    MeshBuild river;
    if (!TerrainRiverMeshBuilder::build(*heightmap, *layers, settings, river, &lastError_) || river.empty())
        return nullptr;
    return gfx->newMeshFromArrays(river.positions().data(), river.normals().data(), river.uvs().data(),
                                  river.getVertexCount(), river.indices().data(), river.getIndexCount());
}

graphics::Mesh* Procgen::generateTerrainLakeMesh(Heightmap* heightmap, TerrainLayers* layers, graphics::Graphics* gfx,
                                                 int originX, int originY, int cellsX, int cellsY, float cellSize,
                                                 float heightScale, float minimumDepth, float heightOffset) {
    lastError_.clear();
    if (!heightmap || !layers || !gfx) {
        lastError_ = "generateTerrainLakeMesh: heightmap, layers, and graphics are required";
        return nullptr;
    }
    TerrainLakeMeshSettings settings;
    settings.originX      = originX;
    settings.originY      = originY;
    settings.cellsX       = cellsX;
    settings.cellsY       = cellsY;
    settings.cellSize     = cellSize;
    settings.heightScale  = heightScale;
    settings.minimumDepth = minimumDepth;
    settings.heightOffset = heightOffset;
    MeshBuild lake;
    if (!TerrainLakeMeshBuilder::build(*heightmap, *layers, settings, lake, &lastError_) || lake.empty())
        return nullptr;
    return gfx->newMeshFromArrays(lake.positions().data(), lake.normals().data(), lake.uvs().data(),
                                  lake.getVertexCount(), lake.indices().data(), lake.getIndexCount());
}

image::ImageData* Procgen::generateTerrainSplatMap(TerrainMeshChunk* chunk) {
    lastError_.clear();
    if (!chunk || chunk->getSplatWidth() <= 0 || chunk->getSplatHeight() <= 0) {
        lastError_ = "generateTerrainSplatMap: a built terrain chunk is required";
        return nullptr;
    }
    auto* image  = new image::ImageData(chunk->getSplatWidth(), chunk->getSplatHeight(), "RGBA8");
    auto* pixels = static_cast<uint8_t*>(image->getData());
    for (int vertex = 0; vertex < chunk->getBaseVertexCount(); ++vertex) {
        std::array<int, 4>   quantized{};
        std::array<float, 4> remainder{};
        int                  total = 0;
        for (int channel = 0; channel < 4; ++channel) {
            const float scaled = std::clamp(chunk->getMaterialWeight(vertex, channel), 0.f, 1.f) * 255.f;
            quantized[channel] = int(std::floor(scaled));
            remainder[channel] = scaled - float(quantized[channel]);
            total += quantized[channel];
        }
        while (total < 255) {
            const int channel = int(std::max_element(remainder.begin(), remainder.end()) - remainder.begin());
            ++quantized[channel];
            remainder[channel] = -1.f;
            ++total;
        }
        for (int channel = 0; channel < 4; ++channel)
            pixels[size_t(vertex) * 4u + size_t(channel)] = uint8_t(quantized[channel]);
    }
    return image;
}

image::ImageData* Procgen::generateTerrainAlbedoMap(TerrainMeshChunk* chunk) {
    lastError_.clear();
    if (!chunk || chunk->getSplatWidth() <= 0 || chunk->getSplatHeight() <= 0) {
        lastError_ = "generateTerrainAlbedoMap: a built terrain chunk is required";
        return nullptr;
    }
    static constexpr std::array<std::array<float, 3>, 4> palette{{
        {{0.55f, 0.40f, 0.22f}},  // sand, dry soil, and river sediment
        {{0.15f, 0.38f, 0.12f}},  // vegetation
        {{0.36f, 0.35f, 0.33f}},  // exposed rock
        {{0.88f, 0.91f, 0.94f}},  // snow
    }};
    auto* image  = new image::ImageData(chunk->getSplatWidth(), chunk->getSplatHeight(), "RGBA8");
    auto* pixels = static_cast<uint8_t*>(image->getData());
    for (int vertex = 0; vertex < chunk->getBaseVertexCount(); ++vertex) {
        const int            biome = chunk->getBiome(vertex);
        std::array<float, 3> semanticColor{};
        bool                 useSemanticColor = true;
        if (biome == int(Biome::Ocean))
            semanticColor = {0.035f, 0.16f, 0.25f};
        else if (biome == int(Biome::River))
            semanticColor = {0.24f, 0.18f, 0.09f};
        else if (biome == int(Biome::Lake))
            semanticColor = {0.12f, 0.19f, 0.14f};
        else if (biome == int(Biome::Wetland))
            semanticColor = {0.12f, 0.28f, 0.07f};
        else if (biome == int(Biome::Beach))
            semanticColor = {0.42f, 0.34f, 0.19f};
        else
            useSemanticColor = false;
        for (int component = 0; component < 3; ++component) {
            float value = semanticColor[component];
            if (!useSemanticColor) {
                value = 0.f;
                for (int channel = 0; channel < 4; ++channel)
                    value += chunk->getMaterialWeight(vertex, channel) * palette[channel][component];
            }
            pixels[size_t(vertex) * 4u + size_t(component)] = uint8_t(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
        }
        pixels[size_t(vertex) * 4u + 3u] = 255;
    }
    return image;
}

namespace {
float diagnosticScale(const std::vector<float>& values, float exposure) {
    if (std::isfinite(exposure) && exposure > 0.f) return exposure;
    std::vector<float> positive;
    positive.reserve(values.size());
    for (float value : values)
        if (std::isfinite(value) && value > 0.f) positive.push_back(value);
    if (positive.empty()) return 1.f;
    const size_t percentile = std::min(positive.size() - 1, size_t(std::floor(float(positive.size() - 1) * 0.99f)));
    std::nth_element(positive.begin(), positive.begin() + ptrdiff_t(percentile), positive.end());
    return 1.f / std::max(1e-8f, positive[percentile]);
}

enum class ErosionImageMode { Combined, Wear, Deposit };

image::ImageData* erosionDiagnosticImage(TerrainErosionMap* map, float exposure, ErosionImageMode mode) {
    if (!map || map->width <= 0 || map->height <= 0 || map->wear.size() != size_t(map->width) * size_t(map->height) ||
        map->deposition.size() != map->wear.size())
        return nullptr;
    const float wearScale    = diagnosticScale(map->wear, exposure);
    const float depositScale = diagnosticScale(map->deposition, exposure);
    auto*       result       = new image::ImageData(map->width, map->height, "RGBA8");
    auto*       pixels       = static_cast<uint8_t*>(result->getData());
    for (size_t i = 0; i < map->wear.size(); ++i) {
        const float wear    = std::sqrt(std::clamp(map->wear[i] * wearScale, 0.f, 1.f));
        const float deposit = std::sqrt(std::clamp(map->deposition[i] * depositScale, 0.f, 1.f));
        float       r = 0.025f, g = 0.035f, b = 0.050f;
        if (mode == ErosionImageMode::Wear) {
            r += 0.95f * wear;
            g += 0.30f * wear;
            b += 0.035f * wear;
        } else if (mode == ErosionImageMode::Deposit) {
            r += 0.035f * deposit;
            g += 0.78f * deposit;
            b += 0.95f * deposit;
        } else {
            r += 0.95f * wear + 0.035f * deposit;
            g += 0.30f * wear + 0.78f * deposit;
            b += 0.035f * wear + 0.95f * deposit;
        }
        pixels[i * 4u]      = uint8_t(std::lround(std::clamp(r, 0.f, 1.f) * 255.f));
        pixels[i * 4u + 1u] = uint8_t(std::lround(std::clamp(g, 0.f, 1.f) * 255.f));
        pixels[i * 4u + 2u] = uint8_t(std::lround(std::clamp(b, 0.f, 1.f) * 255.f));
        pixels[i * 4u + 3u] = 255;
    }
    return result;
}
}  // namespace

image::ImageData* Procgen::generateTerrainErosionMap(TerrainErosionMap* erosion, float exposure) {
    lastError_.clear();
    image::ImageData* result = erosionDiagnosticImage(erosion, exposure, ErosionImageMode::Combined);
    if (!result) lastError_ = "generateTerrainErosionMap: valid erosion diagnostics are required";
    return result;
}

image::ImageData* Procgen::generateTerrainWearMap(TerrainErosionMap* erosion, float exposure) {
    lastError_.clear();
    image::ImageData* result = erosionDiagnosticImage(erosion, exposure, ErosionImageMode::Wear);
    if (!result) lastError_ = "generateTerrainWearMap: valid erosion diagnostics are required";
    return result;
}

image::ImageData* Procgen::generateTerrainDepositionMap(TerrainErosionMap* erosion, float exposure) {
    lastError_.clear();
    image::ImageData* result = erosionDiagnosticImage(erosion, exposure, ErosionImageMode::Deposit);
    if (!result) lastError_ = "generateTerrainDepositionMap: valid erosion diagnostics are required";
    return result;
}

graphics::Shader* Procgen::createTerrainMaterialShader(graphics::Graphics* gfx) {
    lastError_.clear();
    if (!gfx) {
        lastError_ = "createTerrainMaterialShader: graphics is required";
        return nullptr;
    }
    try {
        std::vector<uint32_t> fragment(terrain_material_compat_frag_spv,
                                       terrain_material_compat_frag_spv + terrain_material_compat_frag_spv_count);
        auto *shader = gfx->newMeshShaderFromSpv({}, fragment);
        if (!shader) return nullptr;
        for (int slot = 0; slot < 4; ++slot)
            shader->declareVec4("terrainLayer" + std::to_string(slot) + "ST");
        shader->declareVec4("terrainMetallic");
        shader->declareVec4("terrainNormalScale");
        shader->sendVec4("terrainNormalScale", 1.f, 1.f, 1.f, 1.f);
        shader->declareVec4("terrainSmoothness");
        shader->declareVec4("terrainFeatures");
        return shader;
    } catch (const std::exception &e) {
        lastError_ = std::string("createTerrainMaterialShader: ") + e.what();
        return nullptr;
    }
}

graphics::Shader* Procgen::createTerrainWaterShader(graphics::Graphics* gfx) {
    lastError_.clear();
    if (!gfx) {
        lastError_ = "createTerrainWaterShader: graphics is required";
        return nullptr;
    }
    try {
        std::vector<uint32_t> fragment(terrain_water_compat_frag_spv,
                                       terrain_water_compat_frag_spv + terrain_water_compat_frag_spv_count);
        return gfx->newMeshShaderFromSpv({}, fragment);
    } catch (const std::exception& e) {
        lastError_ = std::string("createTerrainWaterShader: ") + e.what();
        return nullptr;
    }
}


}  // namespace eve::procgen
