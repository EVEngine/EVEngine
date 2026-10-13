#include "procgen/Procgen.h"
#include "procgen/ProcgenLive.h"
#include "procgen/ProcgenScriptSupport.h"

#include "common/Capability.h"
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

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::procgen {

void Procgen::expose(ssq::Table& table) {
    const HSQUIRRELVM vm  = table.getHandle();
    auto              cls = table.addClass(name, Procgen::create, false);
    expose(cls);
    exposeBiomeRules(table);
    exposePointGraph(table);
    exposeMeshModifierGraph(table);
    exposeGridMeshGraphs(table);
    housegen::exposeHouseGeneration(table);
    exposeShapeGrammar(table);
    exposePcgFrameRateManagerBindings(table);
    exposePcgTaskQueueBindings(table);

    exposeModuleAssembly(table);

    auto recipe = table.addClass<RecipeDescriptor>(
        "ProcgenRecipeSchema", std::function<RecipeDescriptor*()>([]() -> RecipeDescriptor* { return nullptr; }), true);
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

    auto params =
        table.addClass<Params>("ProcgenParams", std::function<Params*()>([]() -> Params* { return nullptr; }), true);
    params.addFunc("setSeed", &Params::setSeed);
    params.addFunc("getSeed", &Params::getSeed);
    params.addFunc("setSize", &Params::setSize);
    params.addFunc("getWidth", &Params::getWidth);
    params.addFunc("getHeight", &Params::getHeight);
    params.addFunc("setInt", &Params::setInt);
    params.addFunc("setFloat", &Params::setFloat);
    params.addFunc("setBool", &Params::setBool);
    params.addFunc("setString", &Params::setString);
    params.addFunc("has", &Params::has);
    params.addFunc("getInt", &Params::getInt);
    params.addFunc("getFloat", &Params::getFloat);
    params.addFunc("getBool", &Params::getBool);
    params.addFunc("getString", &Params::getString);

    auto output = table.addClass<OutputSpec>(
        "ProcgenOutput", std::function<OutputSpec*()>([]() -> OutputSpec* { return nullptr; }), true);
    output.addFunc("setTarget", &OutputSpec::setTarget);
    output.addFunc("getTarget", &OutputSpec::getTarget);
    output.addFunc("setLayer", &OutputSpec::setLayer);
    output.addFunc("getLayer", &OutputSpec::getLayer);
    output.addFunc("setPalette", &OutputSpec::setPalette);
    output.addFunc("getPalette", &OutputSpec::getPalette);
    output.addFunc("setPath", &OutputSpec::setPath);
    output.addFunc("getPath", &OutputSpec::getPath);

    auto grid =
        table.addClass<Grid2D>("ProcgenGrid2D", std::function<Grid2D*()>([]() -> Grid2D* { return nullptr; }), true);
    const HSQUIRRELVM gridVm = grid.getHandle();
    grid.addFunc("ownership", [](Grid2D* value) {
        return nativeProxyReference<ProcgenGridHandleRef>(value).has_value() ? std::string("owned")
                                                                            : std::string("value");
    });
    grid.addFunc("ownerEpoch", [](Grid2D* value) {
        const auto reference = nativeProxyReference<ProcgenGridHandleRef>(value);
        return reference ? static_cast<std::int64_t>(reference->ownerEpoch) : std::int64_t{0};
    });
    grid.addFunc("handle", [](Grid2D* value) {
        const auto reference = nativeProxyReference<ProcgenGridHandleRef>(value);
        return reference ? static_cast<std::int64_t>(reference->packed()) : std::int64_t{0};
    });
    grid.addFunc("isStale", [](Grid2D* value) {
        const auto reference = nativeProxyReference<ProcgenGridHandleRef>(value);
        return reference ? Procgen::isStale(*reference) : false;
    });
    grid.addFunc("release", [gridVm](Grid2D* value) {
        const auto reference = nativeProxyReference<ProcgenGridHandleRef>(value);
        if (!reference)
            return eve::script::projectResult(
                gridVm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "grid value is not an owned procgen proxy",
                                                                          "grid", {}, "procgen.squirrel")));
        return eve::script::projectResult(gridVm, Procgen::release(*reference));
    });
    grid.addFunc("resize", [gridVm](Grid2D* value, int width, int height) {
        value->resize(width, height);
        return eve::script::projectResult(gridVm, eve::Result<void>::success());
    });
    grid.addFunc("getWidth", &Grid2D::getWidth);
    grid.addFunc("getHeight", &Grid2D::getHeight);
    grid.addFunc("setCell", [gridVm](Grid2D* value, int x, int y, int semantic) {
        value->setCell(x, y, semantic);
        return eve::script::projectResult(gridVm, eve::Result<void>::success());
    });
    grid.addFunc("getCell", &Grid2D::getCell);
    grid.addFunc("setDetail", [gridVm](Grid2D* value, int x, int y, int detail) {
        value->setDetail(x, y, detail);
        return eve::script::projectResult(gridVm, eve::Result<void>::success());
    });
    grid.addFunc("getDetail", &Grid2D::getDetail);
    grid.addFunc("fill", [gridVm](Grid2D* value, int semantic) {
        value->fill(semantic);
        return eve::script::projectResult(gridVm, eve::Result<void>::success());
    });
    grid.addFunc("setMeta", [gridVm](Grid2D* value, const std::string& key, const std::string& data) {
        value->setMeta(key, data);
        return eve::script::projectResult(gridVm, eve::Result<void>::success());
    });
    grid.addFunc("getMeta", &Grid2D::getMeta);
    grid.addFunc("clearObjects", &Grid2D::clearObjects);
    grid.addFunc("addObjectAt", &Grid2D::addObjectAt);
    grid.addFunc("addObject", &Grid2D::addObject);
    grid.addFunc("addAssetObject", &Grid2D::addAssetObject);
    grid.addFunc("getObjectCount", &Grid2D::getObjectCount);
    grid.addFunc("getObjectName", &Grid2D::getObjectName);
    grid.addFunc("getObjectType", &Grid2D::getObjectType);
    grid.addFunc("getObjectX", &Grid2D::getObjectX);
    grid.addFunc("getObjectY", &Grid2D::getObjectY);
    grid.addFunc("getObjectWidth", &Grid2D::getObjectWidth);
    grid.addFunc("getObjectHeight", &Grid2D::getObjectHeight);
    grid.addFunc("getObjectGid", &Grid2D::getObjectGid);
    grid.addFunc("getObjectAsset", &Grid2D::getObjectAsset);
    grid.addFunc("getObjectRotation", &Grid2D::getObjectRotation);
    grid.addFunc("getObjectFlags", &Grid2D::getObjectFlags);

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
    points.addFunc("setRotation", &PointSet::setRotation);
    points.addFunc("getPitch", &PointSet::getPitch);
    points.addFunc("getRoll", &PointSet::getRoll);
    points.addFunc("setScale", &PointSet::setScale);
    points.addFunc("getScaleX", &PointSet::getScaleX);
    points.addFunc("getScaleY", &PointSet::getScaleY);
    points.addFunc("getScaleZ", &PointSet::getScaleZ);
    points.addFunc("setBounds", &PointSet::setBounds);
    points.addFunc("getBoundsMinX", &PointSet::getBoundsMinX);
    points.addFunc("getBoundsMinY", &PointSet::getBoundsMinY);
    points.addFunc("getBoundsMinZ", &PointSet::getBoundsMinZ);
    points.addFunc("getBoundsMaxX", &PointSet::getBoundsMaxX);
    points.addFunc("getBoundsMaxY", &PointSet::getBoundsMaxY);
    points.addFunc("getBoundsMaxZ", &PointSet::getBoundsMaxZ);
    points.addFunc("setColor", &PointSet::setColor);
    points.addFunc("getColorR", &PointSet::getColorR);
    points.addFunc("getColorG", &PointSet::getColorG);
    points.addFunc("getColorB", &PointSet::getColorB);
    points.addFunc("getColorA", &PointSet::getColorA);
    points.addFunc("setSteepness", &PointSet::setSteepness);
    points.addFunc("getSteepness", &PointSet::getSteepness);
    points.addFunc("setDensity", &PointSet::setDensity);
    points.addFunc("getDensity", &PointSet::getDensity);
    points.addFunc("setPointSeed", &PointSet::setPointSeed);
    points.addFunc("getPointSeed", &PointSet::getPointSeed);
    points.addFunc("getPointId", [](PointSet* value, int index) { return std::to_string(value->getPointId(index)); });
    points.addFunc("assignPointIds", [](PointSet* value, const std::string& namespaceText) {
        std::uint64_t namespaceId = 0;
        const auto [end, error] =
            std::from_chars(namespaceText.data(), namespaceText.data() + namespaceText.size(), namespaceId);
        if (error != std::errc{} || end != namespaceText.data() + namespaceText.size() || namespaceId == 0)
            throw std::invalid_argument("assignPointIds: namespace must be a non-zero unsigned decimal string");
        auto assigned = value->assignPointIds(namespaceId);
        if (!assigned.ok()) throw std::invalid_argument("assignPointIds: point set contains duplicate identities");
    });
    points.addFunc("setFloatAttribute", &PointSet::setFloatAttribute);
    points.addFunc("getFloatAttribute", &PointSet::getFloatAttribute);
    points.addFunc("hasFloatAttribute", &PointSet::hasFloatAttribute);
    points.addFunc("setIntAttribute", &PointSet::setIntAttribute);
    points.addFunc("getIntAttribute", &PointSet::getIntAttribute);
    points.addFunc("hasIntAttribute", &PointSet::hasIntAttribute);
    points.addFunc("setBoolAttribute", &PointSet::setBoolAttribute);
    points.addFunc("getBoolAttribute", &PointSet::getBoolAttribute);
    points.addFunc("hasBoolAttribute", &PointSet::hasBoolAttribute);
    points.addFunc("setVectorAttribute", &PointSet::setVectorAttribute);
    points.addFunc("getVectorAttributeX", &PointSet::getVectorAttributeX);
    points.addFunc("getVectorAttributeY", &PointSet::getVectorAttributeY);
    points.addFunc("getVectorAttributeZ", &PointSet::getVectorAttributeZ);
    points.addFunc("hasVectorAttribute", &PointSet::hasVectorAttribute);
    points.addFunc("setStringAttribute", &PointSet::setStringAttribute);
    points.addFunc("getStringAttribute", &PointSet::getStringAttribute);
    points.addFunc("hasStringAttribute", &PointSet::hasStringAttribute);
    points.addFunc("getAttributeType", &PointSet::getAttributeType);

    auto lsystem = table.addClass<LSystem>("ProcgenLSystem",
                                           std::function<LSystem*()>([]() -> LSystem* { return nullptr; }), true);
    lsystem.addFunc("setAxiom", &LSystem::setAxiom);
    lsystem.addFunc("addRule", &LSystem::addRule);
    lsystem.addFunc("addRules", [](LSystem* ls, char symbol, ssq::Array productions, ssq::Array weights) {
        ls->addRules(symbol, productions.convert<std::string>(), weights.convert<float>());
    });
    lsystem.addFunc("clearRules", &LSystem::clearRules);
    lsystem.addFunc("setAngle", &LSystem::setAngle);
    lsystem.addFunc("setStep", &LSystem::setStep);
    lsystem.addFunc("setIterations", &LSystem::setIterations);
    lsystem.addFunc("setSeed", &LSystem::setSeed);
    lsystem.addFunc("getSeed", &LSystem::getSeed);
    lsystem.addFunc("getIterations", &LSystem::getIterations);
    lsystem.addFunc("setInitialHeading", &LSystem::setInitialHeading);
    lsystem.addFunc("setBranchRadius", &LSystem::setBranchRadius);
    lsystem.addFunc("setBranchRadiusFalloff", &LSystem::setBranchRadiusFalloff);
    lsystem.addFunc("setLeafSize", &LSystem::setLeafSize);
    lsystem.addFunc("setLeafSymbols", &LSystem::setLeafSymbols);
    lsystem.addFunc("setTropism", &LSystem::setTropism);
    lsystem.addFunc("derive", &LSystem::derive);
    lsystem.addFunc("trace", [](LSystem* ls, PointSet* out) {
        if (!out) throw std::invalid_argument("trace: null PointSet");
        ls->toPointSet(*out);
    });

    auto spatial = table.addClass<SpatialData>(
        "ProcgenSpatialData", std::function<SpatialData*()>([]() -> SpatialData* { return nullptr; }), true);
    spatial.addFunc("getKind", &SpatialData::getKind);
    spatial.addFunc("contains", &SpatialData::contains);
    spatial.addFunc("hasBounds", &SpatialData::hasBounds);
    spatial.addFunc("getMinX", &SpatialData::getMinX);
    spatial.addFunc("getMinY", &SpatialData::getMinY);
    spatial.addFunc("getMinZ", &SpatialData::getMinZ);
    spatial.addFunc("getMaxX", &SpatialData::getMaxX);
    spatial.addFunc("getMaxY", &SpatialData::getMaxY);
    spatial.addFunc("getMaxZ", &SpatialData::getMaxZ);

    auto pointDelta = table.addClass<PointDelta>(
        "ProcgenPointDelta", std::function<PointDelta*()>([]() -> PointDelta* { return nullptr; }), true);
    pointDelta.addFunc("getAddedCount", [](PointDelta* value) { return value->added.getCount(); });
    pointDelta.addFunc("getUpdatedCount", [](PointDelta* value) { return value->updated.getCount(); });
    pointDelta.addFunc("getRemovedCount", [](PointDelta* value) { return int(value->removed.size()); });
    pointDelta.addFunc("getTargetCount", [](PointDelta* value) { return int(value->targetOrder.size()); });
    pointDelta.addFunc("getAdded", [](PointDelta* value) { return new PointSet(value->added); });
    pointDelta.addFunc("getUpdated", [](PointDelta* value) { return new PointSet(value->updated); });
    pointDelta.addFunc("getRemovedId", [](PointDelta* value, int index) {
        if (index < 0 || std::size_t(index) >= value->removed.size())
            throw std::out_of_range("getRemovedId: index is out of range");
        return std::to_string(value->removed[std::size_t(index)]);
    });
    pointDelta.addFunc("getTargetId", [](PointDelta* value, int index) {
        if (index < 0 || std::size_t(index) >= value->targetOrder.size())
            throw std::out_of_range("getTargetId: index is out of range");
        return std::to_string(value->targetOrder[std::size_t(index)]);
    });
    pointDelta.addFunc("getBaseFingerprint", [](PointDelta* value) { return std::to_string(value->baseFingerprint); });
    pointDelta.addFunc("getTargetFingerprint",
                       [](PointDelta* value) { return std::to_string(value->targetFingerprint); });

    exposeRuntimeGeneration(table);

    auto context = table.addClass<ProcgenContext>(
        "ProcgenContext", std::function<ProcgenContext*()>([]() -> ProcgenContext* { return nullptr; }), true);
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

    auto ownedParams = table.addClass<ScriptProcgenParams>(
        "ProcgenOwnedParams", std::function<ScriptProcgenParams*()>([] { return nullptr; }), true);
    ownedParams.addFunc("ownership", [](ScriptProcgenParams*) { return std::string("owned"); });
    ownedParams.addFunc("ownerEpoch", [](ScriptProcgenParams* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedParams.addFunc("handle", [](ScriptProcgenParams* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedParams.addFunc("isStale",
                        [](ScriptProcgenParams* value) { return !value || Procgen::isStale(value->reference); });
    ownedParams.addFunc("release", [vm](ScriptProcgenParams* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "owned procgen params proxy must not be null",
                                                                      "params", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, Procgen::release(value->reference));
    });
    ownedParams.addFunc("setSeed", [vm](ScriptProcgenParams* value, uint32_t seed) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        view->setSeed(seed);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setSize", [vm](ScriptProcgenParams* value, int width, int height) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        view->setSize(width, height);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setInt", [vm](ScriptProcgenParams* value, const std::string& key, int number) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        view->setInt(key, number);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setFloat", [vm](ScriptProcgenParams* value, const std::string& key, float number) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        view->setFloat(key, number);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setBool", [vm](ScriptProcgenParams* value, const std::string& key, bool flag) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        view->setBool(key, flag);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setString", [vm](ScriptProcgenParams* value, const std::string& key, const std::string& text) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "params" + " handle is stale",
                        "params", {}, "procgen.squirrel")));
        view->setString(key, text);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("getSeed", [](ScriptProcgenParams* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() ? static_cast<int64_t>(view->getSeed()) : int64_t{0};
    });
    ownedParams.addFunc("getWidth", [](ScriptProcgenParams* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() ? view->getWidth() : 0;
    });
    ownedParams.addFunc("getHeight", [](ScriptProcgenParams* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() ? view->getHeight() : 0;
    });
    ownedParams.addFunc("has", [](ScriptProcgenParams* value, const std::string& key) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() && view->has(key);
    });
    ownedParams.addFunc("getInt", [](ScriptProcgenParams* value, const std::string& key, int fallback) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() ? view->getInt(key, fallback) : fallback;
    });
    ownedParams.addFunc("getFloat", [](ScriptProcgenParams* value, const std::string& key, float fallback) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() ? view->getFloat(key, fallback) : fallback;
    });
    ownedParams.addFunc("getBool", [](ScriptProcgenParams* value, const std::string& key, bool fallback) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() ? view->getBool(key, fallback) : fallback;
    });
    ownedParams.addFunc("getString",
                        [](ScriptProcgenParams* value, const std::string& key, const std::string& fallback) {
                            auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
                            return view.isBound() ? view->getString(key, fallback) : fallback;
                        });
    ownedParams.addFunc("canonicalString", [](ScriptProcgenParams* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() ? view->canonicalString() : std::string{};
    });

    auto ownedGrid = table.addClass<ScriptProcgenGrid>(
        "ProcgenOwnedGrid2D", std::function<ScriptProcgenGrid*()>([] { return nullptr; }), true);
    ownedGrid.addFunc("ownership", [](ScriptProcgenGrid*) { return std::string("owned"); });
    ownedGrid.addFunc("ownerEpoch", [](ScriptProcgenGrid* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedGrid.addFunc("handle", [](ScriptProcgenGrid* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedGrid.addFunc("isStale", [](ScriptProcgenGrid* value) { return !value || Procgen::isStale(value->reference); });
    ownedGrid.addFunc("release", [vm](ScriptProcgenGrid* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "owned procgen grid proxy must not be null",
                                                                      "grid", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, Procgen::release(value->reference));
    });
    ownedGrid.addFunc("resize", [vm](ScriptProcgenGrid* value, int width, int height) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        view->resize(width, height);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedGrid.addFunc("fill", [vm](ScriptProcgenGrid* value, int semantic) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        view->fill(semantic);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedGrid.addFunc("setCell", [vm](ScriptProcgenGrid* value, int x, int y, int semantic) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        view->setCell(x, y, semantic);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedGrid.addFunc("getWidth", [](ScriptProcgenGrid* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getWidth() : 0;
    });
    ownedGrid.addFunc("getHeight", [](ScriptProcgenGrid* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getHeight() : 0;
    });
    ownedGrid.addFunc("getCell", [](ScriptProcgenGrid* value, int x, int y) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getCell(x, y) : 0;
    });
    ownedGrid.addFunc("setDetail", [vm](ScriptProcgenGrid* value, int x, int y, int detail) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        view->setDetail(x, y, detail);
        return eve::script::projectResult(vm, eve::Result<void>::success());
    });
    ownedGrid.addFunc("getDetail", [](ScriptProcgenGrid* value, int x, int y) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getDetail(x, y) : 0;
    });
    ownedGrid.addFunc("getObjectCount", [](ScriptProcgenGrid* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectCount() : 0;
    });
    ownedGrid.addFunc("getObjectName", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectName(index) : std::string{};
    });
    ownedGrid.addFunc("getObjectType", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectType(index) : std::string{};
    });
    ownedGrid.addFunc("getObjectX", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectX(index) : 0;
    });
    ownedGrid.addFunc("getObjectY", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectY(index) : 0;
    });
    ownedGrid.addFunc("getObjectWidth", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectWidth(index) : 0;
    });
    ownedGrid.addFunc("getObjectHeight", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectHeight(index) : 0;
    });
    ownedGrid.addFunc("getObjectGid", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectGid(index) : 0;
    });
    ownedGrid.addFunc("getObjectAsset", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectAsset(index) : std::string{};
    });
    ownedGrid.addFunc("getObjectRotation", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectRotation(index) : 0.f;
    });
    ownedGrid.addFunc("getObjectFlags", [](ScriptProcgenGrid* value, int index) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getObjectFlags(index) : 0;
    });
    ownedGrid.addFunc("getMeta", [](ScriptProcgenGrid* value, const std::string& key, const std::string& fallback) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Grid2D>();
        return view.isBound() ? view->getMeta(key, fallback) : fallback;
    });
    ownedGrid.addFunc("setMeta", [vm](ScriptProcgenGrid* value, const std::string& key, const std::string& data) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, std::string("owned procgen ") + "grid" + " handle is stale",
                        "grid", {}, "procgen.squirrel")));
        view->setMeta(key, data);
        return eve::script::projectResult(vm, eve::Result<void>::success());
    });

    auto ownedContext = table.addClass<ScriptProcgenContext>(
        "ProcgenOwnedContext", std::function<ScriptProcgenContext*()>([] { return nullptr; }), true);
    ownedContext.addFunc("ownership", [](ScriptProcgenContext*) { return std::string("owned"); });
    ownedContext.addFunc("ownerEpoch", [](ScriptProcgenContext* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedContext.addFunc("handle", [](ScriptProcgenContext* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedContext.addFunc("isStale",
                         [](ScriptProcgenContext* value) { return !value || Procgen::isStale(value->reference); });
    ownedContext.addFunc("release", [vm](ScriptProcgenContext* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "owned procgen context proxy must not be null",
                                                                      "context", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, Procgen::release(value->reference));
    });
    ownedContext.addFunc("getName", [](ScriptProcgenContext* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() ? view->getName() : std::string{};
    });
    ownedContext.addFunc("isActive", [](ScriptProcgenContext* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() && view->isActive();
    });
    ownedContext.addFunc("hasFailed", [](ScriptProcgenContext* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() && view->hasFailed();
    });
    ownedContext.addFunc("seedFor", [](ScriptProcgenContext* value, const std::string& scope) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() ? view->seedFor(scope) : uint32_t{0};
    });
    ownedContext.addFunc("getError", [](ScriptProcgenContext* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() ? view->getError() : std::string("stale procgen context");
    });
    ownedContext.addFunc("beginTrace", [](ScriptProcgenContext* value, const std::string& stage, int inputCount) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() && view->beginTrace(stage, inputCount);
    });
    ownedContext.addFunc("endTrace", [](ScriptProcgenContext* value, int outputCount) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() && view->endTrace(outputCount);
    });
    ownedContext.addFunc("publish", [](ScriptProcgenContext* value, const std::string& name, PointSet* points) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() && view->publish(name, points);
    });
    ownedContext.addFunc("captureDebug", [](ScriptProcgenContext* value, const std::string& name, PointSet* points) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() && view->captureDebug(name, points);
    });
    ownedContext.addFunc(
        "reuseStage",
        [](ScriptProcgenContext* value, const std::string& name, const std::string& cacheKey) -> PointSet* {
            auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
            return view.isBound() ? view->reuseStage(name, cacheKey) : nullptr;
        });
    ownedContext.addFunc("cacheStage", [](ScriptProcgenContext* value, const std::string& name,
                                          const std::string& cacheKey, PointSet* points) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        return view.isBound() && view->cacheStage(name, cacheKey, points);
    });
    ownedContext.addFunc("fail", [](ScriptProcgenContext* value, const std::string& error) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<ProcgenContext>();
        if (view.isBound()) view->fail(error);
    });
    ownedContext.addFunc("commit", [vm](ScriptProcgenContext* value) {
        if (!value)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::StaleHandle,
                                                      std::string("owned procgen ") + "context" + " handle is stale",
                                                      "context", {}, "procgen.squirrel")));
        auto* module = liveProcgen();
        if (!module)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                      "Procgen module is no longer loaded", "context",
                                                                      {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, module->commitSystem(value->reference));
    });
    ownedContext.addFunc("abort", [vm](ScriptProcgenContext* value) {
        if (!value)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::StaleHandle,
                                                      std::string("owned procgen ") + "context" + " handle is stale",
                                                      "context", {}, "procgen.squirrel")));
        auto* module = liveProcgen();
        if (!module)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                      "Procgen module is no longer loaded", "context",
                                                                      {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, module->abortSystem(value->reference));
    });

    auto mesh = table.addClass<MeshBuild>("ProcgenMeshBuild",
                                          std::function<MeshBuild*()>([]() -> MeshBuild* { return nullptr; }), true);
    mesh.addFunc("clear", &MeshBuild::clear);
    mesh.addFunc("appendTransformed", &MeshBuild::appendTransformed);
    mesh.addFunc("setActiveGroup", &MeshBuild::setActiveGroup);
    mesh.addFunc("getGroupCount", &MeshBuild::getGroupCount);
    mesh.addFunc("getGroupName", &MeshBuild::getGroupName);
    mesh.addFunc("getTriangleGroup", &MeshBuild::getTriangleGroup);
    mesh.addFunc("copyGroup", [vm](MeshBuild* self, int groupIndex) -> ssq::Object {
        if (!self) return ssq::Object(vm);
        auto object = eve::script::makeOwnedSquirrelInstance<MeshBuild>(vm, self->copyGroup(groupIndex));
        if (!object) {
            object.ignore("failed to create copied mesh Squirrel instance");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
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
    mesh.addFunc("hasVertexColors", [](const MeshBuild* self) { return self && self->hasVertexColors(); });
    mesh.addFunc("getColor", [](const MeshBuild* self,int vertex,int component) {
        return self ? self->getColor(vertex,component) : 1.f;
    });
    mesh.addFunc("setColor", [vm](MeshBuild* self, int vertex, float r, float g, float b, float a) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshBuild is required", "color", {},
                                                                      "procgen.squirrel")));
        return eve::script::projectResult(vm, self->setColor(vertex, r, g, b, a));
    });
    mesh.addFunc("getIndex", &MeshBuild::getIndex);
    mesh.addFunc("setMeta", &MeshBuild::setMeta);
    mesh.addFunc("getMeta", &MeshBuild::getMeta);

    auto splitResult=table.addClass("GtsMeshSplitResult",ssq::Class::Ctor<GtsMeshSplitResult()>());
    splitResult.addFunc("getColumnCount",[](const GtsMeshSplitResult* value){return value?value->getColumnCount():0;});
    splitResult.addFunc("getRowCount",[](const GtsMeshSplitResult* value){return value?value->getRowCount():0;});
    splitResult.addFunc("getTileCount",[](const GtsMeshSplitResult* value){return value?value->getTileCount():0;});
    splitResult.addFunc("getTileOffsetX",[](const GtsMeshSplitResult* value,int index){return value?value->getTileOffsetX(index):0.f;});
    splitResult.addFunc("getTileOffsetZ",[](const GtsMeshSplitResult* value,int index){return value?value->getTileOffsetZ(index):0.f;});
    splitResult.addFunc("copyTileMesh",[vm](const GtsMeshSplitResult* self,int index)->ssq::Object {
        if(!self)return ssq::Object(vm);
        auto object=eve::script::makeOwnedSquirrelInstance<MeshBuild>(vm,self->copyTileMesh(index));
        if(!object){object.ignore("failed to create split mesh Squirrel instance");return ssq::Object(vm);}
        return std::move(object).takeValue();
    });
    table.addFunc(
        "splitGtsMesh", [vm](GtsMeshSplitResult* output, const MeshBuild* source, int xSplits, int zSplits, int pivot) {
            auto result = output && source
                              ? splitGtsMeshInto(*output, *source, xSplits, zSplits, static_cast<GtsMeshPivot>(pivot))
                              : eve::Result<void>::failure(eve::Diagnostic::error(
                                    DiagnosticCode::InvalidArgument, "GTS split output and source are required", "mesh",
                                    {}, "procgen.squirrel"));
            return eve::script::projectResult(vm, std::move(result));
        });

    auto pcgMeshTransform=table.addClass("PcgMeshTransform",ssq::Class::Ctor<PcgMeshTransform()>());
    pcgMeshTransform.addFunc("setElement", [vm](PcgMeshTransform* self, int row, int column, float value) {
        if (!self)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      DiagnosticCode::InvalidArgument, "Pcg mesh transform is required",
                                                      "mesh", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,self->setElement(row,column,value));
    });
    pcgMeshTransform.addFunc("getElement",[](const PcgMeshTransform* self,int row,int column){
        return self?self->getElement(row,column):0.F;
    });
    auto pcgMeshCombinePlan=table.addClass("PcgMeshCombinePlan",ssq::Class::Ctor<PcgMeshCombinePlan()>());
    pcgMeshCombinePlan.addFunc("appendSource", [vm](PcgMeshCombinePlan* self, const MeshBuild* source,
                                                    const PcgMeshTransform* transform, const std::string& materialId) {
        if (!self || !source || !transform)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "Pcg mesh combine plan, source and transform are required",
                        "mesh", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,self->appendSource(*source,*transform,materialId));
    });
    pcgMeshCombinePlan.addFunc("clear",[](PcgMeshCombinePlan* self){if(self)self->clear();});
    pcgMeshCombinePlan.addFunc("getSourceCount",[](const PcgMeshCombinePlan* self){
        return self?self->getSourceCount():0;
    });
    table.addFunc("combinePcgStaticMeshes", [vm](MeshBuild* output, const PcgMeshCombinePlan* plan) {
        if (!output || !plan)
            return eve::script::projectResult(
                vm,
                eve::Result<int>::failure(eve::Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    !output ? "Pcg mesh combine output is required" : "Pcg mesh combine plan is required", "mesh", {},
                    "procgen.squirrel")),
                [](int value) { return Value(static_cast<std::int64_t>(value)); });
        return eve::script::projectResult(vm,combinePcgStaticMeshesInto(*output,*plan),
                                          [](int value){return Value(static_cast<std::int64_t>(value));});
    });

    auto meshMergePlan = table.addClass("MeshMergePlan", ssq::Class::Ctor<MeshMergePlan()>());
    meshMergePlan.addFunc("appendSource",
                          [vm](MeshMergePlan* self, const MeshBuild* source, float tx, float ty, float tz, float yaw,
                               float sx, float sy, float sz, const std::string& materialId) {
                              if (!self || !source)
                                  return eve::script::projectResult(
                                      vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                              DiagnosticCode::InvalidArgument,
                                              "MeshMergePlan and source mesh are required", "merge", {},
                                              "procgen.squirrel")));
                              return eve::script::projectResult(
                                  vm, self->appendSource(*source, tx, ty, tz, yaw, sx, sy, sz, materialId));
                          });
    meshMergePlan.addFunc("clear", [](MeshMergePlan* self) {
        if (self) self->clear();
    });
    meshMergePlan.addFunc("getSourceCount",
                          [](const MeshMergePlan* self) { return self ? self->getSourceCount() : 0; });
    meshMergePlan.addFunc("setEnableContactBlend", [vm](MeshMergePlan* self, bool enabled) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshMergePlan is required", "merge", {},
                                                                      "procgen.squirrel")));
        return eve::script::projectResult(vm, self->setEnableContactBlend(enabled));
    });
    meshMergePlan.addFunc("getEnableContactBlend",
                          [](const MeshMergePlan* self) { return self && self->getEnableContactBlend(); });
    meshMergePlan.addFunc("setContactBlend", [vm](MeshMergePlan* self, float edgeRadius, float materialRadius,
                                                  float strength, float normalsBlend, float materialBlend,
                                                  float surfaceOffset, bool softSnap, const std::string& falloff) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshMergePlan is required", "merge", {},
                                                                      "procgen.squirrel")));
        MeshContactBlendParams params;
        params.edgeRadius        = edgeRadius;
        params.materialRadius    = materialRadius;
        params.strength          = strength;
        params.normalsBlend      = normalsBlend;
        params.materialBlend     = materialBlend;
        params.surfaceOffset     = surfaceOffset;
        params.softSnapPositions = softSnap;
        params.falloff           = falloff;
        return eve::script::projectResult(vm, self->setContactBlendParams(params));
    });
    meshMergePlan.addFunc("setWeldTolerance", [vm](MeshMergePlan* self, float tolerance) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshMergePlan is required", "merge", {},
                                                                      "procgen.squirrel")));
        return eve::script::projectResult(vm, self->setWeldTolerance(tolerance));
    });
    meshMergePlan.addFunc("setSimplifyQuality", [vm](MeshMergePlan* self, float quality) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshMergePlan is required", "merge", {},
                                                                      "procgen.squirrel")));
        return eve::script::projectResult(vm, self->setSimplifyQuality(quality));
    });
    meshMergePlan.addFunc("setPivotMode", [vm](MeshMergePlan* self, int mode) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshMergePlan is required", "merge", {},
                                                                      "procgen.squirrel")));
        return eve::script::projectResult(
            vm, self->setPivotMode(mode == 1 ? MeshMergePlan::PivotMode::WorldOrigin
                                             : MeshMergePlan::PivotMode::FirstSource));
    });
    table.addFunc("mergeStaticMeshes", [vm](const MeshMergePlan* plan) {
        if (!plan) {
            auto failed = eve::Result<MeshBuild>::failure(eve::Diagnostic::error(
                DiagnosticCode::InvalidArgument, "MeshMergePlan is required", "merge", {}, "procgen.squirrel"));
            return eve::script::projectStatusResult(vm, failed.status());
        }
        auto result = mergeStaticMeshes(*plan);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            vm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status());
        return eve::script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
    });

    auto meshAdhereLive = table.addClass("MeshAdhereLive", ssq::Class::Ctor<MeshAdhereLive()>());
    meshAdhereLive.addFunc("activate", [vm](MeshAdhereLive* self, const MeshBuild* source, const MeshBuild* surface) {
        if (!self || !source || !surface)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "MeshAdhereLive, source and surface are required", "adhere",
                        {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, self->activateResult(*source, *surface));
    });
    meshAdhereLive.addFunc("isActive", [](const MeshAdhereLive* self) { return self && self->isActive(); });
    meshAdhereLive.addFunc("getRevision",
                           [](const MeshAdhereLive* self) { return self ? static_cast<int>(self->revision()) : 0; });
    meshAdhereLive.addFunc("setParams", [vm](MeshAdhereLive* self, float edgeRadius, float materialRadius,
                                             float strength, float normalsBlend, float materialBlend,
                                             float surfaceOffset, bool softSnap, const std::string& falloff) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshAdhereLive is required", "adhere", {},
                                                                      "procgen.squirrel")));
        MeshContactBlendParams params;
        params.edgeRadius        = edgeRadius;
        params.materialRadius    = materialRadius;
        params.strength          = strength;
        params.normalsBlend      = normalsBlend;
        params.materialBlend     = materialBlend;
        params.surfaceOffset     = surfaceOffset;
        params.softSnapPositions = softSnap;
        params.falloff           = falloff;
        return eve::script::projectResult(vm, self->setParamsResult(params));
    });
    meshAdhereLive.addFunc("setSurface", [vm](MeshAdhereLive* self, const MeshBuild* surface) {
        if (!self || !surface)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshAdhereLive and surface are required",
                                                                      "adhere", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, self->setSurfaceResult(*surface));
    });
    meshAdhereLive.addFunc("setSource", [vm](MeshAdhereLive* self, const MeshBuild* source) {
        if (!self || !source)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "MeshAdhereLive and source are required",
                                                                      "adhere", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, self->setSourceResult(*source));
    });
    meshAdhereLive.addFunc("evaluate", [vm](MeshAdhereLive* self, bool force) {
        if (!self)
            return eve::script::projectResult(
                vm,
                eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "MeshAdhereLive is required", "adhere", {},
                                                                           "procgen.squirrel")),
                [](std::uint64_t value) { return Value(static_cast<std::int64_t>(value)); });
        return eve::script::projectResult(vm, self->evaluateResult(force),
                                          [](std::uint64_t value) { return Value(static_cast<std::int64_t>(value)); });
    });
    meshAdhereLive.addFunc("isDirty", [](const MeshAdhereLive* self) { return self && self->isDirty(); });
    meshAdhereLive.addFunc("removeSetup", [](MeshAdhereLive* self) {
        if (self) self->removeSetup();
    });
    meshAdhereLive.addFunc("derivedMeshResult", [vm](const MeshAdhereLive* self) {
        if (!self) {
            auto failed = eve::Result<MeshBuild>::failure(eve::Diagnostic::error(
                DiagnosticCode::InvalidArgument, "MeshAdhereLive is required", "adhere", {}, "procgen.squirrel"));
            return eve::script::projectStatusResult(vm, failed.status());
        }
        auto result = self->derivedMeshResult();
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            vm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status());
        return eve::script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
    });
    meshAdhereLive.addFunc("bakeToMesh", [vm](MeshAdhereLive* self) {
        if (!self) {
            auto failed = eve::Result<MeshBuild>::failure(eve::Diagnostic::error(
                DiagnosticCode::InvalidArgument, "MeshAdhereLive is required", "adhere", {}, "procgen.squirrel"));
            return eve::script::projectStatusResult(vm, failed.status());
        }
        auto result = self->bakeToMeshResult();
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            vm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status());
        return eve::script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
    });

    auto pcgMeshLodBackup=table.addClass("PcgMeshLodBackup",ssq::Class::Ctor<PcgMeshLodBackup()>());
    pcgMeshLodBackup.addFunc("capture", [vm](PcgMeshLodBackup* self, graphics::Renderable3D* renderable) {
        if (!self || !renderable)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "Pcg mesh LOD backup and renderable are required",
                                                                      "capture", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,self->capture(*renderable));
    });
    pcgMeshLodBackup.addFunc("restore", [vm](PcgMeshLodBackup* self, graphics::Renderable3D* renderable) {
        if (!self || !renderable)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "Pcg mesh LOD backup and renderable are required",
                                                                      "restore", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,self->restore(*renderable));
    });
    pcgMeshLodBackup.addFunc("discard",[](PcgMeshLodBackup* self){if(self)self->discard();});
    pcgMeshLodBackup.addFunc("isCaptured",[](const PcgMeshLodBackup* self){return self&&self->isCaptured();});
    pcgMeshLodBackup.addFunc("getEntityId",[](const PcgMeshLodBackup* self){return self?self->getEntityId():0u;});
    pcgMeshLodBackup.addFunc("getEntityGeneration",[](const PcgMeshLodBackup* self){
        return self?self->getEntityGeneration():0u;
    });

    auto pcgMeshLodProfile=table.addClass("PcgMeshLodProfile",ssq::Class::Ctor<PcgMeshLodProfile()>());
    pcgMeshLodProfile.addFunc("appendLevel",[vm](PcgMeshLodProfile* self,float transition,float fade,float quality,
                                                   bool combineMeshes,bool combineSubMeshes){
        return eve::script::projectResult(vm,self->appendLevel(transition,fade,quality,combineMeshes,combineSubMeshes));
    });
    pcgMeshLodProfile.addFunc("clear",[](PcgMeshLodProfile* self){self->clear();});
    pcgMeshLodProfile.addFunc("getLevelCount",[](const PcgMeshLodProfile* self){return self->getLevelCount();});
    pcgMeshLodProfile.addFunc("getTransitionHeight",[](const PcgMeshLodProfile* self,int index){
        const auto* level=self?self->levelAt(index):nullptr;return level?level->screenRelativeTransitionHeight:-1.F;
    });
    pcgMeshLodProfile.addFunc("getFadeWidth",[](const PcgMeshLodProfile* self,int index){
        const auto* level=self?self->levelAt(index):nullptr;return level?level->fadeTransitionWidth:-1.F;
    });
    pcgMeshLodProfile.addFunc("getQuality",[](const PcgMeshLodProfile* self,int index){
        const auto* level=self?self->levelAt(index):nullptr;return level?level->quality:-1.F;
    });
    pcgMeshLodProfile.addFunc("setLevelRendererState", [vm](PcgMeshLodProfile* self, int index, int skinQuality,
                                                            int shadowMode, bool receiveShadows, int motionMode,
                                                            bool skinnedMotion, int lightProbe, int reflectionProbe) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "Pcg mesh LOD profile is required",
                                                                      "renderer-state", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,self->setLevelRendererState(index,skinQuality,shadowMode,
            receiveShadows,motionMode,skinnedMotion,lightProbe,reflectionProbe));
    });
    pcgMeshLodProfile.addFunc("getLevelRendererState",[](const PcgMeshLodProfile* self,int index,int field){
        return self?self->getLevelRendererState(index,field):-1;
    });
    pcgMeshLodProfile.addFunc("setFadePolicy", [vm](PcgMeshLodProfile* self, int mode, bool animate, float duration) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "Pcg mesh LOD profile is required", "fade-policy",
                                                                      {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,self->setFadePolicy(mode,animate,duration));
    });
    pcgMeshLodProfile.addFunc("getFadeMode",[](const PcgMeshLodProfile* self){return self->getFadeMode();});
    pcgMeshLodProfile.addFunc("getAnimateCrossFading",[](const PcgMeshLodProfile* self){
        return self->getAnimateCrossFading();
    });
    pcgMeshLodProfile.addFunc("getCrossFadeAnimationDuration",[](const PcgMeshLodProfile* self){
        return self->getCrossFadeAnimationDuration();
    });
    auto pcgMeshLods=table.addClass("PcgMeshLodSet",ssq::Class::Ctor<PcgMeshLodSet()>());
    pcgMeshLods.addFunc("getLevelCount",[](const PcgMeshLodSet* self){return self->getLevelCount();});
    pcgMeshLods.addFunc("getTransitionHeight",[](const PcgMeshLodSet* self,int index){return self->getTransitionHeight(index);});
    pcgMeshLods.addFunc("getFadeWidth",[](const PcgMeshLodSet* self,int index){return self->getFadeWidth(index);});
    pcgMeshLods.addFunc("getQuality",[](const PcgMeshLodSet* self,int index){return self->getQuality(index);});
    pcgMeshLods.addFunc("selectLevel",[](const PcgMeshLodSet* self,float height){return self->selectLevel(height);});
    pcgMeshLods.addFunc("getSwitchDistance",[](const PcgMeshLodSet* self,int level,float diameter,float fov){
        return self->getSwitchDistance(level,diameter,fov);
    });
    pcgMeshLods.addFunc("copyLevelMesh",[vm](const PcgMeshLodSet* self,int level)->ssq::Object{
        const auto* mesh=self?self->meshAt(level):nullptr;if(!mesh)return ssq::Object(vm);
        auto object=eve::script::makeOwnedSquirrelInstance<MeshBuild>(vm,std::make_unique<MeshBuild>(*mesh));
        if(!object){object.ignore("failed to create Pcg LOD mesh Squirrel instance");return ssq::Object(vm);}
        return std::move(object).takeValue();
    });
    table.addFunc(
        "buildPcgMeshLods", [vm](PcgMeshLodSet* output, const MeshBuild* source, const PcgMeshLodProfile* profile) {
            if (!output || !source || !profile)
                return eve::script::projectResult(
                    vm, eve::Result<void>::failure(eve::Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "Pcg mesh LOD output, source and profile are required",
                            "mesh", {}, "procgen.squirrel")));
            return eve::script::projectResult(vm, buildPcgMeshLodsInto(*output, *source, *profile));
        });
    table.addFunc("buildPcgCombinedMeshLods", [vm](PcgMeshLodSet* output, const PcgMeshCombinePlan* plan,
                                                   const PcgMeshLodProfile* profile) {
        if (!output || !plan || !profile)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "Pcg combined mesh LOD output, plan and profile are required",
                        "mesh", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,buildPcgCombinedMeshLodsInto(*output,*plan,*profile));
    });
    table.addFunc(
        "configurePcgMeshLods", [vm](Procgen* self, const PcgMeshLodSet* lods, graphics::Renderable3D* renderable,
                                     graphics::Graphics* gfx, float diameter, float fov) {
            if (!self || !lods || !renderable || !gfx)
                return eve::script::projectResult(
                    vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                          "Pcg mesh LOD runtime arguments are required",
                                                                          "runtime", {}, "procgen.squirrel")));
            return eve::script::projectResult(vm, self->configurePcgMeshLods(*lods, *renderable, *gfx, diameter, fov));
        });

    auto terrainMeshSettings=table.addClass("GtsTerrainMeshSettings",ssq::Class::Ctor<GtsTerrainMeshSettings()>());
    terrainMeshSettings.addFunc("getSaveResolution",[](const GtsTerrainMeshSettings* self){return self->getSaveResolution();});
    terrainMeshSettings.addFunc("setSaveResolution",[vm](GtsTerrainMeshSettings* self,int value){return eve::script::projectResult(vm,self->setSaveResolution(value));});
    terrainMeshSettings.addFunc("getLodCount",[](const GtsTerrainMeshSettings* self){return self->getLodCount();});
    terrainMeshSettings.addFunc("setLodCount",[vm](GtsTerrainMeshSettings* self,int value){return eve::script::projectResult(vm,self->setLodCount(value));});
    terrainMeshSettings.addFunc("getSubTiles",[](const GtsTerrainMeshSettings* self){return self->getSubTiles();});
    terrainMeshSettings.addFunc("setSubTiles",[vm](GtsTerrainMeshSettings* self,int value){return eve::script::projectResult(vm,self->setSubTiles(value));});
    terrainMeshSettings.addFunc("getLodQuality",[](const GtsTerrainMeshSettings* self,int index){return self->getLodQuality(index);});
    terrainMeshSettings.addFunc("setLodQuality",[vm](GtsTerrainMeshSettings* self,int index,float value){return eve::script::projectResult(vm,self->setLodQuality(index,value));});
    terrainMeshSettings.addFunc("getLodTransitionHeight",[](const GtsTerrainMeshSettings* self,int index){return self->getLodTransitionHeight(index);});
    terrainMeshSettings.addFunc("snapshotJson",[vm](const GtsTerrainMeshSettings* self){return eve::script::projectResult(vm,self->snapshotJson(),[](const std::string&value){return value;});});
    terrainMeshSettings.addFunc("restoreJson",[vm](GtsTerrainMeshSettings* self,const std::string&json){return eve::script::projectResult(vm,self->restoreJson(json));});
    auto terrainExportSettings=table.addClass("GtsTerrainExportSettings",ssq::Class::Ctor<GtsTerrainExportSettings()>());
    terrainExportSettings.addFunc("appendSourcePreset",[vm](GtsTerrainExportSettings* self,int mode,int level,float quality,float transition){return eve::script::projectResult(vm,self->appendSourcePreset(mode,level,quality,transition));});
    terrainExportSettings.addFunc("appendImpostorPreset",[vm](GtsTerrainExportSettings* self,int mode,int level,float quality,float transition){return eve::script::projectResult(vm,self->appendImpostorPreset(mode,level,quality,transition));});
    terrainExportSettings.addFunc("getSourceLodCount",[](const GtsTerrainExportSettings* self){return self->getSourceLodCount();});
    terrainExportSettings.addFunc("getImpostorLodCount",[](const GtsTerrainExportSettings* self){return self->getImpostorLodCount();});
    terrainExportSettings.addFunc("snapshotJson",[vm](const GtsTerrainExportSettings* self){return eve::script::projectResult(vm,self->snapshotJson(),[](const std::string& value){return value;});});
    terrainExportSettings.addFunc("restoreJson",[vm](GtsTerrainExportSettings* self,const std::string& json){return eve::script::projectResult(vm,self->restoreJson(json));});
    auto terrainLods=table.addClass("GtsTerrainLodSet",ssq::Class::Ctor<GtsTerrainLodSet()>());
    terrainLods.addFunc("getColumnCount",&GtsTerrainLodSet::getColumnCount);
    terrainLods.addFunc("getRowCount",&GtsTerrainLodSet::getRowCount);
    terrainLods.addFunc("getTileCount",&GtsTerrainLodSet::getTileCount);
    terrainLods.addFunc("getLevelCount",&GtsTerrainLodSet::getLevelCount);
    terrainLods.addFunc("getTileOffsetX",[](const GtsTerrainLodSet* self,int tile){auto* value=self?self->tileAt(tile):nullptr;return value?value->offsetX:0.f;});
    terrainLods.addFunc("getTileOffsetZ",[](const GtsTerrainLodSet* self,int tile){auto* value=self?self->tileAt(tile):nullptr;return value?value->offsetZ:0.f;});
    terrainLods.addFunc("getLevelQuality",[](const GtsTerrainLodSet* self,int level){auto* value=self?self->levelAt(level):nullptr;return value?value->quality:0.f;});
    terrainLods.addFunc("getLevelTransitionHeight",[](const GtsTerrainLodSet* self,int level){auto* value=self?self->levelAt(level):nullptr;return value?value->screenRelativeTransitionHeight:0.f;});
    terrainLods.addFunc("selectLevel",&GtsTerrainLodSet::selectLevel);
    terrainLods.addFunc("getLevelSwitchDistance",&GtsTerrainLodSet::getLevelSwitchDistance);
    terrainLods.addFunc("selectLevelForCamera",&GtsTerrainLodSet::selectLevelForCamera);
    terrainLods.addFunc("snapshotJson",[vm](const GtsTerrainLodSet* self){
        return eve::script::projectResult(vm,self->snapshotJson(),[](const std::string& value){return value;});
    });
    terrainLods.addFunc("restoreJson",[vm](GtsTerrainLodSet* self,const std::string& json){
        return eve::script::projectResult(vm,self->restoreJson(json));
    });
    terrainLods.addFunc("copyLevelMesh",[vm](const GtsTerrainLodSet* self,int tile,int level)->ssq::Object{
        auto* value=self?self->tileAt(tile):nullptr;if(!value||level<0||level>=static_cast<int>(value->levels.size()))return ssq::Object(vm);
        auto object=eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            vm,std::make_unique<MeshBuild>(value->levels[level]));
        if(!object){object.ignore("failed to create terrain LOD mesh Squirrel instance");return ssq::Object(vm);}
        return std::move(object).takeValue();
    });
    table.addFunc("buildDefaultGtsTerrainLods", [vm](GtsTerrainLodSet* output, const MeshBuild* source, int xSplits,
                                                     int zSplits, int pivot) {
        if (!output || !source)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "GTS terrain LOD output and source are required",
                                                                      "mesh", {}, "procgen.squirrel")));
        auto built=buildGtsTerrainLods(*source,xSplits,zSplits,static_cast<GtsMeshPivot>(pivot),defaultGtsTerrainLodLevels());
        if(!built)return eve::script::projectResult(vm,Result<void>::failure(*built.error()));
        *output=std::move(built).takeValue();
        return eve::script::projectResult(vm,Result<void>::success());
    });
    table.addFunc("buildGtsTerrainBaseMesh", [vm](MeshBuild* output, const Heightmap* heightmap, int resolution,
                                                  float sizeX, float sizeY, float sizeZ) {
        if (!output || !heightmap)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "GTS terrain mesh output and heightmap are required",
                        "heightmap", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,buildGtsTerrainBaseMesh(*output,*heightmap,
            static_cast<GtsTerrainSaveResolution>(resolution),sizeX,sizeY,sizeZ));
    });
    table.addFunc("buildDefaultGtsTerrainLodsFromHeightmap", [vm](GtsTerrainLodSet* output, const Heightmap* heightmap,
                                                                  int resolution, float sizeX, float sizeY, float sizeZ,
                                                                  int subTiles, int pivot) {
        if (!output || !heightmap)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "GTS terrain LOD output and heightmap are required",
                        "heightmap", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,buildDefaultGtsTerrainLodsFromHeightmapInto(*output,*heightmap,
            static_cast<GtsTerrainSaveResolution>(resolution),sizeX,sizeY,sizeZ,subTiles,static_cast<GtsMeshPivot>(pivot)));
    });
    table.addFunc("buildGtsTerrainLodsFromHeightmap", [vm](GtsTerrainLodSet* output, const Heightmap* heightmap,
                                                           const GtsTerrainMeshSettings* settings, float sizeX,
                                                           float sizeY, float sizeZ, int pivot) {
        if (!output || !heightmap || !settings)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "GTS terrain LOD output, heightmap and settings are required",
                        "heightmap", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,buildGtsTerrainLodsFromHeightmapInto(*output,*heightmap,*settings,
            sizeX,sizeY,sizeZ,static_cast<GtsMeshPivot>(pivot)));
    });
    table.addFunc("buildGtsTerrainExportLodsFromHeightmap", [vm](GtsTerrainLodSet* output, const Heightmap* heightmap,
                                                                 const GtsTerrainExportSettings* settings, float sizeX,
                                                                 float sizeY, float sizeZ, int subTiles, int pivot) {
        if (!output || !heightmap || !settings)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      DiagnosticCode::InvalidArgument,
                                                      "GTS terrain export output, heightmap and settings are required",
                                                      "heightmap", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,buildGtsTerrainExportLodsFromHeightmapInto(*output,*heightmap,*settings,
            sizeX,sizeY,sizeZ,subTiles,static_cast<GtsMeshPivot>(pivot)));
    });
    table.addFunc("buildGtsTerrainColliderMeshFromHeightmap", [vm](MeshBuild* output, const Heightmap* heightmap,
                                                                   const GtsTerrainExportSettings* settings,
                                                                   float sizeX, float sizeY, float sizeZ) {
        if (!output || !heightmap || !settings)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "GTS collider output, heightmap and settings are required",
                        "heightmap", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,buildGtsTerrainColliderMeshFromHeightmapInto(
            *output,*heightmap,settings->getWorkflow(),sizeX,sizeY,sizeZ));
    });
    table.addFunc("encodeGtsTerrainObj", [vm](const Heightmap* heightmap, int resolution, float sizeX, float sizeY,
                                              float sizeZ, int faceMode) {
        if (!heightmap)
            return eve::script::projectResult(vm,
                                              eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                  DiagnosticCode::InvalidArgument, "GTS OBJ heightmap is required",
                                                  "heightmap", {}, "procgen.squirrel")),
                                              [](const std::string& value) { return value; });
        return eve::script::projectResult(vm,encodeGtsTerrainObj(*heightmap,static_cast<GtsTerrainSaveResolution>(resolution),
            sizeX,sizeY,sizeZ,static_cast<GtsTerrainObjFaceMode>(faceMode)),[](const std::string& value){return value;});
    });
    table.addFunc("encodeGtsMaskedTerrainObj", [vm](const Heightmap* heightmap, const Heightmap* maskmap,
                                                    int resolution, float sizeX, float sizeY, float sizeZ, int faceMode,
                                                    float threshold, bool invert) {
        if (!heightmap || !maskmap)
            return eve::script::projectResult(
                vm,
                eve::Result<std::string>::failure(eve::Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "GTS masked OBJ heightmap and maskmap are required", "heightmap",
                    {}, "procgen.squirrel")),
                [](const std::string& value) { return value; });
        return eve::script::projectResult(vm,encodeGtsMaskedTerrainObj(*heightmap,*maskmap,
            static_cast<GtsTerrainSaveResolution>(resolution),sizeX,sizeY,sizeZ,
            static_cast<GtsTerrainObjFaceMode>(faceMode),threshold,invert),[](const std::string& value){return value;});
    });
    table.addFunc("bakeGtsTerrainVertexColors", [vm](MeshBuild* output, const MeshBuild* source,
                                                     const image::ImageData* bakedTexture, int edgeMode,
                                                     int smoothingIterations, float terrainSizeX, float terrainSizeZ,
                                                     bool linearize) {
        if (!output || !source || !bakedTexture)
            return eve::script::projectResult(
                vm,
                eve::Result<int>::failure(eve::Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "GTS vertex-color output, source and texture are required", "mesh",
                    {}, "procgen.squirrel")),
                [](int value) { return value; });
        return eve::script::projectResult(vm,bakeGtsTerrainVertexColorsInto(*output,*source,*bakedTexture,
            static_cast<GtsTerrainNormalEdgeMode>(edgeMode),smoothingIterations,terrainSizeX,terrainSizeZ,linearize),
            [](int value){return value;});
    });
    auto terrainLodRuntime=table.addClass("GtsTerrainLodRuntime",ssq::Class::Ctor<GtsTerrainLodRuntime()>());
    terrainLodRuntime.addFunc("replace", [vm](GtsTerrainLodRuntime* self, const GtsTerrainLodSet* lods,
                                              Procgen* procgen, graphics::Graphics* gfx, float diameter, float fov,
                                              float x, float y, float z) {
        if (!self || !lods || !procgen || !gfx)
            return eve::script::projectResult(
                vm,
                eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "GTS terrain runtime replace arguments are required", "runtime",
                    {}, "procgen.squirrel")),
                [](std::uint64_t v) { return static_cast<std::int64_t>(v); });
        return eve::script::projectResult(vm,self->replace(*lods,*procgen,*gfx,diameter,fov,x,y,z),[](std::uint64_t v){return static_cast<std::int64_t>(v);});
    });
    terrainLodRuntime.addFunc("replaceAndHideSource", [vm](GtsTerrainLodRuntime* self, const GtsTerrainLodSet* lods,
                                                           Procgen* procgen, graphics::Graphics* gfx,
                                                           graphics::Renderable3D* source, float diameter, float fov,
                                                           float x, float y, float z) {
        if (!self || !lods || !procgen || !gfx || !source)
            return eve::script::projectResult(
                vm,
                eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "GTS terrain runtime source handoff arguments are required",
                    "runtime", {}, "procgen.squirrel")),
                [](std::uint64_t v) { return static_cast<std::int64_t>(v); });
        return eve::script::projectResult(vm,self->replaceAndHideSource(*lods,*procgen,*gfx,*source,diameter,fov,x,y,z),
            [](std::uint64_t v){return static_cast<std::int64_t>(v);});
    });
    terrainLodRuntime.addFunc("clear",[vm](GtsTerrainLodRuntime* self){return eve::script::projectResult(vm,self->clear(),[](int v){return v;});});
    terrainLodRuntime.addFunc("applyMaterial", [vm](GtsTerrainLodRuntime* self, graphics::Material* material) {
        if (!self || !material)
            return eve::script::projectResult(vm,
                                              eve::Result<int>::failure(eve::Diagnostic::error(
                                                  DiagnosticCode::InvalidArgument, "GTS terrain material is required",
                                                  "material", {}, "procgen.squirrel")),
                                              [](int v) { return v; });
        return eve::script::projectResult(vm,self->applyMaterial(*material),[](int v){return v;});
    });
    terrainLodRuntime.addFunc("getTileCount",&GtsTerrainLodRuntime::getTileCount);
    terrainLodRuntime.addFunc("getRevision",&GtsTerrainLodRuntime::getRevision);
    terrainLodRuntime.addFunc("getRenderable",&GtsTerrainLodRuntime::getRenderable);
    terrainLodRuntime.addFunc("getSourceTerrain",&GtsTerrainLodRuntime::getSourceTerrain);
    auto terrainLodAssets=table.addClass("GtsTerrainLodAssetPlan",ssq::Class::Ctor<GtsTerrainLodAssetPlan()>());
    terrainLodAssets.addFunc("getEntryCount",&GtsTerrainLodAssetPlan::getEntryCount);
    terrainLodAssets.addFunc("getTileIndex",&GtsTerrainLodAssetPlan::getTileIndex);
    terrainLodAssets.addFunc("getLevelIndex",&GtsTerrainLodAssetPlan::getLevelIndex);
    terrainLodAssets.addFunc("getObjectName",&GtsTerrainLodAssetPlan::getObjectName);
    terrainLodAssets.addFunc("getMeshName",&GtsTerrainLodAssetPlan::getMeshName);
    terrainLodAssets.addFunc("getRelativePath",&GtsTerrainLodAssetPlan::getRelativePath);
    table.addFunc("planGtsTerrainLodAssets", [vm](GtsTerrainLodAssetPlan* output, const GtsTerrainLodSet* lods,
                                                  const std::string& name, const std::string& folder) {
        if (!output || !lods)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "GTS terrain export output and LOD set are required", "assets",
                        {}, "procgen.squirrel")));
        return eve::script::projectResult(vm,planGtsTerrainLodAssetsInto(*output,*lods,name,folder));
    });

    auto sampler = table.addClass<TerrainSampler>(
        "ProcgenTerrainSampler", std::function<TerrainSampler*()>([]() -> TerrainSampler* { return nullptr; }), true);
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

    exposeHeightmap(table);
    exposeTerrainImageAdapter(table);
    exposeTerrainCurveTexture(table);
    exposeTerrainGrassAdapter(table);

    auto terrainLayers = table.addClass<TerrainLayers>(
        "ProcgenTerrainLayers", std::function<TerrainLayers*()>([]() -> TerrainLayers* { return nullptr; }), true);
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
        "ProcgenTerrainErosionMap", std::function<TerrainErosionMap*()>([]() -> TerrainErosionMap* { return nullptr; }),
        true);
    erosionMap.addFunc("getWidth", &TerrainErosionMap::getWidth);
    erosionMap.addFunc("getHeight", &TerrainErosionMap::getHeight);
    erosionMap.addFunc("getWear", &TerrainErosionMap::getWear);
    erosionMap.addFunc("getDeposition", &TerrainErosionMap::getDeposition);
    erosionMap.addFunc("getHeightDelta", &TerrainErosionMap::getHeightDelta);

    auto terrainMesh = table.addClass<TerrainMeshChunk>(
        "ProcgenTerrainMeshChunk", std::function<TerrainMeshChunk*()>([]() -> TerrainMeshChunk* { return nullptr; }),
        true);
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
    terrainMesh.addFunc("getPositionX",
                        [](const TerrainMeshChunk* c, int i) { return c ? c->mesh().getPositionX(i) : 0.f; });
    terrainMesh.addFunc("getPositionY",
                        [](const TerrainMeshChunk* c, int i) { return c ? c->mesh().getPositionY(i) : 0.f; });
    terrainMesh.addFunc("getPositionZ",
                        [](const TerrainMeshChunk* c, int i) { return c ? c->mesh().getPositionZ(i) : 0.f; });
    terrainMesh.addFunc("getNormalX",
                        [](const TerrainMeshChunk* c, int i) { return c ? c->mesh().getNormalX(i) : 0.f; });
    terrainMesh.addFunc("getNormalY",
                        [](const TerrainMeshChunk* c, int i) { return c ? c->mesh().getNormalY(i) : 0.f; });
    terrainMesh.addFunc("getNormalZ",
                        [](const TerrainMeshChunk* c, int i) { return c ? c->mesh().getNormalZ(i) : 0.f; });
    terrainMesh.addFunc("getIndex", [](const TerrainMeshChunk* c, int i) { return c ? c->mesh().getIndex(i) : 0; });

    auto cloud = table.addClass<CloudField>(
        "ProcgenCloudField", std::function<CloudField*()>([]() -> CloudField* { return nullptr; }), true);
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
        "ProcgenCloudShadow", std::function<CloudShadow*()>([]() -> CloudShadow* { return nullptr; }), true);
    cloudShadow.addFunc("setSunDirection", &CloudShadow::setSunDirection);
    cloudShadow.addFunc("setCloudAltitude", &CloudShadow::setCloudAltitude);
    cloudShadow.addFunc("setStrength", &CloudShadow::setStrength);
    cloudShadow.addFunc("coverageAt", &CloudShadow::coverageAt);
    cloudShadow.addFunc("shadowFactorAt", &CloudShadow::shadowFactorAt);
    auto pbr = table.addClass<PbrTextureSet>(
        "ProcgenPbrMaterial", std::function<PbrTextureSet*()>([]() -> PbrTextureSet* { return nullptr; }), true);
    pbr.addFunc("destroy", &PbrTextureSet::destroy);
    pbr.addFunc("getAlbedo", &PbrTextureSet::getAlbedo);
    pbr.addFunc("getNormal", &PbrTextureSet::getNormal);
    pbr.addFunc("getRoughness", &PbrTextureSet::getRoughness);
    pbr.addFunc("getMetallic", &PbrTextureSet::getMetallic);
    pbr.addFunc("getHeight", &PbrTextureSet::getHeight);
    pbr.addFunc("getAo", &PbrTextureSet::getAo);
    pbr.addFunc("getAlbedoWidth", [](const PbrTextureSet* s) { return s->albedo->getWidth(); });
    pbr.addFunc("getAlbedoHeight", [](const PbrTextureSet* s) { return s->albedo->getHeight(); });
    pbr.addFunc("getNormalWidth", [](const PbrTextureSet* s) { return s->normal->getWidth(); });
    pbr.addFunc("getRoughnessWidth", [](const PbrTextureSet* s) { return s->roughness->getWidth(); });
    pbr.addFunc("getMetallicWidth", [](const PbrTextureSet* s) { return s->metallic->getWidth(); });
    pbr.addFunc("getHeightWidth", [](const PbrTextureSet* s) { return s->height->getWidth(); });
    pbr.addFunc("getAoWidth", [](const PbrTextureSet* s) { return s->ao->getWidth(); });
    pbr.addFunc("hasAllMaps", [](const PbrTextureSet* s) {
        return s->albedo && s->normal && s->roughness && s->metallic && s->height && s->ao;
    });
}


}  // namespace eve::procgen
