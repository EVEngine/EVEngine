#include "procgen/heightmap/TerrainStampScript.h"
#include "common/Capability.h"
#include "common/ProcgenProbeSink.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainDetailPlacement.h"
#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/PcgTerrainWatcher.h"
#include "procgen/heightmap/PcgTreeManager.h"
#include "procgen/heightmap/PcgBiomeController.h"
#include "procgen/heightmap/PcgRuntimeStamper.h"
#include "procgen/PcgSpawnProgress.h"
#include "procgen/heightmap/TerrainImageMask.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/heightmap/TerrainTerraceRemover.h"
#include "procgen/heightmap/TerrainWaterField.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"

#include "common/SquirrelBinding.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainBiomePreset.h"
#include "procgen/heightmap/TerrainBakedMaskCache.h"
#include "procgen/heightmap/TerrainCollisionMask.h"
#include "procgen/heightmap/TerrainPolygonMask.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainNoiseMask.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainStamp.h"

#include <functional>

namespace eve::procgen {
namespace {
Result<int> missingRaster() {
    return Result<int>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain operation requires non-null rasters and settings"));
}
}  // namespace

void exposeHeightmap(ssq::Table& table) {
    auto spawnProgress = table.addClass("PcgSpawnProgress", ssq::Class::Ctor<PcgSpawnProgress()>());
    spawnProgress.addFunc("updateRule", [vm = table.getHandle()](PcgSpawnProgress* progress,
        const std::string& name, int total, int done, int localTotal, int localDone) {
        return eve::script::projectResult(vm, progress->updateRule(name, total, done, localTotal, localDone),
            [](PcgSpawnProgressStatus value) { return eve::Value(static_cast<int>(value)); });
    });
    spawnProgress.addFunc("updateRuleFraction", [vm = table.getHandle()](PcgSpawnProgress* progress, float value) {
        return eve::script::projectResult(vm, progress->updateRuleFraction(value),
            [](PcgSpawnProgressStatus status) { return eve::Value(static_cast<int>(status)); });
    });
    spawnProgress.addFunc("requestCancel", [vm = table.getHandle()](PcgSpawnProgress* progress) {
        return eve::script::projectResult(vm, progress->requestCancel(),
            [](PcgSpawnProgressStatus status) { return eve::Value(static_cast<int>(status)); });
    });
    spawnProgress.addFunc("clear", [](PcgSpawnProgress* progress) { return static_cast<int>(progress->clear()); });
    spawnProgress.addFunc("getProgress", [](const PcgSpawnProgress* progress) { return float(progress->progress()); });
    spawnProgress.addFunc("getTitle", [](const PcgSpawnProgress* progress) { return progress->title(); });
    spawnProgress.addFunc("getSubtitle", [](const PcgSpawnProgress* progress) { return progress->subtitle(); });
    spawnProgress.addFunc("getStatus", [](const PcgSpawnProgress* progress) { return static_cast<int>(progress->status()); });

    auto runtimeStamper = table.addClass("PcgRuntimeStamper", ssq::Class::Ctor<PcgRuntimeStamper()>());
    runtimeStamper.addFunc("configure", [vm = table.getHandle()](PcgRuntimeStamper* stamper,
                                                                   const std::string& address, bool showGui,
                                                                   bool showDebug) {
        return eve::script::projectResult(vm, stamper->configure(address, showGui, showDebug));
    });
    runtimeStamper.addFunc("loadStamp", [vm = table.getHandle()](PcgRuntimeStamper* stamper,
                                                                  const Heightmap* stamp,
                                                                  const std::string& resourceName) {
        auto result = stamp ? stamper->loadStamp(*stamp, resourceName)
                            : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "stamp resource is required", {}, {}, "procgen.pcgRuntimeStamper"));
        return eve::script::projectResult(vm, std::move(result));
    });
    runtimeStamper.addFunc("reportMissingStamp", [vm = table.getHandle()](PcgRuntimeStamper* stamper) {
        return eve::script::projectResult(vm, stamper->reportMissingStamp());
    });
    runtimeStamper.addFunc("execute", [vm = table.getHandle()](PcgRuntimeStamper* stamper, Heightmap* target,
                                                                float originX, float originZ, float spacingX,
                                                                float spacingZ) {
        auto result = target ? stamper->execute(*target, originX, originZ, spacingX, spacingZ) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    runtimeStamper.addFunc("updateLayout", [vm = table.getHandle()](PcgRuntimeStamper* stamper,
                                                                     float width, float height) {
        return eve::script::projectResult(vm, stamper->updateLayout(width, height));
    });
    runtimeStamper.addFunc("getStatus", [](const PcgRuntimeStamper* stamper) {
        return static_cast<int>(stamper->status());
    });
    runtimeStamper.addFunc("getStampAddress", [](const PcgRuntimeStamper* stamper) {
        return stamper->stampAddress();
    });
    runtimeStamper.addFunc("getProgressText", [](const PcgRuntimeStamper* stamper) {
        return stamper->progressText();
    });
    runtimeStamper.addFunc("getUpdateTimeAllowed", [](const PcgRuntimeStamper* stamper) {
        return static_cast<float>(stamper->updateTimeAllowed());
    });
    runtimeStamper.addFunc("getLabelCenterX", [](const PcgRuntimeStamper* stamper) {
        return static_cast<float>(stamper->labelCenterX());
    });
    runtimeStamper.addFunc("getLabelCenterY", [](const PcgRuntimeStamper* stamper) {
        return static_cast<float>(stamper->labelCenterY());
    });

    auto biomeController = table.addClass("PcgBiomeController", ssq::Class::Ctor<PcgBiomeController()>());
    biomeController.addFunc("configure", [vm=table.getHandle()](PcgBiomeController* controller,
        float x,float y,float z,float range,float impostorRange,int mode) {
        return eve::script::projectResult(vm,controller->configure(x,y,z,range,impostorRange,
            static_cast<PcgBiomeLoadMode>(mode)));
    });
    biomeController.addFunc("fitToTerrain", [vm=table.getHandle()](PcgBiomeController* controller,
        float x,float y,float z,float sx,float sy,float sz) {
        return eve::script::projectResult(vm,controller->fitToTerrain(x,y,z,sx,sy,sz));
    });
    biomeController.addFunc("fitToAllTerrains", [vm=table.getHandle()](PcgBiomeController* controller,
        float x,float y,float z,float sx,float sy,float sz) {
        return eve::script::projectResult(vm,controller->fitToAllTerrains(x,y,z,sx,sy,sz));
    });
    biomeController.addFunc("tierAt", [](const PcgBiomeController* controller,float x,float y,float z) {
        return static_cast<int>(controller->tierAt(x,y,z));
    });
    biomeController.addFunc("getRange", [](const PcgBiomeController* controller) {
        return static_cast<float>(controller->range());
    });
    biomeController.addFunc("getLoadMode", [](const PcgBiomeController* controller) {
        return static_cast<int>(controller->loadMode());
    });
    biomeController.addFunc("getRegularCenterX", [](const PcgBiomeController* controller) {
        return static_cast<float>(controller->regularBounds().centerX);
    });
    biomeController.addFunc("getRegularCenterY", [](const PcgBiomeController* controller) {
        return static_cast<float>(controller->regularBounds().centerY);
    });
    biomeController.addFunc("getRegularCenterZ", [](const PcgBiomeController* controller) {
        return static_cast<float>(controller->regularBounds().centerZ);
    });
    biomeController.addFunc("getRegularSize", [](const PcgBiomeController* controller) {
        return static_cast<float>(controller->regularBounds().sizeX);
    });
    biomeController.addFunc("getImpostorSize", [](const PcgBiomeController* controller) {
        return static_cast<float>(controller->impostorBounds().sizeX);
    });
    auto treeManager = table.addClass("PcgTreeManager", ssq::Class::Ctor<PcgTreeManager()>());
    treeManager.addFunc("reset", [vm=table.getHandle()](PcgTreeManager* manager,
        float x,float z,float width,float depth) {
        return eve::script::projectResult(vm,manager->reset(x,z,width,depth));
    });
    treeManager.addFunc("addTree", [vm=table.getHandle()](PcgTreeManager* manager,
        float x,float z,int prototype) {
        return eve::script::projectResult(vm,manager->addTree(x,z,prototype),
                                          [](int value){return eve::Value(value);});
    });
    treeManager.addFunc("addTrees", [vm=table.getHandle()](PcgTreeManager* manager,
        const PointSet* points,int prototype) {
        auto result=points?manager->addTrees(*points,prototype):
            Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                "PointSet is required","procgen.pcgTreeManager"));
        return eve::script::projectResult(vm,std::move(result),
                                          [](int value){return eve::Value(value);});
    });
    treeManager.addFunc("countInRange", [vm=table.getHandle()](const PcgTreeManager* manager,
        float x,float z,float range) {
        return eve::script::projectResult(vm,manager->countInRange(x,z,range),
                                          [](int value){return eve::Value(value);});
    });
    treeManager.addFunc("getCount", [](const PcgTreeManager* manager){return manager->getCount();});
    auto terrainWatcher = table.addClass("PcgTerrainWatcher", ssq::Class::Ctor<PcgTerrainWatcher()>());
    terrainWatcher.addFunc("beginScan", &PcgTerrainWatcher::beginScan);
    terrainWatcher.addFunc("addTerrain", [vm=table.getHandle()](PcgTerrainWatcher* watcher,
                                                                  const std::string& id) {
        return eve::script::projectResult(vm, watcher->addTerrain(id),
                                          [](int value) { return eve::Value(value); });
    });
    terrainWatcher.addFunc("commitScan", [vm=table.getHandle()](PcgTerrainWatcher* watcher) {
        return eve::script::projectResult(vm, watcher->commitScan(),
                                          [](int value) { return eve::Value(value); });
    });
    terrainWatcher.addFunc("cancelScan", &PcgTerrainWatcher::cancelScan);
    terrainWatcher.addFunc("getTerrainCount", [](const PcgTerrainWatcher* watcher) {
        return watcher->getTerrainCount();
    });
    terrainWatcher.addFunc("getChangeCount", [](const PcgTerrainWatcher* watcher) {
        return watcher->getChangeCount();
    });
    terrainWatcher.addFunc("getChangeTerrainId", [vm=table.getHandle()](const PcgTerrainWatcher* watcher,
                                                                          int index) {
        return eve::script::projectResult(vm, watcher->getChangeTerrainId(index),
                                          [](const std::string& value) { return eve::Value(value); });
    });
    terrainWatcher.addFunc("getChangeType", [vm=table.getHandle()](const PcgTerrainWatcher* watcher,
                                                                     int index) {
        return eve::script::projectResult(vm, watcher->getChangeType(index),
                                          [](int value) { return eve::Value(value); });
    });
    auto splatmap = table.addClass("TerrainSplatmap", ssq::Class::Ctor<TerrainSplatmap()>());
    splatmap.addFunc("initialize", [vm = table.getHandle()](TerrainSplatmap* target, int width, int height,
                                                             int layers, int defaultLayer) {
        return eve::script::projectResult(vm, target->initialize(width, height, layers, defaultLayer),
                                          [](int n) { return eve::Value(n); });
    });
    splatmap.addFunc("sample", [vm = table.getHandle()](const TerrainSplatmap* target, int layer, int x, int y) {
        return eve::script::projectResult(vm, target->sample(layer, x, y),
                                          [](float value) { return eve::Value(value); });
    });
    splatmap.addFunc("copyLayer", [vm = table.getHandle()](const TerrainSplatmap* target, int layer,
                                                            Heightmap* output) {
        auto result = output ? target->copyLayer(layer, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    splatmap.addFunc("paint", [vm = table.getHandle()](TerrainSplatmap* target, const Heightmap* paint, int layer) {
        auto result = paint ? paintTerrainSplatLayer(*target, *paint, layer) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    splatmap.addFunc("getWidth", [](const TerrainSplatmap* target) { return target->getWidth(); });
    splatmap.addFunc("getHeight", [](const TerrainSplatmap* target) { return target->getHeight(); });
    splatmap.addFunc("getLayerCount", [](const TerrainSplatmap* target) { return target->getLayerCount(); });
    splatmap.addFunc("getLastChangedSamples",
                     [](const TerrainSplatmap* target) { return target->getLastChangedSamples(); });
    auto textureAlign =
        table.addClass("TerrainTextureAlignSettings", ssq::Class::Ctor<TerrainTextureAlignSettings()>());
    textureAlign.addVar("terrainAOriginX", &TerrainTextureAlignSettings::terrainAOriginX);
    textureAlign.addVar("terrainAOriginZ", &TerrainTextureAlignSettings::terrainAOriginZ);
    textureAlign.addVar("terrainAWidth", &TerrainTextureAlignSettings::terrainAWidth);
    textureAlign.addVar("terrainADepth", &TerrainTextureAlignSettings::terrainADepth);
    textureAlign.addVar("terrainBOriginX", &TerrainTextureAlignSettings::terrainBOriginX);
    textureAlign.addVar("terrainBOriginZ", &TerrainTextureAlignSettings::terrainBOriginZ);
    textureAlign.addVar("terrainBWidth", &TerrainTextureAlignSettings::terrainBWidth);
    textureAlign.addVar("terrainBDepth", &TerrainTextureAlignSettings::terrainBDepth);
    textureAlign.addVar("blendStrength", &TerrainTextureAlignSettings::blendStrength);
    textureAlign.addVar("blendWidth", &TerrainTextureAlignSettings::blendWidth);
    textureAlign.addVar("adjacencyTolerance", &TerrainTextureAlignSettings::adjacencyTolerance);
    table.addFunc("alignTerrainSplatTextures",
                  [vm = table.getHandle()](TerrainSplatmap* terrainA, TerrainSplatmap* terrainB,
                                           const TerrainTextureAlignSettings* settings) {
        auto result = terrainA && terrainB && settings
                          ? alignTerrainSplatTextures(*terrainA, *terrainB, *settings)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    auto heightBlend = table.addClass("GtsHeightBlendSet", ssq::Class::Ctor<GtsHeightBlendSet()>());
    heightBlend.addFunc("addLayer", [vm = table.getHandle()](GtsHeightBlendSet* target,
                                                               const Heightmap* height, float contrast,
                                                               float brightness, float increase) {
        auto result = height ? target->addLayer(*height, contrast, brightness, increase) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightBlend.addFunc("getLayerCount", [](const GtsHeightBlendSet* target) { return target->getLayerCount(); });
    table.addFunc("applyGtsHeightBlend",
                  [vm = table.getHandle()](TerrainSplatmap* output, const TerrainSplatmap* input,
                                           const GtsHeightBlendSet* heights, float blendFactor) {
        auto result = output && input && heights ? applyGtsHeightBlend(*output, *input, *heights, blendFactor)
                                                 : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });

    auto multiSplat = table.addClass("TerrainMultiSplatWorkspace", ssq::Class::Ctor<TerrainMultiSplatWorkspace()>());
    multiSplat.addFunc("addTile", [vm = table.getHandle()](TerrainMultiSplatWorkspace* workspace,
                                                            const std::string& name,
                                                            const TerrainSplatmap* splat, float originX,
                                                            float originZ, float width, float depth, bool worldMap) {
        auto result = splat ? workspace->addTile(name, *splat, originX, originZ, width, depth, worldMap)
                            : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiSplat.addFunc("paint", [vm = table.getHandle()](TerrainMultiSplatWorkspace* workspace,
                                                          const Heightmap* paint, int targetLayer,
                                                          const TerrainStampSettings* operation, bool worldMap) {
        auto result = paint && operation ? workspace->paint(*paint, targetLayer, *operation, worldMap)
                                         : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiSplat.addFunc("copyTile", [vm = table.getHandle()](const TerrainMultiSplatWorkspace* workspace,
                                                             const std::string& name, TerrainSplatmap* output) {
        auto result = output ? workspace->copyTile(name, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiSplat.addFunc("undo", [vm = table.getHandle()](TerrainMultiSplatWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->undo(), [](int n) { return eve::Value(n); });
    });
    multiSplat.addFunc("redo", [vm = table.getHandle()](TerrainMultiSplatWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->redo(), [](int n) { return eve::Value(n); });
    });
    multiSplat.addFunc("getTileCount", [](const TerrainMultiSplatWorkspace* workspace) {
        return workspace->getTileCount();
    });
    multiSplat.addFunc("getLastChangedSamples", [](const TerrainMultiSplatWorkspace* workspace) {
        return workspace->getLastChangedSamples();
    });
    multiSplat.addFunc("getLastAffectedTiles", [](const TerrainMultiSplatWorkspace* workspace) {
        return workspace->getLastAffectedTiles();
    });
    multiSplat.addFunc("getOperationCount", [](const TerrainMultiSplatWorkspace* workspace) {
        return workspace->getOperationCount();
    });
    multiSplat.addFunc("getAppliedCount", [](const TerrainMultiSplatWorkspace* workspace) {
        return workspace->getAppliedCount();
    });

    auto objectInstance =
        table.addClass("TerrainObjectInstanceSettings", ssq::Class::Ctor<TerrainObjectInstanceSettings()>());
    objectInstance.addVar("asset", &TerrainObjectInstanceSettings::asset);
    objectInstance.addVar("minimumInstances", &TerrainObjectInstanceSettings::minimumInstances);
    objectInstance.addVar("maximumInstances", &TerrainObjectInstanceSettings::maximumInstances);
    objectInstance.addVar("failureRate", &TerrainObjectInstanceSettings::failureRate);
    objectInstance.addVar("minimumOffsetX", &TerrainObjectInstanceSettings::minimumOffsetX);
    objectInstance.addVar("maximumOffsetX", &TerrainObjectInstanceSettings::maximumOffsetX);
    objectInstance.addVar("minimumOffsetY", &TerrainObjectInstanceSettings::minimumOffsetY);
    objectInstance.addVar("maximumOffsetY", &TerrainObjectInstanceSettings::maximumOffsetY);
    objectInstance.addVar("minimumOffsetZ", &TerrainObjectInstanceSettings::minimumOffsetZ);
    objectInstance.addVar("maximumOffsetZ", &TerrainObjectInstanceSettings::maximumOffsetZ);
    objectInstance.addVar("customOffset", &TerrainObjectInstanceSettings::customOffset);
    objectInstance.addVar("commonScale", &TerrainObjectInstanceSettings::commonScale);
    objectInstance.addVar("minimumScale", &TerrainObjectInstanceSettings::minimumScale);
    objectInstance.addVar("maximumScale", &TerrainObjectInstanceSettings::maximumScale);
    objectInstance.addVar("minimumScaleX", &TerrainObjectInstanceSettings::minimumScaleX);
    objectInstance.addVar("maximumScaleX", &TerrainObjectInstanceSettings::maximumScaleX);
    objectInstance.addVar("minimumScaleY", &TerrainObjectInstanceSettings::minimumScaleY);
    objectInstance.addVar("maximumScaleY", &TerrainObjectInstanceSettings::maximumScaleY);
    objectInstance.addVar("minimumScaleZ", &TerrainObjectInstanceSettings::minimumScaleZ);
    objectInstance.addVar("maximumScaleZ", &TerrainObjectInstanceSettings::maximumScaleZ);
    objectInstance.addVar("scaleRandomPercentage", &TerrainObjectInstanceSettings::scaleRandomPercentage);
    objectInstance.addVar("scaleRandomPercentageX", &TerrainObjectInstanceSettings::scaleRandomPercentageX);
    objectInstance.addVar("scaleRandomPercentageY", &TerrainObjectInstanceSettings::scaleRandomPercentageY);
    objectInstance.addVar("scaleRandomPercentageZ", &TerrainObjectInstanceSettings::scaleRandomPercentageZ);
    objectInstance.addVar("minimumRotationX", &TerrainObjectInstanceSettings::minimumRotationX);
    objectInstance.addVar("maximumRotationX", &TerrainObjectInstanceSettings::maximumRotationX);
    objectInstance.addVar("minimumRotationY", &TerrainObjectInstanceSettings::minimumRotationY);
    objectInstance.addVar("maximumRotationY", &TerrainObjectInstanceSettings::maximumRotationY);
    objectInstance.addVar("minimumRotationZ", &TerrainObjectInstanceSettings::minimumRotationZ);
    objectInstance.addVar("maximumRotationZ", &TerrainObjectInstanceSettings::maximumRotationZ);
    objectInstance.addVar("yOffsetAlongSlope", &TerrainObjectInstanceSettings::yOffsetAlongSlope);
    objectInstance.addVar("alignForwardToSlope", &TerrainObjectInstanceSettings::alignForwardToSlope);
    objectInstance.addVar("rotateToSlope", &TerrainObjectInstanceSettings::rotateToSlope);
    objectInstance.addFunc("setYOffsetMode", [](TerrainObjectInstanceSettings* value, int mode) {
        value->yOffsetMode = static_cast<TerrainObjectYOffsetMode>(mode);
    });
    objectInstance.addFunc("setScaleMode", [](TerrainObjectInstanceSettings* value, int mode) {
        value->scaleMode = static_cast<TerrainObjectScaleMode>(mode);
    });

    auto object = table.addClass("TerrainObjectPlacementSettings", ssq::Class::Ctor<TerrainObjectPlacementSettings()>());
    object.addVar("originX", &TerrainObjectPlacementSettings::originX);
    object.addVar("originZ", &TerrainObjectPlacementSettings::originZ);
    object.addVar("width", &TerrainObjectPlacementSettings::width);
    object.addVar("depth", &TerrainObjectPlacementSettings::depth);
    object.addVar("heightScale", &TerrainObjectPlacementSettings::heightScale);
    object.addVar("spacing", &TerrainObjectPlacementSettings::spacing);
    object.addVar("spawnDensity", &TerrainObjectPlacementSettings::spawnDensity);
    object.addVar("jitterPercent", &TerrainObjectPlacementSettings::jitterPercent);
    object.addVar("startOffsetX", &TerrainObjectPlacementSettings::startOffsetX);
    object.addVar("startOffsetZ", &TerrainObjectPlacementSettings::startOffsetZ);
    object.addVar("failureRate", &TerrainObjectPlacementSettings::failureRate);
    object.addVar("minimumFitness", &TerrainObjectPlacementSettings::minimumFitness);
    object.addVar("minimumInstanceFitness", &TerrainObjectPlacementSettings::minimumInstanceFitness);
    object.addVar("minimumDirection", &TerrainObjectPlacementSettings::minimumDirection);
    object.addVar("maximumDirection", &TerrainObjectPlacementSettings::maximumDirection);
    object.addVar("boundsRadius", &TerrainObjectPlacementSettings::boundsRadius);
    object.addVar("boundsCheckQuality", &TerrainObjectPlacementSettings::boundsCheckQuality);
    object.addVar("prototypeScale", &TerrainObjectPlacementSettings::prototypeScale);
    object.addVar("boundsCollisionCheck", &TerrainObjectPlacementSettings::boundsCollisionCheck);
    object.addVar("seaLevel", &TerrainObjectPlacementSettings::seaLevel);
    object.addVar("seed", &TerrainObjectPlacementSettings::seed);
    object.addVar("namespaceId", &TerrainObjectPlacementSettings::namespaceId);
    object.addVar("maxPoints", &TerrainObjectPlacementSettings::maxPoints);
    object.addVar("prototype", &TerrainObjectPlacementSettings::prototype);
    object.addFunc("addInstance", [vm = table.getHandle()](TerrainObjectPlacementSettings* settings,
                                                            const TerrainObjectInstanceSettings* instance) {
        auto result = instance ? settings->addInstance(*instance) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    object.addFunc("exportPoints", [vm = table.getHandle()](const TerrainObjectPlacementSettings* settings,
                                                             PointSet* output, const Heightmap* fitness,
                                                             const Heightmap* heights) {
        auto result = output && fitness && heights ? exportTerrainObjectPoints(*output, *fitness, *heights, *settings)
                                                   : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    object.addFunc("removePoints", [vm = table.getHandle()](const TerrainObjectPlacementSettings* settings,
                                                             PointSet* output, const PointSet* input,
                                                             const Heightmap* fitness, float strength) {
        auto result = output && input && fitness
                          ? removeTerrainObjectPoints(*output, *input, *fitness, *settings, strength)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });

    auto multiObject =
        table.addClass("TerrainMultiObjectWorkspace", ssq::Class::Ctor<TerrainMultiObjectWorkspace()>());
    multiObject.addFunc("addTile", [vm = table.getHandle()](TerrainMultiObjectWorkspace* workspace,
                                                              const std::string& name, const PointSet* objects,
                                                              const Heightmap* heights, float originX, float originZ,
                                                              float width, float depth, int resolutionX,
                                                              int resolutionY, bool worldMap) {
        auto result = objects && heights
                          ? workspace->addTile(name, *objects, *heights, originX, originZ, width, depth,
                                               resolutionX, resolutionY, worldMap)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiObject.addFunc("apply", [vm = table.getHandle()](TerrainMultiObjectWorkspace* workspace,
                                                            const Heightmap* fitness,
                                                            const TerrainObjectPlacementSettings* settings,
                                                            const TerrainStampSettings* operation, int mode,
                                                            bool worldMap) {
        auto result = fitness && settings && operation
                          ? workspace->apply(*fitness, *settings, *operation,
                                             static_cast<TerrainObjectOperationMode>(mode), worldMap)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiObject.addFunc("copyTile", [vm = table.getHandle()](const TerrainMultiObjectWorkspace* workspace,
                                                               const std::string& name, PointSet* output) {
        auto result = output ? workspace->copyTile(name, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiObject.addFunc("undo", [vm = table.getHandle()](TerrainMultiObjectWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->undo(), [](int n) { return eve::Value(n); });
    });
    multiObject.addFunc("redo", [vm = table.getHandle()](TerrainMultiObjectWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->redo(), [](int n) { return eve::Value(n); });
    });
    multiObject.addFunc("getTileCount", [](const TerrainMultiObjectWorkspace* workspace) {
        return workspace->getTileCount();
    });
    multiObject.addFunc("getLastChangedSamples", [](const TerrainMultiObjectWorkspace* workspace) {
        return workspace->getLastChangedSamples();
    });
    multiObject.addFunc("getLastAffectedTiles", [](const TerrainMultiObjectWorkspace* workspace) {
        return workspace->getLastAffectedTiles();
    });
    multiObject.addFunc("getOperationCount", [](const TerrainMultiObjectWorkspace* workspace) {
        return workspace->getOperationCount();
    });
    multiObject.addFunc("getAppliedCount", [](const TerrainMultiObjectWorkspace* workspace) {
        return workspace->getAppliedCount();
    });

    auto worldSettings =
        table.addClass("TerrainWorldCreationSettings", ssq::Class::Ctor<TerrainWorldCreationSettings()>());
    worldSettings.addVar("tilesX", &TerrainWorldCreationSettings::tilesX);
    worldSettings.addVar("tilesZ", &TerrainWorldCreationSettings::tilesZ);
    worldSettings.addVar("tileSize", &TerrainWorldCreationSettings::tileSize);
    worldSettings.addVar("tileHeight", &TerrainWorldCreationSettings::tileHeight);
    worldSettings.addVar("centerX", &TerrainWorldCreationSettings::centerX);
    worldSettings.addVar("centerZ", &TerrainWorldCreationSettings::centerZ);
    worldSettings.addVar("heightmapResolution", &TerrainWorldCreationSettings::heightmapResolution);
    worldSettings.addVar("controlTextureResolution", &TerrainWorldCreationSettings::controlTextureResolution);
    worldSettings.addVar("detailResolution", &TerrainWorldCreationSettings::detailResolution);
    worldSettings.addVar("treeResolution", &TerrainWorldCreationSettings::treeResolution);
    worldSettings.addVar("objectResolution", &TerrainWorldCreationSettings::objectResolution);
    worldSettings.addVar("splatLayers", &TerrainWorldCreationSettings::splatLayers);
    worldSettings.addVar("defaultSplatLayer", &TerrainWorldCreationSettings::defaultSplatLayer);
    worldSettings.addVar("defaultDetailDensity", &TerrainWorldCreationSettings::defaultDetailDensity);
    worldSettings.addVar("worldMap", &TerrainWorldCreationSettings::worldMap);
    worldSettings.addVar("namePrefix", &TerrainWorldCreationSettings::namePrefix);
    worldSettings.addVar("nameSuffix", &TerrainWorldCreationSettings::nameSuffix);

    auto probeSettings = table.addClass("TerrainProbePlacementSettings",
                                        ssq::Class::Ctor<TerrainProbePlacementSettings()>());
    probeSettings.addVar("name", &TerrainProbePlacementSettings::name);
    probeSettings.addVar("originX", &TerrainProbePlacementSettings::originX);
    probeSettings.addVar("originZ", &TerrainProbePlacementSettings::originZ);
    probeSettings.addVar("width", &TerrainProbePlacementSettings::width);
    probeSettings.addVar("depth", &TerrainProbePlacementSettings::depth);
    probeSettings.addVar("heightScale", &TerrainProbePlacementSettings::heightScale);
    probeSettings.addVar("spacing", &TerrainProbePlacementSettings::spacing);
    probeSettings.addVar("jitterPercent", &TerrainProbePlacementSettings::jitterPercent);
    probeSettings.addVar("minimumFitness", &TerrainProbePlacementSettings::minimumFitness);
    probeSettings.addVar("seaLevelActive", &TerrainProbePlacementSettings::seaLevelActive);
    probeSettings.addVar("seaLevel", &TerrainProbePlacementSettings::seaLevel);
    probeSettings.addVar("reflectionOffset", &TerrainProbePlacementSettings::reflectionOffset);
    probeSettings.addVar("lightOffset", &TerrainProbePlacementSettings::lightOffset);
    probeSettings.addVar("reflectionResolution", &TerrainProbePlacementSettings::reflectionResolution);
    probeSettings.addVar("reflectionClipDistance", &TerrainProbePlacementSettings::reflectionClipDistance);
    probeSettings.addVar("reflectionShadowDistance", &TerrainProbePlacementSettings::reflectionShadowDistance);
    probeSettings.addVar("seed", &TerrainProbePlacementSettings::seed);
    probeSettings.addVar("namespaceId", &TerrainProbePlacementSettings::namespaceId);
    probeSettings.addVar("maxPoints", &TerrainProbePlacementSettings::maxPoints);
    table.addFunc("generateTerrainProbes",
                  [vm = table.getHandle()](PointSet* output, const Heightmap* fitness, const Heightmap* heights,
                                           const TerrainProbePlacementSettings* settings, int type) {
        auto result = [&]() -> Result<int> {
            if (!output || !fitness || !heights || !settings) return missingRaster();
            auto copy = *settings;
            copy.type = static_cast<TerrainProbeType>(type);
            return exportTerrainProbePoints(*output, *fitness, *heights, copy);
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("publishTerrainProbes", [vm = table.getHandle()](const std::string& batchId,
                                                                     const PointSet* points) {
        auto result = [&]() -> Result<int> {
            if (!points)
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "terrain.probes.publish: point set required"));
            auto* sink = eve::cap::query<eve::IProcgenProbeSink>();
            if (!sink)
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                               "terrain.probes.publish: graphics provider unavailable"));
            std::vector<eve::ProcgenProbeDesc> descriptions;
            descriptions.reserve(static_cast<std::size_t>(points->getCount()));
            for (int i = 0; i < points->getCount(); ++i) {
                const auto& point = points->points()[static_cast<std::size_t>(i)];
                eve::ProcgenProbeDesc desc;
                desc.sourcePointId = point.id;
                desc.resource = points->getStringAttribute(i, "probeResource", "");
                desc.type = static_cast<int>(points->getIntAttribute(i, "probeType", -1));
                desc.x = point.x; desc.y = point.y; desc.z = point.z;
                desc.extentX = std::max(0.5F, (point.boundsMaxX - point.boundsMinX) * 0.5F);
                desc.extentY = std::max(0.5F, (point.boundsMaxY - point.boundsMinY) * 0.5F);
                desc.extentZ = std::max(0.5F, (point.boundsMaxZ - point.boundsMinZ) * 0.5F);
                desc.resolution = static_cast<int>(points->getIntAttribute(i, "reflectionResolution", 128));
                desc.clipDistance = points->getFloatAttribute(i, "reflectionClipDistance", 1000);
                desc.shadowDistance = points->getFloatAttribute(i, "reflectionShadowDistance", 80);
                desc.irradianceR = points->getFloatAttribute(i, "probeIrradianceR", 0.12F);
                desc.irradianceG = points->getFloatAttribute(i, "probeIrradianceG", 0.12F);
                desc.irradianceB = points->getFloatAttribute(i, "probeIrradianceB", 0.14F);
                desc.hasSphericalHarmonics = points->getIntAttribute(i, "probeHasSphericalHarmonics", 0) != 0;
                if (desc.hasSphericalHarmonics) {
                    static constexpr char channels[] = {'R', 'G', 'B'};
                    for (int coefficient = 0; coefficient < 9; ++coefficient)
                        for (int channel = 0; channel < 3; ++channel) {
                            const std::string name = "probeSh" + std::to_string(coefficient) + channels[channel];
                            desc.sphericalHarmonics[static_cast<size_t>(coefficient * 3 + channel)] =
                                points->getFloatAttribute(i, name, 0.f);
                        }
                }
                descriptions.push_back(std::move(desc));
            }
            return sink->replaceProbeBatch(batchId, descriptions);
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("removeTerrainProbeBatch", [vm = table.getHandle()](const std::string& batchId) {
        auto* sink = eve::cap::query<eve::IProcgenProbeSink>();
        auto result = sink ? sink->removeProbeBatch(batchId)
                           : Result<int>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                 "terrain.probes.remove: graphics provider unavailable"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("tickTerrainProbeBatches", [vm = table.getHandle()](int faces, int filters, int samples) {
        auto* sink = eve::cap::query<eve::IProcgenProbeSink>();
        auto result = sink ? sink->tickProbeBatches(faces, filters, samples)
                           : Result<int>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                 "terrain.probes.tick: graphics provider unavailable"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("getTerrainProbeBatchCount", [](const std::string& batchId) {
        auto* sink = eve::cap::query<eve::IProcgenProbeSink>();
        return sink ? sink->probeCount(batchId) : 0;
    });
    table.addFunc("getTerrainReflectionProbeCount", [](const std::string& batchId) {
        auto* sink = eve::cap::query<eve::IProcgenProbeSink>();
        return sink ? sink->reflectionProbeCount(batchId) : 0;
    });
    table.addFunc("getTerrainLightProbeCount", [](const std::string& batchId) {
        auto* sink = eve::cap::query<eve::IProcgenProbeSink>();
        return sink ? sink->lightProbeCount(batchId) : 0;
    });

    auto spawnPlan = table.addClass("TerrainSpawnPlan", ssq::Class::Ctor<TerrainSpawnPlan()>());
    spawnPlan.addFunc("addSplat", [vm = table.getHandle()](TerrainSpawnPlan* self, const std::string& ruleId,
                                                              const Heightmap* paint, int targetLayer,
                                                              const TerrainStampSettings* operation) {
        auto result = paint && operation ? self->addSplat(ruleId, *paint, targetLayer, *operation)
                                         : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    spawnPlan.addFunc("addDetail", [vm = table.getHandle()](TerrainSpawnPlan* self, const std::string& ruleId,
                                                               const Heightmap* fitness,
                                                               const TerrainDetailSettings* settings,
                                                               const TerrainStampSettings* operation, int mode,
                                                               int seed) {
        auto result = fitness && settings && operation
                          ? self->addDetail(ruleId, *fitness, *settings, *operation,
                                             static_cast<TerrainDetailMode>(mode), seed)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    spawnPlan.addFunc("addTrees", [vm = table.getHandle()](TerrainSpawnPlan* self, const std::string& ruleId,
                                                              const Heightmap* fitness,
                                                              const TerrainTreePlacementSettings* settings,
                                                              const TerrainStampSettings* operation, int mode) {
        auto result = fitness && settings && operation
                          ? self->addTrees(ruleId, *fitness, *settings, *operation,
                                            static_cast<TerrainTreeOperationMode>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    spawnPlan.addFunc("addObjects", [vm = table.getHandle()](TerrainSpawnPlan* self, const std::string& ruleId,
                                                                const Heightmap* fitness,
                                                                const TerrainObjectPlacementSettings* settings,
                                                                const TerrainStampSettings* operation, int mode) {
        auto result = fitness && settings && operation
                          ? self->addObjects(ruleId, *fitness, *settings, *operation,
                                              static_cast<TerrainObjectOperationMode>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    spawnPlan.addFunc("addProbes", [vm = table.getHandle()](TerrainSpawnPlan* self, const std::string& ruleId,
                                                               const Heightmap* fitness,
                                                               const TerrainProbePlacementSettings* settings,
                                                               const TerrainStampSettings* operation, int mode,
                                                               int type) {
        auto result = [&]() -> Result<int> {
            if (!fitness || !settings || !operation) return missingRaster();
            auto copy = *settings;
            copy.type = static_cast<TerrainProbeType>(type);
            return self->addProbes(ruleId, *fitness, copy, *operation,
                                   static_cast<TerrainProbeOperationMode>(mode));
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    spawnPlan.addFunc("addModifierStamp", [vm = table.getHandle()](TerrainSpawnPlan* self,
                                                                    const std::string& ruleId,
                                                                    const Heightmap* stamp,
                                                                    const TerrainStampSettings* operation,
                                                                    int operationType,
                                                                    const Heightmap* localMask,
                                                                    const Heightmap* globalMask) {
        auto result = [&]() -> Result<int> {
            if (!stamp || !operation || !localMask || !globalMask) return missingRaster();
            auto copy = *operation;
            copy.operation = static_cast<TerrainStampOperation>(operationType);
            return self->addModifierStamp(ruleId, *stamp, copy, *localMask, *globalMask);
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    spawnPlan.addFunc("setEnabled", [vm = table.getHandle()](TerrainSpawnPlan* self,
                                                                const std::string& ruleId, bool enabled) {
        return eve::script::projectResult(vm, self->setEnabled(ruleId, enabled));
    });
    spawnPlan.addFunc("getRuleCount", [](const TerrainSpawnPlan* self) { return self->getRuleCount(); });
    spawnPlan.addFunc("snapshotJson", [vm = table.getHandle()](const TerrainSpawnPlan* self) {
        return eve::script::projectResult(vm, self->snapshotJson(),
                                           [](const std::string& value) { return eve::Value(value); });
    });
    spawnPlan.addFunc("restoreJson", [vm = table.getHandle()](TerrainSpawnPlan* self,
                                                                 const std::string& json) {
        return eve::script::projectResult(vm, self->restoreJson(json));
    });

    auto biomePreset = table.addClass("TerrainBiomePreset", ssq::Class::Ctor<TerrainBiomePreset()>());
    biomePreset.addFunc("addSpawner", [vm = table.getHandle()](TerrainBiomePreset* self,
                                                                  const std::string& entryId,
                                                                  const TerrainSpawnPlan* plan,
                                                                  bool activeInBiome, bool activeInStamper,
                                                                  bool autoAssignResources) {
        auto result = plan ? self->addSpawner(entryId, *plan, activeInBiome, activeInStamper,
                                               autoAssignResources)
                           : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    biomePreset.addFunc("setActiveInBiome", [vm = table.getHandle()](TerrainBiomePreset* self,
                                                                        const std::string& entryId, bool active) {
        return eve::script::projectResult(vm, self->setActiveInBiome(entryId, active));
    });
    biomePreset.addFunc("setActiveInStamper", [vm = table.getHandle()](TerrainBiomePreset* self,
                                                                          const std::string& entryId, bool active) {
        return eve::script::projectResult(vm, self->setActiveInStamper(entryId, active));
    });
    biomePreset.addFunc("getSpawnerCount", [](const TerrainBiomePreset* self) { return self->getSpawnerCount(); });
    biomePreset.addFunc("getAutoAssignResources", [vm = table.getHandle()](const TerrainBiomePreset* self,
                                                                              const std::string& entryId) {
        return eve::script::projectResult(vm, self->getAutoAssignResources(entryId),
                                           [](bool value) { return eve::Value(value); });
    });
    biomePreset.addFunc("snapshotJson", [vm = table.getHandle()](const TerrainBiomePreset* self) {
        return eve::script::projectResult(vm, self->snapshotJson(),
                                           [](const std::string& value) { return eve::Value(value); });
    });
    biomePreset.addFunc("restoreJson", [vm = table.getHandle()](TerrainBiomePreset* self,
                                                                   const std::string& json) {
        return eve::script::projectResult(vm, self->restoreJson(json));
    });

    auto world = table.addClass("TerrainWorldWorkspace", ssq::Class::Ctor<TerrainWorldWorkspace()>());
    world.addFunc("create", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                       const TerrainWorldCreationSettings* settings) {
        auto result = settings ? self->create(*settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("stamp", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* stamp,
                                                      const TerrainStampSettings* settings, const Heightmap* local,
                                                      const Heightmap* global) {
        auto result = stamp && settings && local && global ? self->stamp(*stamp, *settings, *local, *global)
                                                           : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("paintSplat", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* paint,
                                                           int layer, const TerrainStampSettings* operation) {
        auto result = paint && operation ? self->paintSplat(*paint, layer, *operation) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("spawn", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                       const TerrainSpawnPlan* plan) {
        auto result = plan ? self->spawn(*plan) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("spawnBiome", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                             const TerrainBiomePreset* preset) {
        auto result = preset ? self->spawnBiome(*preset) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("spawnStamper", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                               const TerrainBiomePreset* preset) {
        auto result = preset ? self->spawnStamper(*preset) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("beginSpawn", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                             const TerrainSpawnPlan* plan) {
        auto result = plan ? self->beginSpawn(*plan)
                           : Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
                                 DiagnosticCode::InvalidArgument, "terrain.world.beginSpawn: plan required"));
        return eve::script::projectResult(vm, std::move(result),
                                           [](TerrainWorldRunStatus status) { return eve::Value(int(status)); });
    });
    world.addFunc("beginSpawnBiome", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                                  const TerrainBiomePreset* preset) {
        auto result = preset ? self->beginSpawnBiome(*preset)
                             : Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
                                   DiagnosticCode::InvalidArgument,
                                   "terrain.world.beginSpawnBiome: preset required"));
        return eve::script::projectResult(vm, std::move(result),
                                           [](TerrainWorldRunStatus status) { return eve::Value(int(status)); });
    });
    world.addFunc("beginSpawnStamper", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                                    const TerrainBiomePreset* preset) {
        auto result = preset ? self->beginSpawnStamper(*preset)
                             : Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
                                   DiagnosticCode::InvalidArgument,
                                   "terrain.world.beginSpawnStamper: preset required"));
        return eve::script::projectResult(vm, std::move(result),
                                           [](TerrainWorldRunStatus status) { return eve::Value(int(status)); });
    });
    world.addFunc("stepSpawn", [vm = table.getHandle()](TerrainWorldWorkspace* self, int maxRules) {
        return eve::script::projectResult(vm, self->stepSpawn(maxRules),
                                           [](TerrainWorldRunStatus status) { return eve::Value(int(status)); });
    });
    world.addFunc("cancelSpawn", [vm = table.getHandle()](TerrainWorldWorkspace* self) {
        return eve::script::projectResult(vm, self->cancelSpawn(),
                                           [](TerrainWorldRunStatus status) { return eve::Value(int(status)); });
    });
    world.addFunc("getSpawnStatus", [](const TerrainWorldWorkspace* self) { return int(self->getSpawnStatus()); });
    world.addFunc("getSpawnCompletedRules",
                  [](const TerrainWorldWorkspace* self) { return self->getSpawnCompletedRules(); });
    world.addFunc("applyDetail", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* fitness,
                                                            const TerrainDetailSettings* settings,
                                                            const TerrainStampSettings* operation, int mode,
                                                            int seed) {
        auto result = fitness && settings && operation
                          ? self->applyDetail(*fitness, *settings, *operation,
                                              static_cast<TerrainDetailMode>(mode), seed)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("applyTrees", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* fitness,
                                                           const TerrainTreePlacementSettings* settings,
                                                           const TerrainStampSettings* operation, int mode) {
        auto result = fitness && settings && operation
                          ? self->applyTrees(*fitness, *settings, *operation,
                                             static_cast<TerrainTreeOperationMode>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("applyObjects", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                             const Heightmap* fitness,
                                                             const TerrainObjectPlacementSettings* settings,
                                                             const TerrainStampSettings* operation, int mode) {
        auto result = fitness && settings && operation
                          ? self->applyObjects(*fitness, *settings, *operation,
                                               static_cast<TerrainObjectOperationMode>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("applyProbes", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                            const Heightmap* fitness,
                                                            const TerrainProbePlacementSettings* settings,
                                                            const TerrainStampSettings* operation, int mode,
                                                            int type) {
        auto result = [&]() -> Result<int> {
            if (!fitness || !settings || !operation) return missingRaster();
            auto copy = *settings;
            copy.type = static_cast<TerrainProbeType>(type);
            return self->applyProbes(*fitness, copy, *operation,
                                     static_cast<TerrainProbeOperationMode>(mode));
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    auto worldClear = table.addClass("TerrainWorldClearSettings", ssq::Class::Ctor<TerrainWorldClearSettings()>());
    worldClear.addVar("details", &TerrainWorldClearSettings::details);
    worldClear.addVar("trees", &TerrainWorldClearSettings::trees);
    worldClear.addVar("objects", &TerrainWorldClearSettings::objects);
    worldClear.addVar("probes", &TerrainWorldClearSettings::probes);
    worldClear.addVar("sourceNamespace", &TerrainWorldClearSettings::sourceNamespace);
    world.addFunc("flatten", [vm = table.getHandle()](TerrainWorldWorkspace* self) {
        return eve::script::projectResult(vm, self->flatten(), [](int n) { return eve::Value(n); });
    });
    world.addFunc("setHeightWorldUnits",
                  [vm = table.getHandle()](TerrainWorldWorkspace* self, float heightWorldUnits,
                                           float worldHeightSpan) {
        return eve::script::projectResult(vm, self->setHeightWorldUnits(heightWorldUnits, worldHeightSpan),
                                          [](int n) { return eve::Value(n); });
    });
    world.addFunc("clearSpawns", [vm = table.getHandle()](TerrainWorldWorkspace* self,
                                                             const TerrainWorldClearSettings* settings) {
        auto result = settings ? self->clearSpawns(*settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("copyHeightmap", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index,
                                                              Heightmap* output) {
        auto result = output ? self->copyHeightmap(index, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("copySplatmap", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index,
                                                             TerrainSplatmap* output) {
        auto result = output ? self->copySplatmap(index, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("copyDetail", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index,
                                                           TerrainDetailLayer* output) {
        auto result = output ? self->copyDetail(index, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("copyTrees", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index,
                                                          PointSet* output) {
        auto result = output ? self->copyTrees(index, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("copyObjects", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index,
                                                            PointSet* output) {
        auto result = output ? self->copyObjects(index, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("copyProbes", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index,
                                                           PointSet* output) {
        auto result = output ? self->copyProbes(index, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("getTileName", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index) {
        return eve::script::projectResult(vm, self->getTileName(index),
                                           [](const std::string& value) { return eve::Value(value); });
    });
    world.addFunc("getTileOriginX", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index) {
        return eve::script::projectResult(vm, self->getTileOriginX(index),
                                           [](double value) { return eve::Value(value); });
    });
    world.addFunc("getTileOriginZ", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index) {
        return eve::script::projectResult(vm, self->getTileOriginZ(index),
                                           [](double value) { return eve::Value(value); });
    });
    world.addFunc("undo", [vm = table.getHandle()](TerrainWorldWorkspace* self) {
        return eve::script::projectResult(vm, self->undo(), [](int n) { return eve::Value(n); });
    });
    world.addFunc("redo", [vm = table.getHandle()](TerrainWorldWorkspace* self) {
        return eve::script::projectResult(vm, self->redo(), [](int n) { return eve::Value(n); });
    });
    world.addFunc("getTileCount", [](const TerrainWorldWorkspace* self) { return self->getTileCount(); });
    world.addFunc("getLastChangedSamples",
                  [](const TerrainWorldWorkspace* self) { return self->getLastChangedSamples(); });
    world.addFunc("getLastAffectedTiles",
                  [](const TerrainWorldWorkspace* self) { return self->getLastAffectedTiles(); });
    world.addFunc("getOperationCount", [](const TerrainWorldWorkspace* self) { return self->getOperationCount(); });
    world.addFunc("getAppliedCount", [](const TerrainWorldWorkspace* self) { return self->getAppliedCount(); });

    auto tree = table.addClass("TerrainTreePlacementSettings", ssq::Class::Ctor<TerrainTreePlacementSettings()>());
    tree.addVar("originX", &TerrainTreePlacementSettings::originX);
    tree.addVar("originZ", &TerrainTreePlacementSettings::originZ);
    tree.addVar("width", &TerrainTreePlacementSettings::width);
    tree.addVar("depth", &TerrainTreePlacementSettings::depth);
    tree.addVar("heightScale", &TerrainTreePlacementSettings::heightScale);
    tree.addVar("spacing", &TerrainTreePlacementSettings::spacing);
    tree.addVar("spawnDensity", &TerrainTreePlacementSettings::spawnDensity);
    tree.addVar("jitterPercent", &TerrainTreePlacementSettings::jitterPercent);
    tree.addVar("failureRate", &TerrainTreePlacementSettings::failureRate);
    tree.addVar("minimumFitness", &TerrainTreePlacementSettings::minimumFitness);
    tree.addVar("snapToTerrain", &TerrainTreePlacementSettings::snapToTerrain);
    tree.addVar("seaLevel", &TerrainTreePlacementSettings::seaLevel);
    tree.addVar("customOffset", &TerrainTreePlacementSettings::customOffset);
    tree.addVar("minimumYOffset", &TerrainTreePlacementSettings::minimumYOffset);
    tree.addVar("maximumYOffset", &TerrainTreePlacementSettings::maximumYOffset);
    tree.addVar("minimumWidth", &TerrainTreePlacementSettings::minimumWidth);
    tree.addVar("maximumWidth", &TerrainTreePlacementSettings::maximumWidth);
    tree.addVar("minimumHeight", &TerrainTreePlacementSettings::minimumHeight);
    tree.addVar("maximumHeight", &TerrainTreePlacementSettings::maximumHeight);
    tree.addVar("widthRandomPercentage", &TerrainTreePlacementSettings::widthRandomPercentage);
    tree.addVar("heightRandomPercentage", &TerrainTreePlacementSettings::heightRandomPercentage);
    tree.addVar("healthyR", &TerrainTreePlacementSettings::healthyR);
    tree.addVar("healthyG", &TerrainTreePlacementSettings::healthyG);
    tree.addVar("healthyB", &TerrainTreePlacementSettings::healthyB);
    tree.addVar("healthyA", &TerrainTreePlacementSettings::healthyA);
    tree.addVar("dryR", &TerrainTreePlacementSettings::dryR);
    tree.addVar("dryG", &TerrainTreePlacementSettings::dryG);
    tree.addVar("dryB", &TerrainTreePlacementSettings::dryB);
    tree.addVar("dryA", &TerrainTreePlacementSettings::dryA);
    tree.addVar("bendFactor", &TerrainTreePlacementSettings::bendFactor);
    tree.addVar("boundsRadius", &TerrainTreePlacementSettings::boundsRadius);
    tree.addVar("seed", &TerrainTreePlacementSettings::seed);
    tree.addVar("namespaceId", &TerrainTreePlacementSettings::namespaceId);
    tree.addVar("maxPoints", &TerrainTreePlacementSettings::maxPoints);
    tree.addVar("asset", &TerrainTreePlacementSettings::asset);
    tree.addFunc("setScaleMode", [](TerrainTreePlacementSettings* s, int mode) {
        s->scaleMode = static_cast<TerrainTreeScaleMode>(mode);
    });
    tree.addFunc("getScaleMode", [](const TerrainTreePlacementSettings* s) { return int(s->scaleMode); });
    tree.addFunc("setYOffsetMode", [](TerrainTreePlacementSettings* s, int mode) {
        s->yOffsetMode = static_cast<TerrainTreeYOffsetMode>(mode);
    });
    tree.addFunc("getYOffsetMode", [](const TerrainTreePlacementSettings* s) { return int(s->yOffsetMode); });
    table.addFunc("exportTerrainTreePoints",
                  [vm = table.getHandle()](PointSet* output, const Heightmap* fitness, const Heightmap* heights,
                                           const TerrainTreePlacementSettings* settings) {
                      auto result = output && fitness && heights && settings
                                        ? exportTerrainTreePoints(*output, *fitness, *heights, *settings)
                                        : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    table.addFunc("removeTerrainTreePoints",
                  [vm = table.getHandle()](PointSet* output, const PointSet* input, const Heightmap* fitness,
                                           const TerrainTreePlacementSettings* settings) {
                      auto result = output && input && fitness && settings
                                        ? removeTerrainTreePoints(*output, *input, *fitness, *settings)
                                        : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });

    auto rescale = table.addClass("TerrainTreeRescaleSettings", ssq::Class::Ctor<TerrainTreeRescaleSettings()>());
    rescale.addVar("previousMinimumWidth", &TerrainTreeRescaleSettings::previousMinimumWidth);
    rescale.addVar("previousMaximumWidth", &TerrainTreeRescaleSettings::previousMaximumWidth);
    rescale.addVar("previousMinimumHeight", &TerrainTreeRescaleSettings::previousMinimumHeight);
    rescale.addVar("previousMaximumHeight", &TerrainTreeRescaleSettings::previousMaximumHeight);
    rescale.addVar("minimumWidth", &TerrainTreeRescaleSettings::minimumWidth);
    rescale.addVar("maximumWidth", &TerrainTreeRescaleSettings::maximumWidth);
    rescale.addVar("minimumHeight", &TerrainTreeRescaleSettings::minimumHeight);
    rescale.addVar("maximumHeight", &TerrainTreeRescaleSettings::maximumHeight);
    rescale.addVar("bendFactor", &TerrainTreeRescaleSettings::bendFactor);
    rescale.addVar("boundsRadius", &TerrainTreeRescaleSettings::boundsRadius);
    rescale.addVar("asset", &TerrainTreeRescaleSettings::asset);
    rescale.addFunc("setScaleMode", [](TerrainTreeRescaleSettings* s, int mode) {
        s->scaleMode = static_cast<TerrainTreeScaleMode>(mode);
    });
    rescale.addFunc("getScaleMode", [](const TerrainTreeRescaleSettings* s) { return int(s->scaleMode); });
    table.addFunc("rescaleTerrainTreePoints",
                  [vm = table.getHandle()](PointSet* output, const PointSet* input,
                                           const TerrainTreeRescaleSettings* settings) {
                      auto result = output && input && settings ? rescaleTerrainTreePoints(*output, *input, *settings)
                                                               : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });

    auto placement =
        table.addClass("TerrainDetailPlacementSettings", ssq::Class::Ctor<TerrainDetailPlacementSettings()>());
    placement.addVar("originX", &TerrainDetailPlacementSettings::originX);
    placement.addVar("originZ", &TerrainDetailPlacementSettings::originZ);
    placement.addVar("width", &TerrainDetailPlacementSettings::width);
    placement.addVar("depth", &TerrainDetailPlacementSettings::depth);
    placement.addVar("heightScale", &TerrainDetailPlacementSettings::heightScale);
    placement.addVar("minimumScale", &TerrainDetailPlacementSettings::minimumScale);
    placement.addVar("maximumScale", &TerrainDetailPlacementSettings::maximumScale);
    placement.addVar("minimumWidth", &TerrainDetailPlacementSettings::minimumWidth);
    placement.addVar("maximumWidth", &TerrainDetailPlacementSettings::maximumWidth);
    placement.addVar("minimumHeight", &TerrainDetailPlacementSettings::minimumHeight);
    placement.addVar("maximumHeight", &TerrainDetailPlacementSettings::maximumHeight);
    placement.addVar("healthyR", &TerrainDetailPlacementSettings::healthyR);
    placement.addVar("healthyG", &TerrainDetailPlacementSettings::healthyG);
    placement.addVar("healthyB", &TerrainDetailPlacementSettings::healthyB);
    placement.addVar("healthyA", &TerrainDetailPlacementSettings::healthyA);
    placement.addVar("dryR", &TerrainDetailPlacementSettings::dryR);
    placement.addVar("dryG", &TerrainDetailPlacementSettings::dryG);
    placement.addVar("dryB", &TerrainDetailPlacementSettings::dryB);
    placement.addVar("dryA", &TerrainDetailPlacementSettings::dryA);
    placement.addVar("noiseSpread", &TerrainDetailPlacementSettings::noiseSpread);
    placement.addVar("noiseSeed", &TerrainDetailPlacementSettings::noiseSeed);
    placement.addVar("seed", &TerrainDetailPlacementSettings::seed);
    placement.addVar("namespaceId", &TerrainDetailPlacementSettings::namespaceId);
    placement.addVar("densityNamespaceId", &TerrainDetailPlacementSettings::densityNamespaceId);
    placement.addVar("maxPoints", &TerrainDetailPlacementSettings::maxPoints);
    placement.addVar("asset", &TerrainDetailPlacementSettings::asset);
    table.addFunc("exportTerrainDetailPoints",
                  [vm = table.getHandle()](PointSet* output, const TerrainDetailLayer* layer, const Heightmap* heights,
                                           const TerrainDetailPlacementSettings* settings) {
                      auto result = output && layer && heights && settings
                                        ? exportTerrainDetailPoints(*output, *layer, *heights, *settings)
                                        : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });

    auto detailSettings = table.addClass("TerrainDetailSettings", ssq::Class::Ctor<TerrainDetailSettings()>());
    detailSettings.addVar("minimumFitness", &TerrainDetailSettings::minimumFitness);
    detailSettings.addVar("fadeStart", &TerrainDetailSettings::fadeStart);
    detailSettings.addVar("density", &TerrainDetailSettings::density);
    detailSettings.addVar("namespaceId", &TerrainDetailSettings::namespaceId);
    auto detail = table.addClass("TerrainDetailLayer", ssq::Class::Ctor<TerrainDetailLayer()>());
    detail.addFunc("getWidth", [](const TerrainDetailLayer* s) { return s->getWidth(); });
    detail.addFunc("getHeight", [](const TerrainDetailLayer* s) { return s->getHeight(); });
    detail.addFunc("reset", [vm = table.getHandle()](TerrainDetailLayer* s, int width, int height, int count) {
        return eve::script::projectResult(vm, s->reset(width, height, count), [](int n) { return eve::Value(n); });
    });
    detail.addFunc("sample", [vm = table.getHandle()](const TerrainDetailLayer* s, int x, int z) {
        return eve::script::projectResult(vm, s->sample(x, z), [](int n) { return eve::Value(n); });
    });
    detail.addFunc("apply", [vm = table.getHandle()](TerrainDetailLayer* s, const Heightmap* fitness,
                                                     const TerrainDetailSettings* settings, int mode, int seed) {
        auto result = [&]() -> Result<int> {
            if (!fitness || !settings) return missingRaster();
            auto copy = *settings;
            copy.mode = static_cast<TerrainDetailMode>(mode);
            return s->apply(*fitness, copy, seed);
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });

    auto session = table.addClass("TerrainGenerationSession", ssq::Class::Ctor<TerrainGenerationSession()>());
    session.addFunc("smooth", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* mask,
                                                       const TerrainSmoothSettings* settings) {
        auto result = mask && settings ? s->smooth(*mask, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc("ridges", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* mask,
                                                       const TerrainRidgeSettings* settings) {
        auto result = mask && settings ? s->ridges(*mask, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc("terrace", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* mask,
                                                        const TerrainTerraceSettings* settings) {
        auto result = mask && settings ? s->terrace(*mask, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc("contrast", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* mask,
                                                         float strength, float featureSize) {
        auto result = mask ? s->contrast(*mask, strength, featureSize) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc("power", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* mask, float power) {
        auto result = mask ? s->power(*mask, power) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc("heightCurve", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* mask,
                                                            const Heightmap* curve, float minimum, float maximum) {
        auto result = mask && curve ? s->heightCurve(*mask, *curve, minimum, maximum) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc(
        "heightMix", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* local,
                                              const Heightmap* global, const TerrainHeightMixSettings* settings) {
            auto result = local && global && settings ? s->heightMix(*local, *global, *settings) : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    session.addFunc("resetSimulation", [vm = table.getHandle()](TerrainGenerationSession* s, float depth) {
        return eve::script::projectResult(vm, s->resetSimulation(depth), [](int n) { return eve::Value(n); });
    });
    session.addFunc("thermal",
                    [vm = table.getHandle()](TerrainGenerationSession* s, const TerrainThermalSettings* settings) {
                        auto result = settings ? s->thermal(*settings) : missingRaster();
                        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                    });
    session.addFunc(
        "hydraulic", [vm = table.getHandle()](TerrainGenerationSession* s, const TerrainWaterSettings* water,
                                              const TerrainSedimentSettings* sediment,
                                              const TerrainThermalSettings* thermal, int iterations) {
            auto result =
                water && sediment && thermal ? s->hydraulic(*water, *sediment, *thermal, iterations) : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    session.addFunc("copySediment", [vm = table.getHandle()](const TerrainGenerationSession* s, Heightmap* target) {
        auto result = target ? s->copySediment(*target) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc(
        "exportWater", [vm = table.getHandle()](const TerrainGenerationSession* s, Heightmap* target, int channel) {
            auto result = target ? s->exportWater(*target, static_cast<TerrainWaterChannel>(channel)) : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    session.addFunc("getOperationCount", [](const TerrainGenerationSession* s) { return s->getOperationCount(); });
    session.addFunc("getAppliedCount", [](const TerrainGenerationSession* s) { return s->getAppliedCount(); });
    session.addFunc("getReplayStatus", [](const TerrainGenerationSession* s) { return int(s->getReplayStatus()); });
    session.addFunc("getReplayCompletedOperations",
                    [](const TerrainGenerationSession* s) { return s->getReplayCompletedOperations(); });
    session.addFunc("getAccess", [](const TerrainGenerationSession* s) { return int(s->getAccess()); });
    session.addFunc("snapshotJson", [vm = table.getHandle()](const TerrainGenerationSession* s) {
        return eve::script::projectResult(vm, s->snapshotJson(),
                                           [](const std::string& value) { return eve::Value(value); });
    });
    session.addFunc("restoreJson", [vm = table.getHandle()](TerrainGenerationSession* s,
                                                              const std::string& json) {
        return eve::script::projectResult(vm, s->restoreJson(json));
    });
    session.addFunc("setAccess", [vm = table.getHandle()](TerrainGenerationSession* s, int access) {
        return eve::script::projectResult(vm, s->setAccess(static_cast<TerrainSessionAccess>(access)));
    });
    session.addFunc("operationEnabled", [vm = table.getHandle()](const TerrainGenerationSession* s, int index) {
        return eve::script::projectResult(vm, s->operationEnabled(index), [](bool value) { return eve::Value(value); });
    });
    session.addFunc("reset", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* baseline) {
        auto result = baseline ? s->reset(*baseline) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc("copyTerrain", [vm = table.getHandle()](const TerrainGenerationSession* s, Heightmap* target) {
        auto result = target ? s->copyTerrain(*target) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    session.addFunc("setOperationEnabled",
                    [vm = table.getHandle()](TerrainGenerationSession* s, int index, bool enabled) {
                        return eve::script::projectResult(vm, s->setOperationEnabled(index, enabled),
                                                          [](int n) { return eve::Value(n); });
                    });
    session.addFunc("undo", [vm = table.getHandle()](TerrainGenerationSession* s) {
        return eve::script::projectResult(vm, s->undo(), [](int n) { return eve::Value(n); });
    });
    session.addFunc("redo", [vm = table.getHandle()](TerrainGenerationSession* s) {
        return eve::script::projectResult(vm, s->redo(), [](int n) { return eve::Value(n); });
    });
    session.addFunc("replay", [vm = table.getHandle()](TerrainGenerationSession* s) {
        return eve::script::projectResult(vm, s->replay(), [](int n) { return eve::Value(n); });
    });
    session.addFunc("beginReplay", [vm = table.getHandle()](TerrainGenerationSession* s) {
        return eve::script::projectResult(vm, s->beginReplay(),
                                           [](TerrainSessionRunStatus status) { return eve::Value(int(status)); });
    });
    session.addFunc("stepReplay", [vm = table.getHandle()](TerrainGenerationSession* s, int maxOperations) {
        return eve::script::projectResult(vm, s->stepReplay(maxOperations),
                                           [](TerrainSessionRunStatus status) { return eve::Value(int(status)); });
    });
    session.addFunc("cancelReplay", [vm = table.getHandle()](TerrainGenerationSession* s) {
        return eve::script::projectResult(vm, s->cancelReplay(),
                                           [](TerrainSessionRunStatus status) { return eve::Value(int(status)); });
    });
    session.addFunc("stamp", [vm = table.getHandle()](TerrainGenerationSession* s, const Heightmap* stamp,
                                                      const TerrainStampSettings* settings, int operation,
                                                      const Heightmap* local, const Heightmap* global) {
        auto result = [&]() -> Result<int> {
            if (!stamp || !settings || !local || !global) return missingRaster();
            auto copy      = *settings;
            copy.operation = static_cast<TerrainStampOperation>(operation);
            return s->stamp(*stamp, copy, *local, *global);
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });

    exposeTerrainStampSettings(table);

    auto stitchSettings =
        table.addClass("TerrainHeightStitchSettings", ssq::Class::Ctor<TerrainHeightStitchSettings()>());
    stitchSettings.addVar("extraSeamSize", &TerrainHeightStitchSettings::extraSeamSize);
    stitchSettings.addVar("maxDifference", &TerrainHeightStitchSettings::maxDifference);
    auto multi = table.addClass("TerrainMultiTileWorkspace", ssq::Class::Ctor<TerrainMultiTileWorkspace()>());
    multi.addFunc("addTile", [vm = table.getHandle()](TerrainMultiTileWorkspace* workspace,
                                                       const std::string& name, const Heightmap* heightmap,
                                                       float originX, float originZ, float width, float depth,
                                                       bool worldMap) {
        auto result = heightmap ? workspace->addTile(name, *heightmap, originX, originZ, width, depth, worldMap)
                                : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    detail.addFunc("sampleResource", [vm = table.getHandle()](const TerrainDetailLayer* s,
                                                                 std::uint64_t namespaceId, int x, int z) {
        return eve::script::projectResult(vm, s->sampleResource(namespaceId, x, z),
                                           [](int n) { return eve::Value(n); });
    });
    detail.addFunc("clearResource", [vm = table.getHandle()](TerrainDetailLayer* s,
                                                                std::uint64_t namespaceId) {
        return eve::script::projectResult(vm, s->clearResource(namespaceId),
                                           [](int n) { return eve::Value(n); });
    });
    multi.addFunc("stamp", [vm = table.getHandle()](TerrainMultiTileWorkspace* workspace,
                                                     const Heightmap* stampRaster,
                                                     const TerrainStampSettings* stampSettings, int operation,
                                                     const Heightmap* localMask, const Heightmap* globalMask,
                                                     bool worldMap) {
        auto result = [&]() -> Result<int> {
            if (!stampRaster || !stampSettings || !localMask || !globalMask) return missingRaster();
            auto copy = *stampSettings;
            copy.operation = static_cast<TerrainStampOperation>(operation);
            return workspace->stamp(*stampRaster, copy, *localMask, *globalMask, worldMap);
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multi.addFunc("copyTile", [vm = table.getHandle()](const TerrainMultiTileWorkspace* workspace,
                                                        const std::string& name, Heightmap* output) {
        auto result = output ? workspace->copyTile(name, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multi.addFunc("stitch", [vm = table.getHandle()](TerrainMultiTileWorkspace* workspace,
                                                       const std::string& terrainA,
                                                       const std::string& terrainB,
                                                       const TerrainHeightStitchSettings* settings) {
        auto result = settings ? workspace->stitch(terrainA, terrainB, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multi.addFunc("undo", [vm = table.getHandle()](TerrainMultiTileWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->undo(), [](int n) { return eve::Value(n); });
    });
    multi.addFunc("redo", [vm = table.getHandle()](TerrainMultiTileWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->redo(), [](int n) { return eve::Value(n); });
    });
    multi.addFunc("getTileCount", [](const TerrainMultiTileWorkspace* workspace) { return workspace->getTileCount(); });
    multi.addFunc("getLastAffectedTiles", [](const TerrainMultiTileWorkspace* workspace) {
        return workspace->getLastAffectedTiles();
    });
    multi.addFunc("getLastChangedSamples", [](const TerrainMultiTileWorkspace* workspace) {
        return workspace->getLastChangedSamples();
    });
    multi.addFunc("getLastMappingCount", [](const TerrainMultiTileWorkspace* workspace) {
        return workspace->getLastMappingCount();
    });
    multi.addFunc("getOperationCount", [](const TerrainMultiTileWorkspace* workspace) {
        return workspace->getOperationCount();
    });
    multi.addFunc("getAppliedCount", [](const TerrainMultiTileWorkspace* workspace) {
        return workspace->getAppliedCount();
    });

    auto multiDetail =
        table.addClass("TerrainMultiDetailWorkspace", ssq::Class::Ctor<TerrainMultiDetailWorkspace()>());
    multiDetail.addFunc("addTile", [vm = table.getHandle()](TerrainMultiDetailWorkspace* workspace,
                                                             const std::string& name,
                                                             const TerrainDetailLayer* layer, float originX,
                                                             float originZ, float width, float depth, bool worldMap) {
        auto result = layer ? workspace->addTile(name, *layer, originX, originZ, width, depth, worldMap)
                            : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiDetail.addFunc(
        "apply", [vm = table.getHandle()](TerrainMultiDetailWorkspace* workspace, const Heightmap* fitness,
                                           const TerrainDetailSettings* settings,
                                           const TerrainStampSettings* operationSettings, int mode, int seed,
                                           bool worldMap) {
            auto result = [&]() -> Result<int> {
                if (!fitness || !settings || !operationSettings) return missingRaster();
                auto copy = *settings;
                copy.mode = static_cast<TerrainDetailMode>(mode);
                return workspace->apply(*fitness, copy, *operationSettings, seed, worldMap);
            }();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    multiDetail.addFunc("copyTile", [vm = table.getHandle()](const TerrainMultiDetailWorkspace* workspace,
                                                              const std::string& name,
                                                              TerrainDetailLayer* output) {
        auto result = output ? workspace->copyTile(name, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiDetail.addFunc("undo", [vm = table.getHandle()](TerrainMultiDetailWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->undo(), [](int n) { return eve::Value(n); });
    });
    multiDetail.addFunc("redo", [vm = table.getHandle()](TerrainMultiDetailWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->redo(), [](int n) { return eve::Value(n); });
    });
    multiDetail.addFunc("getTileCount",
                        [](const TerrainMultiDetailWorkspace* workspace) { return workspace->getTileCount(); });
    multiDetail.addFunc("getLastChangedSamples", [](const TerrainMultiDetailWorkspace* workspace) {
        return workspace->getLastChangedSamples();
    });
    multiDetail.addFunc("getLastAffectedTiles", [](const TerrainMultiDetailWorkspace* workspace) {
        return workspace->getLastAffectedTiles();
    });
    multiDetail.addFunc("getOperationCount", [](const TerrainMultiDetailWorkspace* workspace) {
        return workspace->getOperationCount();
    });
    multiDetail.addFunc("getAppliedCount", [](const TerrainMultiDetailWorkspace* workspace) {
        return workspace->getAppliedCount();
    });

    auto multiTree = table.addClass("TerrainMultiTreeWorkspace", ssq::Class::Ctor<TerrainMultiTreeWorkspace()>());
    multiTree.addFunc("addTile", [vm = table.getHandle()](TerrainMultiTreeWorkspace* workspace,
                                                           const std::string& name, const PointSet* trees,
                                                           const Heightmap* heights, float originX, float originZ,
                                                           float width, float depth, int resolutionX,
                                                           int resolutionY, bool worldMap) {
        auto result = trees && heights ? workspace->addTile(name, *trees, *heights, originX, originZ, width, depth,
                                                             resolutionX, resolutionY, worldMap)
                                       : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiTree.addFunc("apply", [vm = table.getHandle()](TerrainMultiTreeWorkspace* workspace,
                                                         const Heightmap* fitness,
                                                         const TerrainTreePlacementSettings* settings,
                                                         const TerrainStampSettings* operationSettings, int mode,
                                                         bool worldMap) {
        auto result = fitness && settings && operationSettings
                          ? workspace->apply(*fitness, *settings, *operationSettings,
                                             static_cast<TerrainTreeOperationMode>(mode), worldMap)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiTree.addFunc("copyTile", [vm = table.getHandle()](const TerrainMultiTreeWorkspace* workspace,
                                                            const std::string& name, PointSet* output) {
        auto result = output ? workspace->copyTile(name, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    multiTree.addFunc("undo", [vm = table.getHandle()](TerrainMultiTreeWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->undo(), [](int n) { return eve::Value(n); });
    });
    multiTree.addFunc("redo", [vm = table.getHandle()](TerrainMultiTreeWorkspace* workspace) {
        return eve::script::projectResult(vm, workspace->redo(), [](int n) { return eve::Value(n); });
    });
    multiTree.addFunc("getTileCount",
                      [](const TerrainMultiTreeWorkspace* workspace) { return workspace->getTileCount(); });
    multiTree.addFunc("getLastChangedSamples", [](const TerrainMultiTreeWorkspace* workspace) {
        return workspace->getLastChangedSamples();
    });
    multiTree.addFunc("getLastAffectedTiles", [](const TerrainMultiTreeWorkspace* workspace) {
        return workspace->getLastAffectedTiles();
    });
    multiTree.addFunc("getOperationCount", [](const TerrainMultiTreeWorkspace* workspace) {
        return workspace->getOperationCount();
    });
    multiTree.addFunc("getAppliedCount", [](const TerrainMultiTreeWorkspace* workspace) {
        return workspace->getAppliedCount();
    });

    auto distance = table.addClass("TerrainDistanceMaskSettings", ssq::Class::Ctor<TerrainDistanceMaskSettings()>());
    distance.addVar("offsetX", &TerrainDistanceMaskSettings::offsetX);
    distance.addVar("offsetZ", &TerrainDistanceMaskSettings::offsetZ);
    distance.addVar("scaleX", &TerrainDistanceMaskSettings::scaleX);
    distance.addVar("scaleZ", &TerrainDistanceMaskSettings::scaleZ);
    distance.addVar("rotation", &TerrainDistanceMaskSettings::rotation);
    distance.addVar("roundness", &TerrainDistanceMaskSettings::roundness);
    distance.addVar("tiling", &TerrainDistanceMaskSettings::tiling);

    auto smooth = table.addClass("TerrainSmoothSettings", ssq::Class::Ctor<TerrainSmoothSettings()>());
    smooth.addVar("radius", &TerrainSmoothSettings::radius);
    smooth.addVar("verticality", &TerrainSmoothSettings::verticality);
    smooth.addVar("strength", &TerrainSmoothSettings::strength);
    auto ridge = table.addClass("TerrainRidgeSettings", ssq::Class::Ctor<TerrainRidgeSettings()>());
    ridge.addVar("mixStrength", &TerrainRidgeSettings::mixStrength);
    ridge.addVar("exponent", &TerrainRidgeSettings::exponent);
    ridge.addVar("strength", &TerrainRidgeSettings::strength);
    ridge.addVar("minimum", &TerrainRidgeSettings::minimum);
    ridge.addVar("maximum", &TerrainRidgeSettings::maximum);
    ridge.addVar("passes", &TerrainRidgeSettings::passes);
    auto terrace = table.addClass("TerrainTerraceSettings", ssq::Class::Ctor<TerrainTerraceSettings()>());
    terrace.addVar("count", &TerrainTerraceSettings::count);
    terrace.addVar("bevel", &TerrainTerraceSettings::bevel);
    terrace.addVar("strength", &TerrainTerraceSettings::strength);

    auto mix = table.addClass("TerrainHeightMixSettings", ssq::Class::Ctor<TerrainHeightMixSettings()>());
    mix.addVar("minimum", &TerrainHeightMixSettings::minimum);
    mix.addVar("maximum", &TerrainHeightMixSettings::maximum);
    mix.addVar("midpoint", &TerrainHeightMixSettings::midpoint);
    mix.addVar("strength", &TerrainHeightMixSettings::strength);
    mix.addVar("clipMinimum", &TerrainHeightMixSettings::clipMinimum);
    mix.addVar("clipMaximum", &TerrainHeightMixSettings::clipMaximum);

    auto thermal = table.addClass("TerrainThermalSettings", ssq::Class::Ctor<TerrainThermalSettings()>());
    thermal.addVar("spacingX", &TerrainThermalSettings::spacingX);
    thermal.addVar("spacingZ", &TerrainThermalSettings::spacingZ);
    thermal.addVar("heightScale", &TerrainThermalSettings::heightScale);
    thermal.addVar("reposeSlope", &TerrainThermalSettings::reposeSlope);
    thermal.addVar("dt", &TerrainThermalSettings::dt);
    thermal.addVar("iterations", &TerrainThermalSettings::iterations);

    auto waterSettings = table.addClass("TerrainWaterSettings", ssq::Class::Ctor<TerrainWaterSettings()>());
    waterSettings.addVar("spacingX", &TerrainWaterSettings::spacingX);
    waterSettings.addVar("spacingZ", &TerrainWaterSettings::spacingZ);
    waterSettings.addVar("heightScale", &TerrainWaterSettings::heightScale);
    waterSettings.addVar("waterScale", &TerrainWaterSettings::waterScale);
    waterSettings.addVar("dt", &TerrainWaterSettings::dt);
    waterSettings.addVar("precipitation", &TerrainWaterSettings::precipitation);
    waterSettings.addVar("evaporation", &TerrainWaterSettings::evaporation);
    waterSettings.addVar("flowAcceleration", &TerrainWaterSettings::flowAcceleration);

    auto waterFlowSettings = table.addClass("TerrainWaterFlowMapSettings", ssq::Class::Ctor<TerrainWaterFlowMapSettings()>());
    waterFlowSettings.addVar("dropletVolume", &TerrainWaterFlowMapSettings::dropletVolume);
    waterFlowSettings.addVar("absorptionRate", &TerrainWaterFlowMapSettings::absorptionRate);
    waterFlowSettings.addVar("smoothIterations", &TerrainWaterFlowMapSettings::smoothIterations);
    table.addFunc("generateTerrainWaterFlowMap", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                           const TerrainWaterFlowMapSettings* settings) {
        auto result = target && source && settings
                          ? generateTerrainWaterFlowMap(*target, *source, *settings)
                          : Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                   "terrain.waterFlow: target, source and settings required"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("generateTerrainVelocityFlowMap", [vm = table.getHandle()](Heightmap* target,
                                                                              const Heightmap* source,
                                                                              int iterations) {
        auto result = target && source ? generateTerrainVelocityFlowMap(*target, *source, iterations) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("generateTerrainHeightmapCurvature", [vm = table.getHandle()](Heightmap* target,
                                                                                 const Heightmap* source, int mode) {
        auto result = target && source
                          ? generateTerrainHeightmapCurvature(*target, *source,
                                                              static_cast<TerrainHeightmapCurvature>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("generateTerrainHeightmapAspect", [vm = table.getHandle()](Heightmap* target,
                                                                              const Heightmap* source, int mode) {
        auto result = target && source
                          ? generateTerrainHeightmapAspect(*target, *source,
                                                           static_cast<TerrainHeightmapAspect>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("filterTerrainHeightmapNeighborhood", [vm = table.getHandle()](Heightmap* target,
                                                                                  const Heightmap* source, int radius,
                                                                                  int mode) {
        auto result = target && source
                          ? filterTerrainHeightmapNeighborhood(*target, *source, radius,
                                                               static_cast<TerrainHeightmapNeighborhood>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("smoothTerrainHeightmap", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                      int iterations) {
        auto result = target && source ? smoothTerrainHeightmap(*target, *source, iterations) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("smoothTerrainHeightmapRadius", [vm = table.getHandle()](Heightmap* target,
                                                                            const Heightmap* source, int radius) {
        auto result = target && source ? smoothTerrainHeightmapRadius(*target, *source, radius) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("convolveTerrainHeightmap", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                        const Heightmap* kernel) {
        auto result = target && source && kernel ? convolveTerrainHeightmap(*target, *source, *kernel) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("generateTerrainHeightmapSlope", [vm = table.getHandle()](Heightmap* target,
                                                                             const Heightmap* source) {
        auto result = target && source ? generateTerrainHeightmapSlope(*target, *source) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("quantizeTerrainHeightmap", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                        float divisor) {
        auto result = target && source ? quantizeTerrainHeightmap(*target, *source, divisor) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("applyTerrainHeightmapScalarArithmetic",
                  [vm = table.getHandle()](Heightmap* target, const Heightmap* source, float operand, int operation,
                                           bool clampResult, float minValue, float maxValue) {
                      auto result = target && source
                                        ? applyTerrainHeightmapScalarArithmetic(
                                              *target, *source, operand,
                                              static_cast<TerrainHeightmapArithmetic>(operation), clampResult,
                                              minValue, maxValue)
                                        : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    table.addFunc("applyTerrainHeightmapRasterArithmetic",
                  [vm = table.getHandle()](Heightmap* target, const Heightmap* source, const Heightmap* operand,
                                           int operation, bool clampResult, float minValue, float maxValue) {
                      auto result = target && source && operand
                                        ? applyTerrainHeightmapRasterArithmetic(
                                              *target, *source, *operand,
                                              static_cast<TerrainHeightmapArithmetic>(operation), clampResult,
                                              minValue, maxValue)
                                        : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    table.addFunc("lerpTerrainHeightmap", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                    const Heightmap* values, const Heightmap* mask) {
        auto result = target && source && values && mask ? lerpTerrainHeightmap(*target, *source, *values, *mask)
                                                         : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("transformTerrainHeightmap", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                         int transform, float parameter) {
        auto result = target && source
                          ? transformTerrainHeightmap(*target, *source, static_cast<TerrainHeightmapTransform>(transform),
                                                      parameter)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("copyTerrainHeightmap", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                    int mode) {
        auto result = target && source
                          ? copyTerrainHeightmap(*target, *source, static_cast<TerrainHeightmapCopy>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("copyTerrainHeightmapClamped", [vm = table.getHandle()](Heightmap* target,
                                                                           const Heightmap* source, float minValue,
                                                                           float maxValue) {
        auto result = target && source ? copyTerrainHeightmapClamped(*target, *source, minValue, maxValue)
                                       : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("flipTerrainHeightmap", [vm = table.getHandle()](Heightmap* target, const Heightmap* source) {
        auto result = target && source ? flipTerrainHeightmap(*target, *source) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("measureTerrainHeightmap", [vm = table.getHandle()](const Heightmap* source, int measure) {
        auto result = source
                          ? measureTerrainHeightmap(*source, static_cast<TerrainHeightmapMeasure>(measure))
                          : Result<double>::failure(missingRaster().status());
        return eve::script::projectResult(vm, std::move(result), [](double value) { return eve::Value(value); });
    });
    table.addFunc("quantizeTerrainHeightmapTerraces",
                  [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                           const Heightmap* startHeights, const Heightmap* curves) {
                      auto result = target && source && startHeights && curves
                                        ? quantizeTerrainHeightmapTerraces(*target, *source, *startHeights, *curves)
                                        : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    table.addFunc("measureTerrainHeightmapSlope",
                  [vm = table.getHandle()](const Heightmap* source, float x, float y, int mode) {
                      auto result = source
                                        ? measureTerrainHeightmapSlope(
                                              *source, x, y, static_cast<TerrainHeightmapSlopeQuery>(mode))
                                        : Result<double>::failure(missingRaster().status());
                      return eve::script::projectResult(vm, std::move(result),
                                                        [](double value) { return eve::Value(value); });
                  });
    table.addFunc("fillTerrainHeightmap", [vm = table.getHandle()](Heightmap* target, float value) {
        auto result = target ? fillTerrainHeightmap(*target, value) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("setTerrainHeightmapSafe",
                  [vm = table.getHandle()](Heightmap* target, int x, int y, float value) {
                      auto result = target ? setTerrainHeightmapSafe(*target, x, y, value) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    table.addFunc("setTerrainHeightmapRow", [vm = table.getHandle()](Heightmap* target, int rowX,
                                                                      const Heightmap* values) {
        auto result = target && values ? setTerrainHeightmapRow(*target, rowX, *values) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("setTerrainHeightmapColumn", [vm = table.getHandle()](Heightmap* target, int columnZ,
                                                                         const Heightmap* values) {
        auto result = target && values ? setTerrainHeightmapColumn(*target, columnZ, *values) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("resetTerrainHeightmap", [vm = table.getHandle()](Heightmap* target) {
        auto result = target ? resetTerrainHeightmap(*target) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("sampleTerrainHeightmapSafe", [vm = table.getHandle()](const Heightmap* source, int x, int z) {
        auto result = source ? sampleTerrainHeightmapSafe(*source, x, z)
                             : Result<float>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        "terrain: heightmap required"));
        return eve::script::projectResult(vm, std::move(result), [](float value) { return eve::Value(value); });
    });
    table.addFunc("sampleTerrainHeightmapNormalized",
                  [vm = table.getHandle()](const Heightmap* source, float x, float z) {
                      auto result = source ? sampleTerrainHeightmapNormalized(*source, x, z)
                                           : Result<float>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, "terrain: heightmap required"));
                      return eve::script::projectResult(vm, std::move(result),
                                                        [](float value) { return eve::Value(value); });
                  });
    auto terraceRemoval = table.addClass("TerrainTerraceRemovalSettings",
                                         ssq::Class::Ctor<TerrainTerraceRemovalSettings()>());
    terraceRemoval.addVar("perlinScale", &TerrainTerraceRemovalSettings::perlinScale);
    terraceRemoval.addVar("perlinStrength", &TerrainTerraceRemovalSettings::perlinStrength);
    terraceRemoval.addVar("slopeTerraceThreshold", &TerrainTerraceRemovalSettings::slopeTerraceThreshold);
    terraceRemoval.addVar("flatThreshold", &TerrainTerraceRemovalSettings::flatThreshold);
    terraceRemoval.addVar("verticalGradientThreshold", &TerrainTerraceRemovalSettings::verticalGradientThreshold);
    terraceRemoval.addVar("minimumTerraceThreshold", &TerrainTerraceRemovalSettings::minimumTerraceThreshold);
    terraceRemoval.addVar("maximumTerraceThreshold", &TerrainTerraceRemovalSettings::maximumTerraceThreshold);
    terraceRemoval.addVar("excludeRed", &TerrainTerraceRemovalSettings::excludeRed);
    terraceRemoval.addVar("excludeBlack", &TerrainTerraceRemovalSettings::excludeBlack);
    terraceRemoval.addVar("terrainWorkflow", &TerrainTerraceRemovalSettings::terrainWorkflow);
    terraceRemoval.addVar("noiseSeed", &TerrainTerraceRemovalSettings::noiseSeed);
    table.addFunc("analyzeTerrainTerraces", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                       const TerrainTerraceRemovalSettings* settings) {
        auto result = target && source && settings ? analyzeTerrainTerraces(*target, *source, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("removeTerrainTerraces", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                      const Heightmap* classification,
                                                                      const TerrainTerraceRemovalSettings* settings) {
        auto result = target && source && classification && settings
                          ? removeTerrainTerraces(*target, *source, *classification, *settings)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    auto sedimentSettings = table.addClass("TerrainSedimentSettings", ssq::Class::Ctor<TerrainSedimentSettings()>());
    sedimentSettings.addVar("effect", &TerrainSedimentSettings::effect);
    sedimentSettings.addVar("depositRate", &TerrainSedimentSettings::depositRate);
    sedimentSettings.addVar("bankDeposit", &TerrainSedimentSettings::bankDeposit);
    sedimentSettings.addVar("bedDeposit", &TerrainSedimentSettings::bedDeposit);
    auto water = table.addClass("TerrainWaterField", ssq::Class::Ctor<TerrainWaterField()>());
    water.addFunc(
        "exportChannel", [vm = table.getHandle()](const TerrainWaterField* field, Heightmap* target, int channel) {
            auto result = field && target ? field->exportChannel(*target, static_cast<TerrainWaterChannel>(channel))
                                          : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    water.addFunc("advanceHydraulic", [vm = table.getHandle()](
                                          TerrainWaterField* field, Heightmap* heights, Heightmap* sediment,
                                          const TerrainWaterSettings* settings, const TerrainSedimentSettings* reaction,
                                          const TerrainThermalSettings* thermal, int iterations) {
        auto result = field && heights && sediment && settings && reaction && thermal
                          ? field->advanceHydraulic(*heights, *sediment, *settings, *reaction, *thermal, iterations)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    water.addFunc("getWidth", [](const TerrainWaterField* field) { return field ? field->getWidth() : 0; });
    water.addFunc("getHeight", [](const TerrainWaterField* field) { return field ? field->getHeight() : 0; });
    water.addFunc("reset", [vm = table.getHandle()](TerrainWaterField* field, int width, int height, float depth) {
        auto result = field ? field->reset(width, height, depth) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int count) { return eve::Value(count); });
    });
    water.addFunc("advance", [vm = table.getHandle()](TerrainWaterField* field, const Heightmap* terrain,
                                                      const TerrainWaterSettings* settings) {
        auto result = field && terrain && settings ? field->advance(*terrain, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int count) { return eve::Value(count); });
    });
    water.addFunc("sample", [vm = table.getHandle()](const TerrainWaterField* field, int x, int z) {
        auto result = field ? field->sample(x, z)
                            : Result<TerrainWaterSample>::failure(Diagnostic::error(
                                  DiagnosticCode::InvalidArgument, "terrain.water.sample: missing field"));
        return eve::script::projectResult(vm, std::move(result), [](const TerrainWaterSample& cell) {
            return eve::Value(eve::Value::Object{{"depth", eve::Value(cell.depth)},
                                                 {"velocityX", eve::Value(cell.velocityX)},
                                                 {"velocityZ", eve::Value(cell.velocityZ)},
                                                 {"fluxRight", eve::Value(cell.fluxRight)},
                                                 {"fluxLeft", eve::Value(cell.fluxLeft)},
                                                 {"fluxBottom", eve::Value(cell.fluxBottom)},
                                                 {"fluxTop", eve::Value(cell.fluxTop)}});
        });
    });

    auto heightmap = table.addClass<Heightmap>(
        "ProcgenHeightmap", std::function<Heightmap*()>([]() -> Heightmap* { return nullptr; }), true);
    heightmap.addFunc("resize", &Heightmap::resize);
    auto imageMask = table.addClass("TerrainImageMaskSettings", ssq::Class::Ctor<TerrainImageMaskSettings()>());
    imageMask.addVar("offsetX", &TerrainImageMaskSettings::offsetX);
    imageMask.addVar("offsetZ", &TerrainImageMaskSettings::offsetZ);
    imageMask.addVar("scaleX", &TerrainImageMaskSettings::scaleX);
    imageMask.addVar("scaleZ", &TerrainImageMaskSettings::scaleZ);
    imageMask.addVar("rotation", &TerrainImageMaskSettings::rotation);
    imageMask.addVar("red", &TerrainImageMaskSettings::red);
    imageMask.addVar("green", &TerrainImageMaskSettings::green);
    imageMask.addVar("blue", &TerrainImageMaskSettings::blue);
    imageMask.addVar("accuracy", &TerrainImageMaskSettings::accuracy);
    imageMask.addVar("tiling", &TerrainImageMaskSettings::tiling);
    auto collisionMask = table.addClass("TerrainCollisionMaskStack", ssq::Class::Ctor<TerrainCollisionMaskStack()>());
    collisionMask.addFunc("addLayer", [vm = table.getHandle()](TerrainCollisionMaskStack* self,
                                                                  const Heightmap* mask, int type, bool active,
                                                                  bool invert) {
        auto result = mask ? self->addLayer(*mask, static_cast<TerrainCollisionMaskType>(type), active, invert)
                           : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    collisionMask.addFunc("clear", &TerrainCollisionMaskStack::clear);
    collisionMask.addFunc("getLayerCount", [](const TerrainCollisionMaskStack* self) { return self->getLayerCount(); });
    collisionMask.addFunc("apply", [vm = table.getHandle()](const TerrainCollisionMaskStack* self,
                                                               Heightmap* target, const Heightmap* input,
                                                               const Heightmap* curve, int mode) {
        auto result = target && input && curve
                          ? self->apply(*target, *input, *curve, static_cast<TerrainMaskBlend>(mode))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    auto polygonMask = table.addClass("TerrainPolygonMask", ssq::Class::Ctor<TerrainPolygonMask()>());
    polygonMask.addFunc("addNode", [vm = table.getHandle()](TerrainPolygonMask* self, float x, float z, float radius,
                                                             float strength) {
        return eve::script::projectResult(vm, self->addNode(x, z, radius, strength),
                                          [](int n) { return eve::Value(n); });
    });
    polygonMask.addFunc("clear", &TerrainPolygonMask::clear);
    polygonMask.addFunc("getNodeCount", [](const TerrainPolygonMask* self) { return self->getNodeCount(); });
    polygonMask.addFunc("rasterize", [vm = table.getHandle()](const TerrainPolygonMask* self, Heightmap* target,
                                                               const Heightmap* brush, float originX, float originZ,
                                                               float spacingX, float spacingZ, int type) {
        TerrainSampleGrid grid;
        if (target) {
            grid = {originX, originZ, spacingX, spacingZ, target->getWidth(), target->getHeight()};
        }
        auto result = target && brush
                          ? self->rasterize(*target, *brush, grid, static_cast<TerrainPolygonMaskType>(type))
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    auto bakedMasks = table.addClass("TerrainBakedMaskCache", ssq::Class::Ctor<TerrainBakedMaskCache()>());
    bakedMasks.addFunc("store", [vm = table.getHandle()](TerrainBakedMaskCache* self, const std::string& terrainId,
                                                          const std::string& maskGuid, const Heightmap* raster) {
        auto result = raster ? self->store(terrainId, maskGuid, *raster) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    bakedMasks.addFunc("markDirty", [vm = table.getHandle()](TerrainBakedMaskCache* self,
                                                              const std::string& maskGuid) {
        return eve::script::projectResult(vm, self->markDirty(maskGuid), [](int n) { return eve::Value(n); });
    });
    bakedMasks.addFunc("erase", [vm = table.getHandle()](TerrainBakedMaskCache* self,
                                                          const std::string& terrainId,
                                                          const std::string& maskGuid) {
        return eve::script::projectResult(vm, self->erase(terrainId, maskGuid),
                                          [](int n) { return eve::Value(n); });
    });
    bakedMasks.addFunc("clear", &TerrainBakedMaskCache::clear);
    bakedMasks.addFunc("getEntryCount", [](const TerrainBakedMaskCache* self) { return self->getEntryCount(); });
    bakedMasks.addFunc("copyMask", [vm = table.getHandle()](const TerrainBakedMaskCache* self,
                                                             const std::string& terrainId,
                                                             const std::string& maskGuid, Heightmap* output) {
        auto result = output ? self->copyMask(terrainId, maskGuid, *output) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc("generateImageMask",
                      [vm = table.getHandle()](Heightmap* target, const Heightmap* input, const Heightmap* red,
                                               const Heightmap* green, const Heightmap* blue, const Heightmap* alpha,
                                               const Heightmap* curve, const TerrainImageMaskSettings* settings,
                                               int filter, int mode) {
                          auto result = [&]() -> Result<int> {
                              if (target && input && red && green && blue && alpha && curve && settings) {
                                  auto copy   = *settings;
                                  copy.filter = static_cast<TerrainImageFilter>(filter);
                                  return generateTerrainImageMask(*target, *input, *red, *green, *blue, *alpha, *curve,
                                                                  copy, static_cast<TerrainMaskBlend>(mode));
                              }
                              return missingRaster();
                          }();
                          return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                      });
    heightmap.addFunc("applyGlobalSpawnerMask",
                      [vm = table.getHandle()](Heightmap* target, const Heightmap* input, const Heightmap* source,
                                               const Heightmap* curve, const TerrainImageMaskSettings* settings,
                                               int mode) {
                          auto result = target && input && source && curve && settings
                                            ? applyTerrainGlobalSpawnerMask(*target, *input, *source, *curve,
                                                                            *settings,
                                                                            static_cast<TerrainMaskBlend>(mode))
                                            : missingRaster();
                          return eve::script::projectResult(vm, std::move(result),
                                                            [](int n) { return eve::Value(n); });
                      });
    auto noiseMask = table.addClass("TerrainNoiseMaskSettings", ssq::Class::Ctor<TerrainNoiseMaskSettings()>());
    noiseMask.addVar("translationX", &TerrainNoiseMaskSettings::translationX);
    noiseMask.addVar("translationZ", &TerrainNoiseMaskSettings::translationZ);
    noiseMask.addVar("scaleX", &TerrainNoiseMaskSettings::scaleX);
    noiseMask.addVar("scaleZ", &TerrainNoiseMaskSettings::scaleZ);
    noiseMask.addVar("rotation", &TerrainNoiseMaskSettings::rotation);
    noiseMask.addVar("octaves", &TerrainNoiseMaskSettings::octaves);
    noiseMask.addVar("amplitude", &TerrainNoiseMaskSettings::amplitude);
    noiseMask.addVar("frequency", &TerrainNoiseMaskSettings::frequency);
    noiseMask.addVar("persistence", &TerrainNoiseMaskSettings::persistence);
    noiseMask.addVar("lacunarity", &TerrainNoiseMaskSettings::lacunarity);
    noiseMask.addVar("warpIterations", &TerrainNoiseMaskSettings::warpIterations);
    noiseMask.addVar("warpStrength", &TerrainNoiseMaskSettings::warpStrength);
    noiseMask.addVar("warpOffsetX", &TerrainNoiseMaskSettings::warpOffsetX);
    noiseMask.addVar("warpOffsetZ", &TerrainNoiseMaskSettings::warpOffsetZ);
    noiseMask.addVar("seed", &TerrainNoiseMaskSettings::seed);
    heightmap.addFunc("generateNoiseMask",
                      [vm = table.getHandle()](Heightmap* target, const Heightmap* input, const Heightmap* curve,
                                               const TerrainNoiseMaskSettings* settings, int type, int mode) {
                          auto result = [&]() -> Result<int> {
                              if (!target || !input || !curve || !settings) return missingRaster();
                              auto copy = *settings;
                              copy.type = static_cast<TerrainNoiseType>(type);
                              return generateTerrainNoiseMask(*target, *input, *curve, copy,
                                                              static_cast<TerrainMaskBlend>(mode));
                          }();
                          return eve::script::projectResult(vm, std::move(result),
                                                            [](int n) { return eve::Value(n); });
                      });

    auto concavity = table.addClass("TerrainConcavitySettings", ssq::Class::Ctor<TerrainConcavitySettings()>());
    concavity.addVar("featureSize", &TerrainConcavitySettings::featureSize);
    concavity.addVar("concavity", &TerrainConcavitySettings::concavity);
    heightmap.addFunc(
        "generateConcavityMask",
        [vm = table.getHandle()](Heightmap* target, const Heightmap* input, const Heightmap* heights,
                                 const Heightmap* curve, const TerrainConcavitySettings* settings, int mode) {
            auto result = target && input && heights && curve && settings
                              ? generateTerrainConcavityMask(*target, *input, *heights, *curve, *settings,
                                                             static_cast<TerrainMaskBlend>(mode))
                              : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });

    auto curvature = table.addClass("TerrainCurvatureSettings", ssq::Class::Ctor<TerrainCurvatureSettings()>());
    curvature.addVar("radius", &TerrainCurvatureSettings::radius);
    curvature.addVar("worldUnits", &TerrainCurvatureSettings::worldUnits);
    curvature.addVar("intensity", &TerrainCurvatureSettings::intensity);
    curvature.addVar("steps", &TerrainCurvatureSettings::steps);
    curvature.addVar("directions", &TerrainCurvatureSettings::directions);
    heightmap.addFunc(
        "generateCurvatureMask",
        [vm = table.getHandle()](Heightmap* target, const Heightmap* input, const Heightmap* heights,
                                 const Heightmap* curve, const TerrainCurvatureSettings* settings, int mode) {
            auto result = target && input && heights && curve && settings
                              ? generateTerrainCurvatureMask(*target, *input, *heights, *curve, *settings,
                                                             static_cast<TerrainMaskBlend>(mode))
                              : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });

    heightmap.addFunc("growShrinkMask", [vm = table.getHandle()](Heightmap* target, const Heightmap* source,
                                                                 const Heightmap* curve, float distance) {
        auto result =
            target && source && curve ? growShrinkTerrainMask(*target, *source, *curve, distance) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });

    heightmap.addFunc(
        "applyStrength", [vm = table.getHandle()](Heightmap* target, const Heightmap* source, const Heightmap* curve,
                                                  int mode, float strength, bool invert) {
            auto result = target && source && curve
                              ? applyTerrainStrength(*target, *source, *curve, static_cast<TerrainStrengthMode>(mode),
                                                     strength, invert)
                              : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    heightmap.addFunc("generateErosionMask",
                      [vm = table.getHandle()](Heightmap* target, const Heightmap* oldHeights, const Heightmap* erosion,
                                               const Heightmap* brush, const TerrainBrushBlendSettings* spatial,
                                               const Heightmap* curve, int mode, float strength, bool userInvert) {
                          auto result = target && oldHeights && erosion && brush && spatial && curve
                                            ? generateTerrainErosionMask(
                                                  *target, *oldHeights, *erosion, *brush, *spatial, *curve,
                                                  static_cast<TerrainStrengthMode>(mode), strength, userInvert)
                                            : missingRaster();
                          return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                      });

    auto sampleGrid = table.addClass("TerrainSampleGrid", ssq::Class::Ctor<TerrainSampleGrid()>());
    sampleGrid.addVar("width", &TerrainSampleGrid::width);
    sampleGrid.addVar("height", &TerrainSampleGrid::height);
    sampleGrid.addFunc("setOrigin", [](TerrainSampleGrid* grid, float x, float z) {
        grid->originX = x;
        grid->originZ = z;
    });
    sampleGrid.addFunc("setSpacing", [](TerrainSampleGrid* grid, float x, float z) {
        grid->spacingX = x;
        grid->spacingZ = z;
    });
    auto brushBlend = table.addClass("TerrainBrushBlendSettings", ssq::Class::Ctor<TerrainBrushBlendSettings()>());
    brushBlend.addFunc(
        "configureWorld", [vm = table.getHandle()](TerrainBrushBlendSettings* target, const TerrainStampSettings* world,
                                                   int width, int height, const TerrainSampleGrid* grid) {
            auto result = target && world && grid ? configureTerrainBrushWorld(*target, *world, width, height, *grid)
                                                  : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });

    brushBlend.addVar("heightXX", &TerrainBrushBlendSettings::heightXX);
    brushBlend.addVar("heightXZ", &TerrainBrushBlendSettings::heightXZ);
    brushBlend.addVar("heightZX", &TerrainBrushBlendSettings::heightZX);
    brushBlend.addVar("heightZZ", &TerrainBrushBlendSettings::heightZZ);
    brushBlend.addVar("heightOffsetX", &TerrainBrushBlendSettings::heightOffsetX);
    brushBlend.addVar("heightOffsetZ", &TerrainBrushBlendSettings::heightOffsetZ);
    brushBlend.addVar("brushXX", &TerrainBrushBlendSettings::brushXX);
    brushBlend.addVar("brushXZ", &TerrainBrushBlendSettings::brushXZ);
    brushBlend.addVar("brushZX", &TerrainBrushBlendSettings::brushZX);
    brushBlend.addVar("brushZZ", &TerrainBrushBlendSettings::brushZZ);
    brushBlend.addVar("brushOffsetX", &TerrainBrushBlendSettings::brushOffsetX);
    brushBlend.addVar("brushOffsetZ", &TerrainBrushBlendSettings::brushOffsetZ);
    brushBlend.addVar("strength", &TerrainBrushBlendSettings::strength);
    heightmap.addFunc("blendBrush", [vm = table.getHandle()](Heightmap* target, const Heightmap* oldHeights,
                                                             const Heightmap* newHeights, const Heightmap* brush,
                                                             const TerrainBrushBlendSettings* settings) {
        auto result = target && oldHeights && newHeights && brush && settings
                          ? blendTerrainBrush(*target, *oldHeights, *newHeights, *brush, *settings)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });

    heightmap.addFunc(
        "applySediment", [vm = table.getHandle()](Heightmap* heights, Heightmap* sediment, const Heightmap* velocityX,
                                                  const Heightmap* velocityZ, const TerrainWaterSettings* water,
                                                  const TerrainSedimentSettings* settings) {
            auto result = heights && sediment && velocityX && velocityZ && water && settings
                              ? applyTerrainSediment(*heights, *sediment, *velocityX, *velocityZ, *water, *settings)
                              : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    heightmap.addFunc("applyThermal", [vm = table.getHandle()](Heightmap* heights, Heightmap* sediment,
                                                               const TerrainThermalSettings* settings) {
        auto result =
            heights && sediment && settings ? applyTerrainThermal(*heights, *sediment, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc(
        "applyHeightCurve", [vm = table.getHandle()](Heightmap* target, const Heightmap* mask, const Heightmap* curve,
                                                     float minimum, float maximum) {
            auto result = target && mask && curve ? applyTerrainHeightCurve(*target, *mask, *curve, minimum, maximum)
                                                  : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    heightmap.addFunc("applyHeightMix", [vm = table.getHandle()](Heightmap* target, const Heightmap* local,
                                                                 const Heightmap*                global,
                                                                 const TerrainHeightMixSettings* settings) {
        auto result = target && local && global && settings ? applyTerrainHeightMix(*target, *local, *global, *settings)
                                                            : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc("applySmooth", [vm = table.getHandle()](Heightmap* target, const Heightmap* mask,
                                                              const TerrainSmoothSettings* settings) {
        auto result = target && mask && settings ? applyTerrainSmooth(*target, *mask, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc("applyRidges", [vm = table.getHandle()](Heightmap* target, const Heightmap* mask,
                                                              const TerrainRidgeSettings* settings) {
        auto result = target && mask && settings ? applyTerrainRidges(*target, *mask, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc("applyTerrace", [vm = table.getHandle()](Heightmap* target, const Heightmap* mask,
                                                               const TerrainTerraceSettings* settings) {
        auto result = target && mask && settings ? applyTerrainTerrace(*target, *mask, *settings) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc("applyPower", [vm = table.getHandle()](Heightmap* target, const Heightmap* mask, float power) {
        auto result = target && mask ? applyTerrainPower(*target, *mask, power) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc("getWidth", &Heightmap::getWidth);
    heightmap.addFunc("getHeight", &Heightmap::getHeight);
    heightmap.addFunc("setHeight", &Heightmap::setHeight);
    heightmap.addFunc("height", &Heightmap::height);
    heightmap.addFunc("sampleBilinear", &Heightmap::sampleBilinear);
    heightmap.addFunc("sampleBilinearSeamless", &Heightmap::sampleBilinearSeamless);
    heightmap.addFunc("hasData", [vm = table.getHandle()](const Heightmap* source) {
        auto result = source ? terrainHeightmapHasData(*source)
                             : Result<bool>::failure(
                                   Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain: heightmap required"));
        return eve::script::projectResult(vm, std::move(result), [](bool value) { return eve::Value(value); });
    });
    heightmap.addFunc("isPowerOfTwo", [vm = table.getHandle()](const Heightmap* source) {
        auto result = source ? terrainHeightmapIsPowerOfTwo(*source)
                             : Result<bool>::failure(
                                   Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain: heightmap required"));
        return eve::script::projectResult(vm, std::move(result), [](bool value) { return eve::Value(value); });
    });
    heightmap.addFunc("blendMask", [vm = table.getHandle()](Heightmap* target, const Heightmap* source, int mode,
                                                            float strength, bool invert) {
        auto result = target && source
                          ? blendTerrainMask(*target, *source, static_cast<TerrainMaskBlend>(mode), strength, invert)
                          : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int changed) { return eve::Value(changed); });
    });
    heightmap.addFunc("applyStamp", [vm = table.getHandle()](Heightmap* target, const Heightmap* stamp,
                                                             const TerrainStampSettings* config, int operation,
                                                             const Heightmap* local, const Heightmap* global) {
        if (!target || !stamp || !config || !local || !global)
            return eve::script::projectResult(vm, missingRaster(), [](int changed) { return eve::Value(changed); });
        auto copy      = *config;
        copy.operation = static_cast<TerrainStampOperation>(operation);
        return eve::script::projectResult(vm, applyTerrainStamp(*target, *stamp, copy, *local, *global),
                                          [](int changed) { return eve::Value(changed); });
    });
    heightmap.addFunc(
        "transformMask", [vm = table.getHandle()](Heightmap* target, const Heightmap* source, const Heightmap* curve) {
            auto result = target && source && curve ? transformTerrainMask(*target, *source, *curve) : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    heightmap.addFunc("smoothMask", [vm = table.getHandle()](Heightmap* target, const Heightmap* input,
                                                              float verticality, float blurRadius) {
        auto result = target && input ? smoothTerrainMask(*target, *input, verticality, blurRadius) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    auto legacyHydraulic = table.addClass("TerrainLegacyHydraulicSettings",
                                          ssq::Class::Ctor<TerrainLegacyHydraulicSettings()>());
    legacyHydraulic.addVar("iterations", &TerrainLegacyHydraulicSettings::iterations);
    legacyHydraulic.addVar("rainFrequency", &TerrainLegacyHydraulicSettings::rainFrequency);
    legacyHydraulic.addVar("sedimentDissolveRate", &TerrainLegacyHydraulicSettings::sedimentDissolveRate);
    heightmap.addFunc("applyLegacyHydraulic",
                      [vm = table.getHandle()](Heightmap* heights, Heightmap* sediment, const Heightmap* hardness,
                                               const Heightmap* rain,
                                               const TerrainLegacyHydraulicSettings* settings) {
                          auto result = heights && sediment && hardness && rain && settings
                                            ? applyTerrainLegacyHydraulic(*heights, *sediment, *hardness, *rain,
                                                                          *settings)
                                            : missingRaster();
                          return eve::script::projectResult(vm, std::move(result),
                                                            [](int n) { return eve::Value(n); });
                      });
    auto distributedErosion = table.addClass("TerrainLegacyDistributedErosionSettings",
                                              ssq::Class::Ctor<TerrainLegacyDistributedErosionSettings()>());
    distributedErosion.addVar("minimumThreshold", &TerrainLegacyDistributedErosionSettings::minimumThreshold);
    distributedErosion.addVar("maximumThreshold", &TerrainLegacyDistributedErosionSettings::maximumThreshold);
    distributedErosion.addVar("iterations", &TerrainLegacyDistributedErosionSettings::iterations);
    heightmap.addFunc("applyLegacyDistributedErosion",
                      [vm = table.getHandle()](Heightmap* target,
                                               const TerrainLegacyDistributedErosionSettings* settings) {
                          auto result = target && settings ? applyTerrainLegacyDistributedErosion(*target, *settings)
                                                           : missingRaster();
                          return eve::script::projectResult(vm, std::move(result),
                                                            [](int n) { return eve::Value(n); });
                      });
    auto steepestErosion = table.addClass("TerrainLegacySteepestErosionSettings",
                                           ssq::Class::Ctor<TerrainLegacySteepestErosionSettings()>());
    steepestErosion.addVar("iterations", &TerrainLegacySteepestErosionSettings::iterations);
    steepestErosion.addVar("talusMinimum", &TerrainLegacySteepestErosionSettings::talusMinimum);
    steepestErosion.addVar("talusMaximum", &TerrainLegacySteepestErosionSettings::talusMaximum);
    heightmap.addFunc("applyLegacySteepestErosion",
                      [vm = table.getHandle()](Heightmap* target, const Heightmap* hardness,
                                               const TerrainLegacySteepestErosionSettings* settings) {
                          auto result = target && hardness && settings
                                            ? applyTerrainLegacySteepestErosion(*target, *hardness, *settings)
                                            : missingRaster();
                          return eve::script::projectResult(vm, std::move(result),
                                                            [](int n) { return eve::Value(n); });
                      });
    heightmap.addFunc(
        "rangeMask", [vm = table.getHandle()](Heightmap* target, const Heightmap* source, float minimum, float maximum,
                                              const Heightmap* curve, const Heightmap* strength) {
            auto result = target && source && curve && strength
                              ? generateTerrainRangeMask(*target, *source, minimum, maximum, *curve, *strength)
                              : missingRaster();
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
    heightmap.addFunc("applyContrast", [vm = table.getHandle()](Heightmap* target, const Heightmap* mask,
                                                                float strength, float size) {
        auto result = target && mask ? applyTerrainContrast(*target, *mask, strength, size) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc("deriveSlope", [vm = table.getHandle()](Heightmap* target, const Heightmap* heights, float dx,
                                                              float dz, float scale) {
        auto result = target && heights ? deriveTerrainSlope(*target, *heights, dx, dz, scale) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    heightmap.addFunc(
        "distanceMask", [vm = table.getHandle()](Heightmap* target, const TerrainDistanceMaskSettings* settings,
                                                 int axis, const Heightmap* curve, const Heightmap* strength) {
            if (!target || !settings || !curve || !strength)
                return eve::script::projectResult(vm, missingRaster(), [](int n) { return eve::Value(n); });
            auto copy = *settings;
            copy.axis = static_cast<TerrainDistanceAxis>(axis);
            return eve::script::projectResult(vm, generateTerrainDistanceMask(*target, copy, *curve, *strength),
                                              [](int n) { return eve::Value(n); });
        });
}
}  // namespace eve::procgen
