#include "common/BorrowedRef.h"
#include "common/Capability.h"
#include "common/ProcgenProbeSink.h"
#include "procgen/MeshBuild.h"
#include "procgen/PcgSpawnProgress.h"
#include "procgen/PointSet.h"
#include "procgen/ProcgenLive.h"
#include "procgen/ProcgenScriptSupport.h"
#include "procgen/heightmap/PcgBiomeController.h"
#include "procgen/heightmap/PcgRuntimeStamper.h"
#include "procgen/heightmap/PcgTerrainWatcher.h"
#include "procgen/heightmap/PcgTreeManager.h"
#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainDetailPlacement.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/TerrainImageMask.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTerraceRemover.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/heightmap/TerrainWaterField.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadTerrainWorkspace.h"

#include "common/SquirrelBinding.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainBakedMaskCache.h"
#include "procgen/heightmap/TerrainBiomePreset.h"
#include "procgen/heightmap/TerrainCollisionMask.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainNoiseMask.h"
#include "procgen/heightmap/TerrainPolygonMask.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainStamp.h"

#include <functional>
#include <limits>
#include <type_traits>

namespace eve::procgen {
namespace {
template <class T>
ResultRef<T> borrowRoadArgument(const ssq::Object& object, const char* path) {
    if (object.isNull())
        return ResultRef<T>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "road terrain argument is required", path, {}, "procgen.squirrel"));
    if (object.getType() != ssq::Type::INSTANCE || object.getTypeTag() != ssq::detail::stableTypeHash<T*>())
        return ResultRef<T>::failure(
            Diagnostic::error(DiagnosticCode::TypeMismatch,
                              std::string("road terrain argument has the wrong instance type: ") + object.getTypeStr(),
                              path, {}, "procgen.squirrel"));
    auto* value = object.toPtrUnsafe<T*>();
    if (!value)
        return ResultRef<T>::failure(Diagnostic::error(DiagnosticCode::StaleHandle,
                                                       "road terrain argument has no live native instance", path, {},
                                                       "procgen.squirrel"));
    if constexpr (std::is_same_v<T, MeshBuild> || std::is_same_v<T, Heightmap>) {
        using Ref =
            std::conditional_t<std::is_same_v<T, MeshBuild>, ProcgenMeshBuildHandleRef, ProcgenHeightmapHandleRef>;
        if (auto reference = nativeProxyReference<Ref>(value)) {
            auto* owner    = liveProcgen();
            auto  resolved = [&]() -> eve::script::Borrowed<T> {
                if (!owner) return {};
                if constexpr (std::is_same_v<T, MeshBuild>)
                    return owner->resolveMeshBuild(*reference);
                else
                    return owner->resolveHeightmap(*reference);
            }();
            if (!resolved.isBound() || resolved.get() != value)
                return ResultRef<T>::failure(Diagnostic::error(DiagnosticCode::StaleHandle,
                                                               "road terrain argument owner or generation is stale",
                                                               path, {}, "procgen.squirrel"));
        }
    }
    return ResultRef<T>::success(std::ref(*value));
}
Result<int> missingRaster() {
    return Result<int>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain operation requires non-null rasters and settings"));
}
}  // namespace
void exposeTerrainWorldCreationBindings(ssq::Table& table) {
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
    worldSettings.addVar("originY", &TerrainWorldCreationSettings::originY);
}

void exposeTerrainWorldBindings(ssq::Table& table) {
    auto roadWrite = table.addClass("RoadTerrainWriteOptions", ssq::Class::Ctor<road::RoadTerrainWriteOptions()>());
    roadWrite.addVar("blendDistance", &road::RoadTerrainWriteOptions::blendDistance);
    roadWrite.addVar("maxVerticalDelta", &road::RoadTerrainWriteOptions::maxVerticalDelta);
    roadWrite.addVar("splatLayer", &road::RoadTerrainWriteOptions::splatLayer);
    roadWrite.addVar("splatOpacity", &road::RoadTerrainWriteOptions::splatOpacity);
    roadWrite.addVar("clearDetails", &road::RoadTerrainWriteOptions::clearDetails);
    roadWrite.addVar("clearTrees", &road::RoadTerrainWriteOptions::clearTrees);
    roadWrite.addVar("cutTunnelHoles", &road::RoadTerrainWriteOptions::cutTunnelHoles);
    roadWrite.addVar("tunnelHolesAtPortalsOnly", &road::RoadTerrainWriteOptions::tunnelHolesAtPortalsOnly);
    roadWrite.addFunc("setMaximumWork", [vm = table.getHandle()](road::RoadTerrainWriteOptions* self, int visits) {
        auto result = [&]() -> Result<int> {
            if (visits <= 0)
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "road terrain work budget must be positive",
                                                              "maximumWork", {}, "procgen.squirrel"));
            self->maximumWork = static_cast<std::size_t>(visits);
            return Result<int>::success(visits);
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    roadWrite.addFunc("getMaximumWork", [](const road::RoadTerrainWriteOptions* self) {
        return static_cast<std::int64_t>(self->maximumWork);
    });
    auto world = table.addClass("TerrainWorldWorkspace", ssq::Class::Ctor<TerrainWorldWorkspace()>());
    world.addFunc("conformRoadBatch", [vm = table.getHandle()](TerrainWorldWorkspace* self, ssq::Object meshes,
                                                               ssq::Object policies, ssq::Object budget) {
        auto result = [&]() -> Result<int> {
            if (budget.getType() != ssq::Type::INTEGER)
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch,
                                                              "road terrain batch budget must be an integer",
                                                              "maximumWork", {}, "procgen.squirrel"));
            const auto maximumWork = budget.toInt();
            if (maximumWork <= 0 || static_cast<std::uint64_t>(maximumWork) > std::numeric_limits<std::size_t>::max())
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "road terrain batch budget is out of range",
                                                              "maximumWork", {}, "procgen.squirrel"));
            if (meshes.getType() != ssq::Type::ARRAY || policies.getType() != ssq::Type::ARRAY)
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch,
                                                              "road terrain batch requires mesh and policy arrays",
                                                              "writes", {}, "procgen.squirrel"));
            auto       sources = meshes.toArray();
            auto       options = policies.toArray();
            const auto count   = sources.size();
            if (count == 0 || count > 4096u || options.size() != count)
                return Result<int>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "road terrain batch arrays must have matching sizes in [1,4096]",
                    "writes", {}, "procgen.squirrel"));
            std::vector<road::RoadTerrainWrite> writes;
            writes.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                const auto sourcePath = "meshes[" + std::to_string(i) + "]";
                auto       source     = borrowRoadArgument<MeshBuild>(sources.get<ssq::Object>(i), sourcePath.c_str());
                if (!source.ok()) return Result<int>::failure(source.status());
                const auto policyPath = "policies[" + std::to_string(i) + "]";
                auto       policy =
                    borrowRoadArgument<road::RoadTerrainWriteOptions>(options.get<ssq::Object>(i), policyPath.c_str());
                if (!policy.ok()) return Result<int>::failure(policy.status());
                writes.push_back({std::cref(source.value().get()), policy.value().get()});
            }
            return self->conformRoadBatch(writes, static_cast<std::size_t>(maximumWork));
        }();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("conformRoad",
                  [vm = table.getHandle()](TerrainWorldWorkspace* self, ssq::Object mesh, ssq::Object options) {
                      auto result = [&]() -> Result<int> {
                          auto source = borrowRoadArgument<MeshBuild>(mesh, "mesh");
                          if (!source.ok()) return Result<int>::failure(source.status());
                          auto policy = borrowRoadArgument<road::RoadTerrainWriteOptions>(options, "options");
                          if (!policy.ok()) return Result<int>::failure(policy.status());
                          return self->conformRoad(source.value().get(), policy.value().get());
                      }();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc(
        "copyHoles", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index, ssq::Object output) {
            auto destination = borrowRoadArgument<Heightmap>(output, "output");
            if (!destination.ok())
                return eve::script::projectResult(vm, Result<int>::failure(destination.status()),
                                                  [](int n) { return eve::Value(n); });
            auto result = self->copyHoles(index);
            if (!result.ok())
                return eve::script::projectResult(vm, Result<int>::failure(result.status()),
                                                  [](int n) { return eve::Value(n); });
            auto& raster = destination.value().get();
            raster       = std::move(result).takeValue();
            return eve::script::projectResult(vm, Result<int>::success(raster.getWidth() * raster.getHeight()),
                                              [](int n) { return eve::Value(n); });
        });
    world.addFunc("create",
                  [vm = table.getHandle()](TerrainWorldWorkspace* self, const TerrainWorldCreationSettings* settings) {
                      auto result = settings ? self->create(*settings) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("stamp", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* stamp,
                                                    const TerrainStampSettings* settings, const Heightmap* local,
                                                    const Heightmap* global) {
        auto result =
            stamp && settings && local && global ? self->stamp(*stamp, *settings, *local, *global) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("paintSplat", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* paint, int layer,
                                                         const TerrainStampSettings* operation) {
        auto result = paint && operation ? self->paintSplat(*paint, layer, *operation) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("spawn", [vm = table.getHandle()](TerrainWorldWorkspace* self, const TerrainSpawnPlan* plan) {
        auto result = plan ? self->spawn(*plan) : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("spawnBiome",
                  [vm = table.getHandle()](TerrainWorldWorkspace* self, const TerrainBiomePreset* preset) {
                      auto result = preset ? self->spawnBiome(*preset) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("spawnStamper",
                  [vm = table.getHandle()](TerrainWorldWorkspace* self, const TerrainBiomePreset* preset) {
                      auto result = preset ? self->spawnStamper(*preset) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("beginSpawn", [vm = table.getHandle()](TerrainWorldWorkspace* self, const TerrainSpawnPlan* plan) {
        auto result = plan ? self->beginSpawn(*plan)
                           : Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
                                 DiagnosticCode::InvalidArgument, "terrain.world.beginSpawn: plan required"));
        return eve::script::projectResult(vm, std::move(result),
                                          [](TerrainWorldRunStatus status) { return eve::Value(int(status)); });
    });
    world.addFunc("beginSpawnBiome", [vm = table.getHandle()](TerrainWorldWorkspace*    self,
                                                              const TerrainBiomePreset* preset) {
        auto result = preset ? self->beginSpawnBiome(*preset)
                             : Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
                                   DiagnosticCode::InvalidArgument, "terrain.world.beginSpawnBiome: preset required"));
        return eve::script::projectResult(vm, std::move(result),
                                          [](TerrainWorldRunStatus status) { return eve::Value(int(status)); });
    });
    world.addFunc(
        "beginSpawnStamper", [vm = table.getHandle()](TerrainWorldWorkspace* self, const TerrainBiomePreset* preset) {
            auto result =
                preset ? self->beginSpawnStamper(*preset)
                       : Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
                             DiagnosticCode::InvalidArgument, "terrain.world.beginSpawnStamper: preset required"));
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
                                                          const TerrainStampSettings* operation, int mode, int seed) {
        auto result = fitness && settings && operation ? self->applyDetail(*fitness, *settings, *operation,
                                                                           static_cast<TerrainDetailMode>(mode), seed)
                                                       : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("applyTrees", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* fitness,
                                                         const TerrainTreePlacementSettings* settings,
                                                         const TerrainStampSettings* operation, int mode) {
        auto result = fitness && settings && operation ? self->applyTrees(*fitness, *settings, *operation,
                                                                          static_cast<TerrainTreeOperationMode>(mode))
                                                       : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("applyObjects", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* fitness,
                                                           const TerrainObjectPlacementSettings* settings,
                                                           const TerrainStampSettings* operation, int mode) {
        auto result =
            fitness && settings && operation
                ? self->applyObjects(*fitness, *settings, *operation, static_cast<TerrainObjectOperationMode>(mode))
                : missingRaster();
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    world.addFunc("applyProbes", [vm = table.getHandle()](TerrainWorldWorkspace* self, const Heightmap* fitness,
                                                          const TerrainProbePlacementSettings* settings,
                                                          const TerrainStampSettings* operation, int mode, int type) {
        auto result = [&]() -> Result<int> {
            if (!fitness || !settings || !operation) return missingRaster();
            auto copy = *settings;
            copy.type = static_cast<TerrainProbeType>(type);
            return self->applyProbes(*fitness, copy, *operation, static_cast<TerrainProbeOperationMode>(mode));
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
    world.addFunc("setHeightWorldUnits", [vm = table.getHandle()](TerrainWorldWorkspace* self, float heightWorldUnits,
                                                                  float worldHeightSpan) {
        return eve::script::projectResult(vm, self->setHeightWorldUnits(heightWorldUnits, worldHeightSpan),
                                          [](int n) { return eve::Value(n); });
    });
    world.addFunc("clearSpawns",
                  [vm = table.getHandle()](TerrainWorldWorkspace* self, const TerrainWorldClearSettings* settings) {
                      auto result = settings ? self->clearSpawns(*settings) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("copyHeightmap",
                  [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index, Heightmap* output) {
                      auto result = output ? self->copyHeightmap(index, *output) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("copySplatmap",
                  [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index, TerrainSplatmap* output) {
                      auto result = output ? self->copySplatmap(index, *output) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("copyDetail",
                  [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index, TerrainDetailLayer* output) {
                      auto result = output ? self->copyDetail(index, *output) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("copyTrees",
                  [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index, PointSet* output) {
                      auto result = output ? self->copyTrees(index, *output) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("copyObjects",
                  [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index, PointSet* output) {
                      auto result = output ? self->copyObjects(index, *output) : missingRaster();
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
    world.addFunc("copyProbes",
                  [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index, PointSet* output) {
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
    world.addFunc("getTileOriginY", [vm = table.getHandle()](const TerrainWorldWorkspace* self, int index) {
        return eve::script::projectResult(vm, self->getTileOriginY(index),
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
}
}  // namespace eve::procgen
