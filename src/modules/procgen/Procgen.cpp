#include "procgen/Procgen.h"

#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "common/SquirrelBinding.h"
#include "procgen/BiomeScript.h"
#include "procgen/PointGraphScript.h"
#include "procgen/ProcgenCapabilities.h"
#include "procgen/ShapeGrammarScript.h"

#include "image/ImageData.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/JsonExport.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/RoguelikeGenerator.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"

#include "data/ByteData.h"
#include "graphics/Graphics.h"
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

namespace {

/** @brief Script-owned proxy; the procedural object remains module-owned. */
struct ScriptProcgenParams {
    explicit ScriptProcgenParams(ProcgenParamsHandleRef value) : reference(value) {}
    ProcgenParamsHandleRef reference;
};

/** @brief Script-owned proxy; the procedural grid remains module-owned. */
struct ScriptProcgenGrid {
    explicit ScriptProcgenGrid(ProcgenGridHandleRef value) : reference(value) {}
    ProcgenGridHandleRef reference;
};

/** @brief Script-owned proxy; the rebuild context remains module-owned. */
struct ScriptProcgenContext {
    explicit ScriptProcgenContext(ProcgenContextHandleRef value) : reference(value) {}
    ProcgenContextHandleRef reference;
};

template <class T>
eve::Result<T> procgenBindingFailure(eve::DiagnosticCode code, std::string message, std::string path = {});

template <class T>
struct NativeProxyReleases {
    struct Record {
        std::any                           reference;
        std::function<eve::Result<void>()> release;
    };
    static inline std::mutex                     mutex;
    static inline std::unordered_map<T*, Record> records;
};

template <class Ref, class T>
std::optional<Ref> nativeProxyReference(T* native) {
    if (!native) return std::nullopt;
    std::lock_guard lock(NativeProxyReleases<T>::mutex);
    const auto      found = NativeProxyReleases<T>::records.find(native);
    if (found == NativeProxyReleases<T>::records.end()) return std::nullopt;
    if (const auto* reference = std::any_cast<Ref>(&found->second.reference)) return *reference;
    return std::nullopt;
}

template <class T>
SQInteger releaseNativeProxy(SQUserPointer pointer, SQInteger) {
    auto* native = static_cast<T*>(pointer);
    if (!native) return 0;

    std::function<eve::Result<void>()> release;
    {
        std::lock_guard lock(NativeProxyReleases<T>::mutex);
        const auto      found = NativeProxyReleases<T>::records.find(native);
        if (found == NativeProxyReleases<T>::records.end()) return 0;
        release = std::move(found->second.release);
        NativeProxyReleases<T>::records.erase(found);
    }
    release().ignore("release Squirrel procgen native proxy");
    return 0;
}

template <class T, class Ref, class Resolve, class Release>
ssq::Table makeOwnedNativeProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Resolve&& resolve, Release&& release) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status(), false, false);

    const auto ref  = std::move(reference).takeValue();
    const auto view = std::invoke(std::forward<Resolve>(resolve), ref);
    if (!view.isBound()) {
        std::invoke(release, ref).ignore("rollback unbound Squirrel procgen proxy");
        return eve::script::projectStatusResult(
            vm,
            procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                        "procgen proxy could not resolve its owned object", "procgen")
                .status(),
            false, false);
    }

    const SQInteger top = sq_gettop(vm);
    const size_t hashCode = eve::script::detail::squirrelTypeHash<T*>();
    sq_pushobject(vm, ssq::detail::getClassObj(vm, hashCode));
    if (SQ_FAILED(sq_createinstance(vm, -1))) {
        sq_settop(vm, top);
        std::invoke(release, ref).ignore("rollback failed Squirrel procgen instance");
        return eve::script::projectStatusResult(
            vm,
            procgenBindingFailure<void>(eve::DiagnosticCode::Failed, "failed to create Squirrel procgen proxy",
                                        "procgen")
                .status(),
            false, false);
    }
    sq_remove(vm, -2);
    auto* native = view.get();
    {
        std::lock_guard lock(NativeProxyReleases<T>::mutex);
        NativeProxyReleases<T>::records.emplace(
            native,
            typename NativeProxyReleases<T>::Record{
                ref, [ref, release = std::forward<Release>(release)]() mutable { return std::invoke(release, ref); }});
    }
    sq_setinstanceup(vm, -1, native);
    sq_settypetag(vm, -1, reinterpret_cast<SQUserPointer>(hashCode));
    sq_setreleasehook(vm, -1, &releaseNativeProxy<T>);

    ssq::Instance value(vm);
    sq_getstackobj(vm, -1, &value.getRaw());
    sq_addref(vm, &value.getRaw());
    sq_settop(vm, top);

    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", value);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

template <class T>
ssq::Table projectBorrowedResult(HSQUIRRELVM vm, eve::script::Borrowed<T> borrowed, const char* objectName) {
    if (!borrowed.isBound())
        return eve::script::projectStatusResult(
            vm,
            procgenBindingFailure<void>(eve::DiagnosticCode::Failed, std::string(objectName) + " could not be produced",
                                        objectName)
                .status(),
            false, false);
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", borrowed.get());
    result.set("ownership", std::string("borrowed"));
    return result;
}

ssq::Table makeOwnedPointSetProxy(HSQUIRRELVM vm, eve::Result<ProcgenPointSetHandleRef>&& reference) {
    auto* module = Procgen::create();
    return makeOwnedNativeProxy<PointSet>(
        vm, std::move(reference), [module](ProcgenPointSetHandleRef ref) { return module->resolvePointSet(ref); },
        [](ProcgenPointSetHandleRef ref) {
            auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
            return owner ? owner->releasePointSet(ref)
                         : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                       "Procgen module is no longer loaded", "pointSet");
        });
}

ssq::Table makeOwnedSpatialProxy(HSQUIRRELVM vm, eve::Result<ProcgenSpatialDataHandleRef>&& reference) {
    auto* module = Procgen::create();
    return makeOwnedNativeProxy<SpatialData>(
        vm, std::move(reference), [module](ProcgenSpatialDataHandleRef ref) { return module->resolveSpatialData(ref); },
        [](ProcgenSpatialDataHandleRef ref) {
            auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
            return owner ? owner->release(ref)
                         : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                       "Procgen module is no longer loaded", "spatialData");
        });
}

template <class T>
eve::Result<T> procgenBindingFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.squirrel"));
}

eve::Value boundsValue(const Bounds& bounds) {
    return eve::Value(eve::Value::Object{
        {"minX", eve::Value(static_cast<double>(bounds.minX))},
        {"minY", eve::Value(static_cast<double>(bounds.minY))},
        {"minZ", eve::Value(static_cast<double>(bounds.minZ))},
        {"maxX", eve::Value(static_cast<double>(bounds.maxX))},
        {"maxY", eve::Value(static_cast<double>(bounds.maxY))},
        {"maxZ", eve::Value(static_cast<double>(bounds.maxZ))},
        {"valid", eve::Value(bounds.valid)},
    });
}

eve::Value artifactProjection(GeneratedArtifact&& artifact) {
    eve::Value::Array dependencies;
    dependencies.reserve(artifact.dependencies.size());
    for (const ArtifactId& dependency : artifact.dependencies) dependencies.emplace_back(dependency.format());

    eve::Value::Object result;
    result.emplace("id", eve::Value(artifact.id.format()));
    result.emplace("type", eve::Value(std::string(artifactTypeName(artifact.type))));
    result.emplace("schemaVersion", eve::Value(static_cast<std::int64_t>(artifact.schemaVersion.value())));
    result.emplace("buildKey", eve::Value(artifact.buildKey.format()));
    result.emplace("bounds", boundsValue(artifact.bounds));
    result.emplace("dependencies", eve::Value(std::move(dependencies)));
    result.emplace("metadata", eve::Value(std::move(artifact.metadata)));
    if (artifact.type == ArtifactType::Composite) {
        const auto* composite = std::get_if<CompositeArtifact>(&artifact.payload);
        result.emplace("partCount", eve::Value(static_cast<std::int64_t>(composite ? composite->children.size() : 0)));
    } else {
        result.emplace("partCount", eve::Value(std::int64_t(1)));
    }
    return eve::Value(std::move(result));
}

eve::Value publishReceiptProjection(ArtifactPublishReceipt&& receipt) {
    return eve::Value(eve::Value::Object{
        {"id", eve::Value(receipt.id.format())},
        {"scenePublished", eve::Value(receipt.scenePublished)},
        {"graphicsPublished", eve::Value(receipt.graphicsPublished)},
        {"physicsPublished", eve::Value(receipt.physicsPublished)},
        {"mapPublished", eve::Value(receipt.mapPublished)},
    });
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status(), false, false);
    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned procgen proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned procgen allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

template <class T>
ssq::Table staleProcgenResult(HSQUIRRELVM vm, const char* objectName) {
    return eve::script::projectResult(
        vm, procgenBindingFailure<T>(eve::DiagnosticCode::StaleHandle,
                                     std::string("owned procgen ") + objectName + " handle is stale", objectName));
}

/** @brief Projects one native recipe schema through the canonical Result table.
 *
 * The schema object is rooted by Squirrel and remains an owning value of the
 * returned `value` field. A failed lookup never returns an empty schema
 * object, so callers cannot mistake a missing recipe for valid metadata.
 */
ssq::Table projectRecipeDescriptorResult(HSQUIRRELVM vm, eve::Result<RecipeDescriptor>&& result) {
    const bool        ok     = result.ok();
    const eve::Status status = result.status();
    if (!ok) return eve::script::projectStatusResult(vm, status, false, false);

    auto descriptor = std::move(result).takeValue();
    auto object     = eve::script::makeOwnedSquirrelInstance<RecipeDescriptor>(
        vm, std::make_unique<RecipeDescriptor>(std::move(descriptor)));
    if (!object) {
        const eve::Status failure = object.status();
        object.ignore("failed to create owned Procgen recipe schema");
        return eve::script::projectStatusResult(vm, failure, false, false);
    }

    auto projected = eve::script::projectStatusResult(vm, status, true, true);
    projected.set("value", std::move(object).takeValue());
    return projected;
}

template <class T, class Tag>
eve::Result<eve::script::RuntimeHandleRef<Tag>> ownProcgenObject(eve::script::RuntimeObjectRegistry<T, Tag>& registry,
                                                                 eve::script::Owned<T>                       object) {
    return registry.emplace(std::move(object));
}

}  // namespace

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

Procgen::~Procgen() = default;

eve::Result<ProcgenParamsHandleRef> Procgen::newParamsHandle() {
    Procgen* module = Procgen::create();
    return module->params_.emplace(std::make_unique<Params>());
}

eve::script::Borrowed<Params> Procgen::resolve(ProcgenParamsHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module) return {};
    return module->params_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenParamsHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                                           "params");
    return module->params_.erase(reference);
}

bool Procgen::isStale(ProcgenParamsHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return !module || module->params_.isStale(reference);
}

eve::Result<ProcgenOutputHandleRef> Procgen::newOutputHandle() {
    Procgen* module = Procgen::create();
    return ownProcgenObject(module->ownership_->outputs, std::make_unique<OutputSpec>());
}

eve::script::Borrowed<OutputSpec> Procgen::resolveOutput(ProcgenOutputHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return module ? module->ownership_->outputs.resolve(reference) : eve::script::Borrowed<OutputSpec>();
}

eve::Result<void> Procgen::releaseOutput(ProcgenOutputHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                                           "output");
    return module->ownership_->outputs.erase(reference);
}

bool Procgen::isOutputStale(ProcgenOutputHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->outputs.isStale(reference);
}

eve::Result<ProcgenGridHandleRef> Procgen::newGridHandle(int width, int height) {
    if (width <= 0 || height <= 0)
        return procgenBindingFailure<ProcgenGridHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                           "procedural grid dimensions must be positive", "grid");
    auto grid = std::make_unique<Grid2D>();
    grid->resize(width, height);
    Procgen* module = Procgen::create();
    return module->grids_.emplace(std::move(grid));
}

eve::script::Borrowed<Grid2D> Procgen::resolve(ProcgenGridHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module) return {};
    return module->grids_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenGridHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                                           "grid");
    return module->grids_.erase(reference);
}

bool Procgen::isStale(ProcgenGridHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return !module || module->grids_.isStale(reference);
}

eve::Result<ProcgenPointSetHandleRef> Procgen::newPointSetHandle() {
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>());
}

eve::script::Borrowed<PointSet> Procgen::resolvePointSet(ProcgenPointSetHandleRef reference) noexcept {
    return ownership_->points.resolve(reference);
}

eve::Result<void> Procgen::releasePointSet(ProcgenPointSetHandleRef reference) {
    return ownership_->points.erase(reference);
}

bool Procgen::isPointSetStale(ProcgenPointSetHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->points.isStale(reference);
}

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleGridHandle(int width, int depth, float spacing, uint32_t seed,
                                                                float jitter) {
    if (width <= 0 || depth <= 0 || spacing <= 0.f)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument, "sampleGrid requires positive dimensions and spacing", "pointSet");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(sampleGridPoints(width, depth, spacing, seed, jitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterHeightHandle(ProcgenPointSetHandleRef input, float minHeight,
                                                                  float maxHeight) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "filterHeight input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointHeight(*view, minHeight, maxHeight)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterDensityHandle(ProcgenPointSetHandleRef input, float minDensity,
                                                                   float maxDensity) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "filterDensity input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointDensity(*view, minDensity, maxDensity)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterBoxHandle(ProcgenPointSetHandleRef input, float minX, float minY,
                                                               float minZ, float maxX, float maxY, float maxZ,
                                                               bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "filterBox input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(
                                                    filterPointBox(*view, minX, minY, minZ, maxX, maxY, maxZ, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSlopeHandle(ProcgenPointSetHandleRef input, float minDegrees,
                                                                 float maxDegrees) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "filterSlope input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointSlope(*view, minDegrees, maxDegrees)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterPolygonHandle(ProcgenPointSetHandleRef input,
                                                                   ProcgenPointSetHandleRef polygon, bool invert) {
    auto source = resolvePointSet(input);
    auto shape  = resolvePointSet(polygon);
    if (!source.isBound() || !shape.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "filterPolygon input handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointsByPolygon(*source, *shape, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSplineDistanceHandle(ProcgenPointSetHandleRef input,
                                                                          ProcgenPointSetHandleRef controlPoints,
                                                                          float minDistance, float maxDistance) {
    auto source  = resolvePointSet(input);
    auto control = resolvePointSet(controlPoints);
    if (!source.isBound() || !control.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "filterSplineDistance input handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(filterPointsBySplineDistance(
                                                    *source, *control, minDistance, maxDistance)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::excludeRadiusHandle(ProcgenPointSetHandleRef input, float x, float z,
                                                                   float radius) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "excludeRadius input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(excludePointRadius(*view, x, z, radius)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::jitterPointsHandle(ProcgenPointSetHandleRef input, uint32_t seed,
                                                                  float amountX, float amountZ) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "jitterPoints input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(jitterPointPositions(*view, seed, amountX, amountZ)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::selfPruneHandle(ProcgenPointSetHandleRef input, float radius) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "selfPrune input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(selfPrunePoints(*view, radius)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::projectToHeightmapHandle(ProcgenPointSetHandleRef  input,
                                                                        ProcgenHeightmapHandleRef heightmap,
                                                                        float originX, float originZ, float cellSize,
                                                                        float heightScale) {
    auto points = resolvePointSet(input);
    auto map    = resolveHeightmap(heightmap);
    if (!points.isBound() || !map.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "projectToHeightmap input handle is stale", "input");
    if (cellSize <= 0.f)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument, "projectToHeightmap cellSize must be positive", "cellSize");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(projectPointsToHeightmap(
                                                    *points, *map, originX, originZ, cellSize, heightScale)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleSplineHandle(ProcgenPointSetHandleRef controlPoints, float spacing,
                                                                  uint32_t seed, float lateralJitter) {
    auto control = resolvePointSet(controlPoints);
    if (!control.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "sampleSpline control point-set handle is stale", "controlPoints");
    if (spacing <= 0.f)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                               "sampleSpline spacing must be positive", "spacing");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(samplePolylinePoints(*control, spacing, seed, lateralJitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::mergePointsHandle(ProcgenPointSetHandleRef first,
                                                                 ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "mergePoints input handle is stale", "points");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(mergePointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::unionPointsHandle(ProcgenPointSetHandleRef first,
                                                                 ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "unionPoints requires live point sets", "points");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(unionPointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::intersectPointsHandle(ProcgenPointSetHandleRef first,
                                                                     ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "intersectPoints requires live point sets", "points");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(intersectPointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::differencePointsHandle(ProcgenPointSetHandleRef first,
                                                                      ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "differencePoints requires live point sets", "points");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(differencePointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::transformPointsHandle(ProcgenPointSetHandleRef input, float translateX,
                                                                     float translateY, float translateZ,
                                                                     float yawDegrees, float scaleX, float scaleY,
                                                                     float scaleZ) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "transformPoints input handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(transformPointSet(*view, translateX, translateY, translateZ,
                                                                         yawDegrees, scaleX, scaleY, scaleZ)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::transformPoints3DHandle(ProcgenPointSetHandleRef input, float translateX,
                                                                       float translateY, float translateZ,
                                                                       float pitchDegrees, float yawDegrees,
                                                                       float rollDegrees, float scaleX, float scaleY,
    float scaleZ) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "transformPoints3D input handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(transformPointSet3D(
                                                    *view, translateX, translateY, translateZ, pitchDegrees, yawDegrees,
                                                    rollDegrees, scaleX, scaleY, scaleZ)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::copyPointsHandle(ProcgenPointSetHandleRef source,
                                                                ProcgenPointSetHandleRef targets,
    bool inheritTargetAttributes) {
    auto sourceView = resolvePointSet(source);
    auto targetView = resolvePointSet(targets);
    if (!sourceView.isBound() || !targetView.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "copyPoints requires live point sets", "points");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(copyPointsToTargets(
                                                    *sourceView, *targetView, inheritTargetAttributes)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::remapDensityHandle(ProcgenPointSetHandleRef input, float inputMin,
                                                                  float inputMax, float outputMin, float outputMax,
                                                                  bool clampOutput) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "remapDensity point-set handle is stale", "points");
    if (!std::isfinite(inputMin) || !std::isfinite(inputMax) || inputMin == inputMax || !std::isfinite(outputMin) ||
        !std::isfinite(outputMax))
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument, "remapDensity requires finite non-zero input range", "inputRange");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(remapPointDensity(
                                                    *view, inputMin, inputMax, outputMin, outputMax, clampOutput)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::mathFloatAttributeHandle(ProcgenPointSetHandleRef input,
                                                                        const std::string&       attribute,
                                                                        const std::string&       outputAttribute,
                                                                        const std::string& operation, float operand,
    float defaultValue) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "mathFloatAttribute point-set handle is stale", "points");
    if (attribute.empty() || outputAttribute.empty() ||
        (operation != "add" && operation != "subtract" && operation != "multiply" && operation != "divide" &&
         operation != "min" && operation != "max") ||
        (operation == "divide" && operand == 0.f))
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument,
            "mathFloatAttribute requires a supported operation and valid attributes", "operation");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(mathPointFloatAttribute(*view, attribute, outputAttribute,
                                                                               operation, operand, defaultValue)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterFloatAttributeHandle(ProcgenPointSetHandleRef input,
                                                                          const std::string& name, float minValue,
                                                                          float maxValue, bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound() || name.empty())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            name.empty() ? eve::DiagnosticCode::InvalidArgument : eve::DiagnosticCode::StaleHandle,
            "filterFloatAttribute requires a live input and attribute name", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(filterPointFloatAttribute(
                                                    *view, name, minValue, maxValue, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterStringAttributeHandle(ProcgenPointSetHandleRef input,
                                                                           const std::string&       name,
                                                                           const std::string& value, bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound() || name.empty())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            name.empty() ? eve::DiagnosticCode::InvalidArgument : eve::DiagnosticCode::StaleHandle,
            "filterStringAttribute requires a live input and attribute name", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointStringAttribute(*view, name, value, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::densityCullHandle(ProcgenPointSetHandleRef input, uint32_t seed,
                                                                 float multiplier) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "densityCull input handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(densityCullPoints(*view, seed, multiplier)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::projectToWorldHandle(ProcgenPointSetHandleRef input, float maxY,
                                                                    float minY, std::uint64_t maskBits,
    bool keepUnmatched) {
    auto points = resolvePointSet(input);
    if (!points.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "projectToWorld point-set handle is stale", "points");
    if (!std::isfinite(maxY) || !std::isfinite(minY) || maxY < minY)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                               "projectToWorld requires finite maxY >= minY", "maxY");
    auto* query = eve::cap::query<eve::IProcgenWorldQuery>();
    if (!query)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::Unsupported, "projectToWorld requires an IProcgenWorldQuery provider", "worldQuery");

    PointSet output;
    output.reserve(points->points().size());
    for (size_t sourceIndex = 0; sourceIndex < points->points().size(); ++sourceIndex) {
        const auto& source      = points->points()[sourceIndex];
        auto queryResult = query->projectDown(source.x, source.z, maxY, minY, maskBits);
        if (!queryResult.ok())
            return procgenBindingFailure<ProcgenPointSetHandleRef>(
                eve::DiagnosticCode::Failed, "world-query provider failed to execute projection", "worldQuery");
        const auto& hit = queryResult.value();
        if (!hit.hit) {
            if (keepUnmatched)
                std::move(output.appendPointFrom(*points, sourceIndex)).expect("projectToWorld attribute schema");
            continue;
        }
        ProcgenPoint projected = source;
        projected.x = hit.x;
        projected.y = hit.y;
        projected.z = hit.z;
        projected.normalX = hit.normalX;
        projected.normalY = hit.normalY;
        projected.normalZ = hit.normalZ;
        const int outputIndex =
            std::move(output.appendPointFrom(*points, sourceIndex)).expect("projectToWorld attribute schema");
        output.mutablePoint(size_t(outputIndex)) = std::move(projected);
        output.trySetIntAttribute(outputIndex, "worldObjectId", hit.objectId).expect("projectToWorld metadata schema");
    }
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(std::move(output)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::poissonDiskHandle(int width, int depth, float radius, uint32_t seed,
                                                                 int maxPoints) {
    if (width < 0 || depth < 0 || radius <= 0.f || maxPoints < 0)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument,
            "poissonDisk requires non-negative dimensions/count and a positive radius");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(poissonDiskPoints(width, depth, radius, seed, maxPoints)));
}

static std::string sceneInstanceId(const PointSet& points, size_t pointIndex,
                                   std::unordered_map<uint32_t, size_t>& seedOccurrences) {
    const auto& point      = points.points()[pointIndex];
    const auto  explicitId = points.attributes().getString(pointIndex, "instanceId");
    if (explicitId && !explicitId->empty()) return std::string(*explicitId);
    if (point.id != 0) return "pcg-id-" + std::to_string(point.id);
    return "pcg-" + std::to_string(point.seed) + "-" + std::to_string(seedOccurrences[point.seed]++);
}

static eve::ProcgenInstanceDesc sceneInstanceDesc(const PointSet& points, size_t pointIndex,
                                                  const std::string& assetAttribute, const std::string& defaultAsset,
                                                  std::unordered_map<uint32_t, size_t>& seedOccurrences) {
    const auto&              point = points.points()[pointIndex];
    eve::ProcgenInstanceDesc instance;
    instance.sourcePointId = point.id;
    instance.id            = sceneInstanceId(points, pointIndex, seedOccurrences);
    instance.asset         = defaultAsset;
    if (!assetAttribute.empty()) {
        const auto found = points.attributes().getString(pointIndex, assetAttribute);
        if (found) instance.asset = std::string(*found);
    }
    instance.x      = point.x;
    instance.y      = point.y;
    instance.z      = point.z;
    instance.yaw    = point.yaw;
    instance.scaleX = point.scaleX;
    instance.scaleY = point.scaleY;
    instance.scaleZ = point.scaleZ;
    instance.seed   = point.seed;
    return instance;
}

static eve::Result<std::vector<eve::ProcgenInstanceDesc>> sceneInstanceDescs(const PointSet&    points,
                                                                             const std::string& assetAttribute,
                                                                             const std::string& defaultAsset,
                                                                             bool               requireStablePointIds) {
    std::vector<eve::ProcgenInstanceDesc> instances;
    instances.reserve(points.points().size());
    std::unordered_map<uint32_t, size_t> seedOccurrences;
    std::unordered_set<std::string>      instanceIds;
    std::unordered_set<uint64_t>         pointIds;
    for (size_t pointIndex = 0; pointIndex < points.points().size(); ++pointIndex) {
        auto instance = sceneInstanceDesc(points, pointIndex, assetAttribute, defaultAsset, seedOccurrences);
        if (!instanceIds.insert(instance.id).second)
            return eve::Result<std::vector<eve::ProcgenInstanceDesc>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                       "procedural Scene snapshot contains a duplicate instance id", "instanceId"));
        if (requireStablePointIds && (instance.sourcePointId == 0 || !pointIds.insert(instance.sourcePointId).second))
            return eve::Result<std::vector<eve::ProcgenInstanceDesc>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural Scene snapshot requires unique non-zero source PointIds",
                "pointId"));
        instances.push_back(std::move(instance));
    }
    return eve::Result<std::vector<eve::ProcgenInstanceDesc>>::success(std::move(instances));
}

eve::Result<void> Procgen::publishInstances(const std::string& batchId, ProcgenPointSetHandleRef points,
                                            const std::string& assetAttribute, const std::string& defaultAsset) {
    const auto view = resolvePointSet(points);
    if (batchId.empty() || !view.isBound())
        return procgenBindingFailure<void>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "publishInstances requires a batch id and a live point-set handle");
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, "publishInstances scene sink is unavailable");

    auto instances = sceneInstanceDescs(*view, assetAttribute, defaultAsset, false);
    if (!instances.ok()) return eve::Result<void>::failure(instances.status());
    if (!sink->applyBatch(batchId, instances.value()))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, "publishInstances scene sink rejected batch");
    return eve::Result<void>::success();
}

eve::Result<void> Procgen::removeInstances(const std::string& batchId) {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (batchId.empty())
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument, "removeInstances requires a batch id");
    if (!sink)
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, "removeInstances scene sink is unavailable");
    if (!sink->removeBatch(batchId))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, "removeInstances scene sink rejected batch");
    return eve::Result<void>::success();
}

eve::Result<void> Procgen::publishCellInstances(const std::string& prefix, const ProcgenCellRequest& request,
                                                ProcgenPointSetHandleRef points, const std::string& assetAttribute,
                                                const std::string& defaultAsset) {
    if (prefix.empty())
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "publishCellInstances requires a prefix");
    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return publishInstances(batchId, points, assetAttribute, defaultAsset);
}

eve::Result<uint64_t> Procgen::publishCellSnapshot(const std::string& prefix, const ProcgenCellRequest& request,
                                                   ProcgenPointSetHandleRef points, uint64_t targetRevision,
                                                   const std::string& assetAttribute, const std::string& defaultAsset) {
    const auto view = resolvePointSet(points);
    if (prefix.empty() || targetRevision == 0 || !view.isBound())
        return procgenBindingFailure<uint64_t>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "publishCellSnapshot requires a prefix, live point set, and non-zero target revision",
            "publishCellSnapshot");
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Failed,
                                               "publishCellSnapshot scene sink is unavailable");
    auto instances = sceneInstanceDescs(*view, assetAttribute, defaultAsset, true);
    if (!instances.ok()) return eve::Result<uint64_t>::failure(instances.status());
    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return sink->replaceBatch(batchId, targetRevision, instances.value());
}

eve::Result<uint64_t> Procgen::publishCellInstanceDelta(const std::string& prefix, const ProcgenCellRequest& request,
                                                        const PointDelta& delta, uint64_t targetRevision,
                                                        const std::string& assetAttribute,
                                                        const std::string& defaultAsset) {
    if (prefix.empty())
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                               "publishCellInstanceDelta requires a prefix", "prefix");
    if (targetRevision < 2)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                               "publishCellInstanceDelta requires a target revision greater than one",
                                               "targetRevision");
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Failed,
                                               "publishCellInstanceDelta scene sink is unavailable");

    eve::ProcgenInstanceDelta sceneDelta;
    sceneDelta.baseRevision     = targetRevision - 1;
    sceneDelta.targetRevision   = targetRevision;
    sceneDelta.removedPointIds  = delta.removed;
    sceneDelta.targetPointOrder = delta.targetOrder;
    std::unordered_map<uint32_t, size_t> seedOccurrences;
    sceneDelta.added.reserve(delta.added.points().size());
    for (size_t pointIndex = 0; pointIndex < delta.added.points().size(); ++pointIndex)
        sceneDelta.added.push_back(
            sceneInstanceDesc(delta.added, pointIndex, assetAttribute, defaultAsset, seedOccurrences));
    seedOccurrences.clear();
    sceneDelta.updated.reserve(delta.updated.points().size());
    for (size_t pointIndex = 0; pointIndex < delta.updated.points().size(); ++pointIndex)
        sceneDelta.updated.push_back(
            sceneInstanceDesc(delta.updated, pointIndex, assetAttribute, defaultAsset, seedOccurrences));

    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return sink->applyDelta(batchId, sceneDelta);
}

eve::Result<uint64_t> Procgen::synchronizeCellInstances(const std::string& prefix, const RuntimeGeneration& runtime,
                                                        const ProcgenCellRequest& request,
                                                        const std::string&        assetAttribute,
                                                        const std::string&        defaultAsset) {
    if (prefix.empty())
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                               "synchronizeCellInstances requires a prefix", "prefix");
    const int                 level          = request.getLevel();
    const int                 x              = request.getX();
    const int                 z              = request.getZ();
    const uint64_t            targetRevision = runtime.getCellRevision(level, x, z);
    std::unique_ptr<PointSet> snapshot(runtime.getCellOutput(level, x, z));
    if (!snapshot || targetRevision == 0)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::NotFound,
                                               "synchronizeCellInstances requires an active runtime cell", "cell");

    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Failed,
                                               "synchronizeCellInstances scene sink is unavailable");
    const std::string batchId =
        prefix + "/L" + std::to_string(level) + "/" + std::to_string(x) + "/" + std::to_string(z);
    const uint64_t sceneRevision = sink->batchRevision(batchId);
    if (sceneRevision == targetRevision) return eve::Result<uint64_t>::success(targetRevision);
    if (sceneRevision > targetRevision)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Conflict,
                                               "Scene cell revision is ahead of RuntimeGeneration", "revision");

    if (sceneRevision + 1 == targetRevision) {
        std::unique_ptr<PointDelta> delta(runtime.getCellDelta(level, x, z));
        if (delta)
            return publishCellInstanceDelta(prefix, request, *delta, targetRevision, assetAttribute, defaultAsset);
    }

    auto instances = sceneInstanceDescs(*snapshot, assetAttribute, defaultAsset, true);
    if (!instances.ok()) return eve::Result<uint64_t>::failure(instances.status());
    return sink->replaceBatch(batchId, targetRevision, instances.value());
}

eve::Result<uint64_t> Procgen::synchronizeCellInstancesAtomic(const std::string&                            prefix,
                                                              const std::vector<const RuntimeGeneration*>&  runtimes,
                                                              const std::vector<const ProcgenCellRequest*>& requests,
                                                              const std::string& assetAttribute,
                                                              const std::string& defaultAsset) {
    if (prefix.empty() || runtimes.empty() || runtimes.size() != requests.size())
        return procgenBindingFailure<uint64_t>(
            eve::DiagnosticCode::InvalidArgument,
            "synchronizeCellInstancesAtomic requires a prefix and equally sized non-empty runtime/request lists",
            "cells");
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Failed,
                                               "synchronizeCellInstancesAtomic scene sink is unavailable");

    std::vector<eve::ProcgenBatchSnapshot> snapshots;
    snapshots.reserve(requests.size());
    std::unordered_set<std::string> batchIds;
    for (size_t index = 0; index < requests.size(); ++index) {
        const auto* runtime = runtimes[index];
        const auto* request = requests[index];
        if (!runtime || !request)
            return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                   "synchronizeCellInstancesAtomic contains a null cell", "cells");
        const int                 level          = request->getLevel();
        const int                 x              = request->getX();
        const int                 z              = request->getZ();
        const uint64_t            targetRevision = runtime->getCellRevision(level, x, z);
        std::unique_ptr<PointSet> output(runtime->getCellOutput(level, x, z));
        if (!output || targetRevision == 0)
            return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::NotFound,
                                                   "synchronizeCellInstancesAtomic contains an inactive cell", "cells");
        const std::string batchId =
            prefix + "/L" + std::to_string(level) + "/" + std::to_string(x) + "/" + std::to_string(z);
        if (!batchIds.insert(batchId).second)
            return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Conflict,
                                                   "synchronizeCellInstancesAtomic repeats a cell", "cells");
        const uint64_t sceneRevision = sink->batchRevision(batchId);
        if (sceneRevision > targetRevision)
            return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Conflict,
                                                   "Scene cell revision is ahead of RuntimeGeneration", "revision");
        if (sceneRevision == targetRevision) continue;
        auto instances = sceneInstanceDescs(*output, assetAttribute, defaultAsset, true);
        if (!instances) return eve::Result<uint64_t>::failure(instances.status());
        snapshots.push_back({batchId, targetRevision, std::move(instances).takeValue()});
    }
    if (snapshots.empty()) return eve::Result<uint64_t>::success(0);
    return sink->replaceBatches(snapshots);
}

eve::Result<void> Procgen::removeCellInstances(const std::string& prefix, const ProcgenCellRequest& request) {
    if (prefix.empty())
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "removeCellInstances requires a prefix");
    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return removeInstances(batchId);
}

eve::Result<uint64_t> Procgen::removeCellInstancesAtomic(const std::string&                            prefix,
                                                         const std::vector<const ProcgenCellRequest*>& requests) {
    if (prefix.empty() || requests.empty())
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                               "removeCellInstancesAtomic requires a prefix and cleanup requests",
                                               "requests");
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Failed,
                                               "removeCellInstancesAtomic scene sink is unavailable");
    std::vector<std::string> batchIds;
    batchIds.reserve(requests.size());
    for (const auto* request : requests) {
        if (!request)
            return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                   "removeCellInstancesAtomic contains a null request", "requests");
        batchIds.push_back(prefix + "/L" + std::to_string(request->getLevel()) + "/" + std::to_string(request->getX()) +
                           "/" + std::to_string(request->getZ()));
    }
    return sink->removeBatches(batchIds);
}

eve::Result<uint64_t> Procgen::completeCellCleanupAtomic(const std::string&                            prefix,
                                                         const std::vector<RuntimeGeneration*>&        runtimes,
                                                         const std::vector<const ProcgenCellRequest*>& requests) {
    if (prefix.empty() || requests.empty() || runtimes.size() != requests.size())
        return procgenBindingFailure<uint64_t>(
            eve::DiagnosticCode::InvalidArgument,
            "completeCellCleanupAtomic requires a prefix and equally sized runtime/request arrays", "requests");
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::Failed,
                                               "completeCellCleanupAtomic scene sink is unavailable");

    struct RuntimeCleanupGroup {
        RuntimeGeneration*                     runtime = nullptr;
        std::vector<const ProcgenCellRequest*> requests;
    };
    std::vector<RuntimeCleanupGroup> groups;
    std::vector<std::string>         batchIds;
    groups.reserve(runtimes.size());
    batchIds.reserve(requests.size());
    for (size_t index = 0; index < requests.size(); ++index) {
        auto* runtime = runtimes[index];
        auto* request = requests[index];
        if (!runtime || !request)
            return procgenBindingFailure<uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                   "completeCellCleanupAtomic contains a null runtime or request",
                                                   "requests");
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [runtime](const auto& candidate) { return candidate.runtime == runtime; });
        if (group == groups.end()) {
            groups.push_back({runtime, {}});
            group = std::prev(groups.end());
        }
        group->requests.push_back(request);
        batchIds.push_back(prefix + "/L" + std::to_string(request->getLevel()) + "/" + std::to_string(request->getX()) +
                           "/" + std::to_string(request->getZ()));
    }
    for (const auto& group : groups) {
        auto validated = group.runtime->validateCleanups(group.requests);
        if (!validated.ok()) return eve::Result<uint64_t>::failure(validated.status());
    }
    return sink->removeBatchesCoordinated(batchIds, [&groups] {
        for (auto& group : groups) group.runtime->eraseValidatedCleanups(group.requests);
        return eve::Result<void>::success();
    });
}

int Procgen::getPublishedInstanceCount(const std::string& batchId) const {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    return sink && !batchId.empty() ? sink->instanceCount(batchId) : 0;
}

int Procgen::getPublishedCreatedCount(const std::string& batchId) const {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    return sink && !batchId.empty() ? sink->lastCreatedCount(batchId) : 0;
}

int Procgen::getPublishedReusedCount(const std::string& batchId) const {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    return sink && !batchId.empty() ? sink->lastReusedCount(batchId) : 0;
}

int Procgen::getPublishedRemovedCount(const std::string& batchId) const {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    return sink && !batchId.empty() ? sink->lastRemovedCount(batchId) : 0;
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::pointDataHandle(ProcgenPointSetHandleRef points) {
    auto view = resolvePointSet(points);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                                  "pointData point-set handle is stale", "points");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::fromPoints(*view)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::boxVolumeHandle(float minX, float minY, float minZ, float maxX,
                                                                  float maxY, float maxZ) {
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::box(minX, minY, minZ, maxX, maxY, maxZ)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::sphereVolumeHandle(float x, float y, float z, float radius) {
    if (radius <= 0.f)
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                  "sphereVolume radius must be positive", "radius");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::sphere(x, y, z, radius)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::polygonVolumeHandle(ProcgenPointSetHandleRef controlPoints,
                                                                      float minY, float maxY) {
    auto points = resolvePointSet(controlPoints);
    if (!points.isBound() || points->getCount() < 3 || !std::isfinite(minY) || !std::isfinite(maxY))
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            eve::DiagnosticCode::InvalidArgument,
            "polygonVolume requires a live set with at least three points and finite heights", "controlPoints");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::polygon(*points, minY, maxY)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::splineDataHandle(ProcgenPointSetHandleRef controlPoints,
                                                                   float                    radius) {
    auto view = resolvePointSet(controlPoints);
    if (!view.isBound() || view->getCount() < 2 || radius < 0.f)
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "splineData requires a live set with at least two points and non-negative radius", "controlPoints");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::spline(*view, radius)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::heightfieldDataHandle(ProcgenHeightmapHandleRef heightmap,
                                                                        float originX, float originZ, float cellSize,
                                                                        float heightScale) {
    auto view = resolveHeightmap(heightmap);
    if (!view.isBound() || view->getWidth() <= 0 || view->getHeight() <= 0 || cellSize <= 0.f)
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "heightfieldData requires a live non-empty heightmap and positive cell size", "heightmap");
    return spatialData_.emplace(
        std::make_unique<SpatialData>(SpatialData::heightfield(*view, originX, originZ, cellSize, heightScale)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::textureMaskDataHandle(ProcgenHeightmapHandleRef values, float originX,
                                                                        float originZ, float cellSize, float minValue,
                                                                        float maxValue, float minY, float maxY) {
    auto view = resolveHeightmap(values);
    if (!view.isBound() || view->getWidth() <= 0 || view->getHeight() <= 0 || cellSize <= 0.f)
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            eve::DiagnosticCode::InvalidArgument,
            "textureMaskData requires a live non-empty scalar map and positive cell size", "values");
    return spatialData_.emplace(std::make_unique<SpatialData>(
        SpatialData::textureMask(*view, originX, originZ, cellSize, minValue, maxValue, minY, maxY)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::meshSurfaceDataHandle(ProcgenMeshBuildHandleRef mesh,
                                                                        float                     tolerance) {
    auto view = resolveMeshBuild(mesh);
    if (!view.isBound() || view->getVertexCount() < 3 || view->getIndexCount() < 3 || tolerance < 0.f)
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            eve::DiagnosticCode::InvalidArgument,
            "meshSurfaceData requires a live triangle mesh and non-negative tolerance", "mesh");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::meshSurface(*view, tolerance)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::unionSpatialHandle(ProcgenSpatialDataHandleRef left,
                                                                     ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left);
    auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                                  "unionSpatial input handle is stale", "spatial");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::unite(*a, *b)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::intersectSpatialHandle(ProcgenSpatialDataHandleRef left,
                                                                         ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left);
    auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                                  "intersectSpatial input handle is stale", "spatial");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::intersect(*a, *b)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::differenceSpatialHandle(ProcgenSpatialDataHandleRef left,
                                                                          ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left);
    auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                                  "differenceSpatial input handle is stale", "spatial");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::subtract(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleSpatialHandle(ProcgenSpatialDataHandleRef spatial, float spacing,
                                                                   uint32_t seed, float jitter) {
    auto view = resolveSpatialData(spatial);
    if (!view.isBound() || spacing <= 0.f)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "sampleSpatial requires live spatial data and positive spacing", "spatial");
    return ownership_->points.emplace(std::make_unique<PointSet>(view->sample(spacing, seed, jitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSpatialHandle(ProcgenPointSetHandleRef    input,
                                                                   ProcgenSpatialDataHandleRef spatial, bool invert) {
    auto points = resolvePointSet(input);
    auto domain = resolveSpatialData(spatial);
    if (!points.isBound() || !domain.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "filterSpatial input handle is stale", "input");
    return ownership_->points.emplace(std::make_unique<PointSet>(domain->filter(*points, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::projectToSpatialHandle(ProcgenPointSetHandleRef    input,
                                                                      ProcgenSpatialDataHandleRef spatial) {
    auto points = resolvePointSet(input);
    auto domain = resolveSpatialData(spatial);
    if (!points.isBound() || !domain.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "projectToSpatial input handle is stale", "input");
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
eve::Result<void> Procgen::release(ProcgenBiomeRulesHandleRef reference) { return biomeRules_.erase(reference); }
eve::Result<void> Procgen::release(ProcgenShapeGrammarHandleRef reference) { return shapeGrammars_.erase(reference); }
eve::Result<void> Procgen::release(ProcgenLSystemHandleRef reference) { return lsystems_.erase(reference); }

bool Procgen::isStale(ProcgenRuntimeGenerationHandleRef reference) const noexcept {
    return runtimeGenerations_.isStale(reference);
}
bool Procgen::isStale(ProcgenPointGraphHandleRef reference) const noexcept { return pointGraphs_.isStale(reference); }
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
        return procgenBindingFailure<ProcgenContextHandleRef>(eve::DiagnosticCode::InvalidArgument, module->lastError_,
                                                              "context");
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
        return procgenBindingFailure<ProcgenContextHandleRef>(eve::DiagnosticCode::InvalidArgument, module->lastError_,
                                                              "context");
    }
    if (buildKey.empty()) {
        module->lastError_ = "beginCachedSystem: build key is empty";
        return procgenBindingFailure<ProcgenContextHandleRef>(eve::DiagnosticCode::InvalidArgument, module->lastError_,
                                                              "buildKey");
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
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module) return {};
    return module->contexts_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenContextHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                                           "context");
    return module->contexts_.erase(reference);
}

bool Procgen::isStale(ProcgenContextHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return !module || module->contexts_.isStale(reference);
}

eve::Result<void> Procgen::commitSystem(ProcgenContextHandleRef reference) {
    auto view = Procgen::resolve(reference);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "commitSystem context handle is stale",
                                           "context");
    ProcgenContext* context = view.get();
    if (!context->isActive())
        return procgenBindingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                           "commitSystem: transaction is closed", "context");
    if (context->hasFailed()) {
        const std::string error = context->getError();
        context->close();
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, "commitSystem: " + error, "context");
    }
    if (!context->openTraces_.empty()) {
        const std::string trace = context->openTraces_.back().name;
        context->close();
        return procgenBindingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                           "commitSystem: unfinished trace '" + trace + "'", "context");
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
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "abortSystem context handle is stale",
                                           "context");
    view->abort();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::removeSystem(const std::string& name) {
    previousSystems_.erase(name);
    if (systems_.erase(name) == 0)
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound, "procgen system was not committed", "system");
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
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::NotFound,
                                                               "procgen system is not committed", "system");
    const auto output = system->second.outputs.find(outputName);
    if (output == system->second.outputs.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::NotFound,
                                                               "procgen system output was not found", "output");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(output->second));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::getSystemDebugStageHandle(const std::string& name,
                                                                         const std::string& stageName) const {
    const auto system = systems_.find(name);
    if (system == systems_.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::NotFound,
                                                               "procgen system is not committed", "system");
    const auto stage = system->second.debugStages.find(stageName);
    if (stage == system->second.debugStages.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::NotFound,
                                                               "procgen debug stage was not found", "stage");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(stage->second));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::getPreviousSystemDebugStageHandle(const std::string& name,
                                                                                 const std::string& stageName) const {
    const auto system = previousSystems_.find(name);
    if (system == previousSystems_.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::NotFound,
                                                               "procgen system has no previous revision", "system");
    const auto stage = system->second.debugStages.find(stageName);
    if (stage == system->second.debugStages.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::NotFound,
                                                               "previous procgen debug stage was not found", "stage");
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

eve::Result<ProcgenGridHandleRef> Procgen::generateHandle(const std::string&     algorithmId,
                                                          ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenGridHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                           "generate parameters handle is stale", "params");
    auto grid = std::make_unique<Grid2D>();
    if (!runGenerate(algorithmId, *input, *grid))
        return procgenBindingFailure<ProcgenGridHandleRef>(
            eve::DiagnosticCode::Failed, lastError_.empty() ? "generate failed" : lastError_, "algorithm");
    return ownProcgenObject(grids_, std::move(grid));
}

eve::Result<void> Procgen::generateTo(const std::string& algorithmId, ProcgenParamsHandleRef params,
                                      ProcgenOutputHandleRef output) {
    auto paramsView = Procgen::resolve(params);
    auto outputView = resolveOutput(output);
    if (!paramsView.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "generateTo parameter handle is stale",
                                           "params");
    if (!outputView.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "generateTo output handle is stale",
                                           "output");
    Grid2D grid;
    if (!runGenerate(algorithmId, *paramsView, grid))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                           lastError_.empty() ? "generateTo failed" : lastError_, "algorithm");

    const std::string target = outputView->getTarget();
    if (target == "grid") {
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "generateTo: target 'grid' has no sink; use generateHandle()",
                                           "output.target");
    }
    if (target == "tilelayer") {
        auto* layer = outputView->getLayer();
        if (!layer)
            return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                               "generateTo: tilelayer target has no layer", "output.layer");
        if (!palettes_.applyToLayer(grid, outputView->getPalette(), layer, &lastError_))
            return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, lastError_, "output");
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    if (target == "json") {
        if (!writeGridJson(grid, outputView->getPath(), &lastError_))
            return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, lastError_, "output.path");
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                       "generateTo: unknown target '" + target + "' (use grid|tilelayer|json)",
                                       "output.target");
}

eve::Result<void> Procgen::applyToLayer(ProcgenGridHandleRef grid, const std::string& palette, map::TileLayer& layer) {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "applyToLayer grid handle is stale",
                                           "grid");
    if (!palettes_.applyToLayer(*view, palette, &layer, &lastError_))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, lastError_, "palette");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
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

eve::Result<RecipeDescriptor> Procgen::getAlgorithmSchema(const std::string& algorithmId) const {
    const RecipeDescriptor *schema = GeneratorRegistry::instance().descriptor(algorithmId);
    if (!schema)
        return procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::NotFound, "algorithm schema was not found",
                                                       "algorithm");
    return eve::Result<RecipeDescriptor>::success(*schema);
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

eve::Result<void> Procgen::applyAlgorithmDefaults(const std::string& algorithmId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "algorithm default parameters handle is stale", "params");
    if (!GeneratorRegistry::instance().applyDefaults(algorithmId, *view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound, "algorithm schema was not found",
                                           "algorithm");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::autotileGrid(ProcgenGridHandleRef grid) {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "autotileGrid grid handle is stale",
                                           "grid");
    if (!eve::procgen::autotileGridInPlace(*view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed, "autotileGrid failed", "grid");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

uint32_t Procgen::randomSeed() { return eve::procgen::randomSeedValue(); }

eve::Result<std::string> Procgen::gridToJson(ProcgenGridHandleRef grid) const {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return procgenBindingFailure<std::string>(eve::DiagnosticCode::StaleHandle, "gridToJson grid handle is stale",
                                                  "grid");
    return eve::Result<std::string>::success(eve::procgen::gridToJson(*view));
}

eve::Result<ProcgenImageHandleRef> Procgen::generateImageHandle(const std::string&     recipeId,
                                                                ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenImageHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                            "generateImage parameters handle is stale", "params");
    lastError_.clear();
    TextureRecipeRegistry::instance().registerBuiltins();
    auto image = TextureRecipeRegistry::instance().generate(recipeId, *input, lastError_);
    if (!image)
        return procgenBindingFailure<ProcgenImageHandleRef>(
            eve::DiagnosticCode::Failed, lastError_.empty() ? "generateImage failed" : lastError_, "recipe");
    return ownProcgenObject(ownership_->images, std::move(image));
}

eve::script::Borrowed<image::ImageData> Procgen::resolve(ProcgenImageHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return module ? module->ownership_->images.resolve(reference) : eve::script::Borrowed<image::ImageData>();
}

eve::Result<void> Procgen::release(ProcgenImageHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                                           "image");
    return module->ownership_->images.erase(reference);
}

bool Procgen::isStale(ProcgenImageHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return !module || module->ownership_->images.isStale(reference);
}

eve::Result<ProcgenNormalImageHandleRef> Procgen::generateNormalImageHandle(const std::string&     recipeId,
                                                                            ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenNormalImageHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generateNormalImage parameters handle is stale", "params");
    auto image = generateImageHandle(recipeId, params);
    if (!image)
        return procgenBindingFailure<ProcgenNormalImageHandleRef>(eve::DiagnosticCode::Failed,
                                                                  image.status().describe(), "recipe");
    const auto imageRef = std::move(image).takeValue();
    auto       albedo   = ownership_->images.resolve(imageRef);
    if (!albedo.isBound()) {
        ownership_->images.erase(imageRef).ignore("release unresolvable temporary albedo image");
        return procgenBindingFailure<ProcgenNormalImageHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                                  "generated albedo image handle is stale", "image");
    }
    const int w = albedo->getWidth();
    const int h = albedo->getHeight();
    auto*              px = static_cast<const uint8_t*>(albedo->getData());
    std::vector<float> height(size_t(w * h));
    for (int i = 0; i < w * h; ++i) {
        const size_t o = size_t(i) * 4u;
        height[size_t(i)] = (float(px[o]) * 0.299f + float(px[o + 1]) * 0.587f + float(px[o + 2]) * 0.114f) / 255.f;
    }
    const bool  seamless = input->getInt("seamless", 1) != 0;
    const float strength = input->getFloat("normalStrength", 4.f);
    auto        normal   = heightToNormalImage(height, w, h, strength, seamless);
    ownership_->images.erase(imageRef).ignore("release temporary albedo image");
    if (!normal)
        return procgenBindingFailure<ProcgenNormalImageHandleRef>(eve::DiagnosticCode::Failed,
                                                                  "generateNormalImage failed", "recipe");
    return ownProcgenObject(ownership_->normalImages, std::move(normal));
}

eve::script::Borrowed<image::ImageData> Procgen::resolve(ProcgenNormalImageHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return module ? module->ownership_->normalImages.resolve(reference) : eve::script::Borrowed<image::ImageData>();
}

eve::Result<void> Procgen::release(ProcgenNormalImageHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                                           "normalImage");
    return module->ownership_->normalImages.erase(reference);
}

bool Procgen::isStale(ProcgenNormalImageHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
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

bool Procgen::hasTextureRecipe(const std::string &recipeId) const {
    TextureRecipeRegistry::instance().registerBuiltins();
    return TextureRecipeRegistry::instance().has(recipeId);
}

eve::Result<RecipeDescriptor> Procgen::getTextureRecipeSchema(const std::string& recipeId) const {
    TextureRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor *schema = TextureRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::NotFound,
                                                       "texture recipe schema was not found", "recipe");
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyTextureRecipeDefaults(const std::string&     recipeId,
                                                      ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "texture default parameters handle is stale", "params");
    TextureRecipeRegistry::instance().registerBuiltins();
    if (!TextureRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound, "texture recipe schema was not found",
                                           "recipe");
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
        return procgenBindingFailure<float>(eve::DiagnosticCode::StaleHandle, "cloud field handle is stale", "field");
    return eve::Result<float>::success(view->coverageAt(x, z, time));
}

eve::Result<float> Procgen::cloudShadowFactor(ProcgenCloudShadowHandleRef shadow, float x, float z, float time) {
    auto view = resolveCloudShadow(shadow);
    if (!view.isBound())
        return procgenBindingFailure<float>(eve::DiagnosticCode::StaleHandle, "cloud shadow handle is stale", "shadow");
    return eve::Result<float>::success(view->shadowFactorAt(x, z, time));
}

eve::Result<void> Procgen::sampleCloud(ProcgenCloudFieldHandleRef field, std::span<float> out, int w, int h, float time,
                                       float x0, float z0, float extent) {
    auto view = resolveCloudField(field);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "cloud field handle is stale", "field");
    if (w <= 0 || h <= 0 || out.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "cloud output buffer is smaller than width*height", "out");
    view->sample(out.data(), w, h, time, x0, z0, extent);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::sampleCloudShadow(ProcgenCloudShadowHandleRef shadow, std::span<float> out, int w, int h,
                                             float time, float x0, float z0, float extent) {
    auto view = resolveCloudShadow(shadow);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "cloud shadow handle is stale", "shadow");
    if (w <= 0 || h <= 0 || out.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "cloud output buffer is smaller than width*height", "out");
    view->sampleCoverage(out.data(), w, h, time, x0, z0, extent);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenPbrMaterialHandleRef> Procgen::generatePbrMaterialHandle(const std::string&     recipeId,
                                                                            ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenPbrMaterialHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generatePbrMaterial parameters handle is stale", "params");
    lastError_.clear();
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    auto set = PbrRecipeRegistry::instance().generate(recipeId, *input, lastError_);
    if (!set)
        return procgenBindingFailure<ProcgenPbrMaterialHandleRef>(
            eve::DiagnosticCode::Failed, lastError_.empty() ? "generatePbrMaterial failed" : lastError_, "recipe");
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

bool Procgen::hasPbrRecipe(const std::string &recipeId) const {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    return PbrRecipeRegistry::instance().has(recipeId);
}

eve::Result<RecipeDescriptor> Procgen::getPbrRecipeSchema(const std::string& recipeId) const {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    const RecipeDescriptor *schema = PbrRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::NotFound, "PBR recipe schema was not found",
                                                       "recipe");
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyPbrRecipeDefaults(const std::string& recipeId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "PBR default parameters handle is stale",
                                           "params");
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    if (!PbrRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound, "PBR recipe schema was not found", "recipe");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenMeshBuildHandleRef> Procgen::buildMeshHandle(const std::string&     recipeId,
                                                                ProcgenParamsHandleRef params) {
    auto built = buildArtifact(recipeId, params, nextCompatibilityArtifactId());
    if (!built)
        return procgenBindingFailure<ProcgenMeshBuildHandleRef>(eve::DiagnosticCode::Failed, built.status().describe(),
                                                                "recipe");
    GeneratedArtifact artifact = std::move(built).takeValue();
    const MeshBuild*  source   = nullptr;
    if (artifact.type == ArtifactType::MeshData) {
        source = &std::get<MeshData>(artifact.payload);
    } else if (artifact.type == ArtifactType::Composite) {
        const ArtifactPart* part = std::get<CompositeArtifact>(artifact.payload).find("mesh");
        if (part && part->type == ArtifactType::MeshData) source = &std::get<MeshData>(part->payload);
    }
    if (!source)
        return procgenBindingFailure<ProcgenMeshBuildHandleRef>(eve::DiagnosticCode::Failed,
                                                                "generated artifact has no mesh payload", "recipe");
    auto              mesh = std::make_unique<MeshBuild>(*source);
    ArtifactPublisher publisher(artifactStore_);
    auto              published = publisher.publish(std::move(artifact), {});
    if (!published)
        return procgenBindingFailure<ProcgenMeshBuildHandleRef>(eve::DiagnosticCode::Failed,
                                                                published.status().describe(), "artifact");
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
        return procgenBindingFailure<GeneratedArtifact>(eve::DiagnosticCode::StaleHandle,
                                                        "buildArtifact parameters handle is stale", "params");
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
    graphics::Mesh* uploaded =
        gfx.newMeshFromArrays(mesh.positions().data(), mesh.normals().data(), mesh.uvs().data(), mesh.getVertexCount(),
                              mesh.indices().data(), mesh.getIndexCount());
    return eve::script::Borrowed<graphics::Mesh>(uploaded,
                                                 static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&gfx)));
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

eve::Result<RecipeDescriptor> Procgen::getMeshRecipeSchema(const std::string& recipeId) const {
    MeshRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor *schema = MeshRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::NotFound,
                                                       "mesh recipe schema was not found", "recipe");
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyMeshRecipeDefaults(const std::string& recipeId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "mesh default parameters handle is stale",
                                           "params");
    MeshRecipeRegistry::instance().registerBuiltins();
    if (!MeshRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound, "mesh recipe schema was not found", "recipe");
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
        return procgenBindingFailure<ProcgenHeightmapHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                "heightmap dimensions must be positive", "heightmap");
    return ownProcgenObject(ownership_->heightmaps, std::make_unique<Heightmap>(width, height));
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
        return procgenBindingFailure<ProcgenHeightmapHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generateHeightmap parameters handle is stale", "params");
    const TerrainSampler sampler = TerrainSampler::fromParams(*input);
    return ownProcgenObject(ownership_->heightmaps, std::make_unique<Heightmap>(Heightmap::generate(
                                                        sampler, input->getWidth(), input->getHeight())));
}

eve::Result<ProcgenGridHandleRef> Procgen::heightmapToGrid(ProcgenHeightmapHandleRef heightmap,
                                                           ProcgenParamsHandleRef    params) {
    auto map   = resolveHeightmap(heightmap);
    auto input = Procgen::resolve(params);
    if (!map.isBound())
        return procgenBindingFailure<ProcgenGridHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                           "heightmap handle is stale", "heightmap");
    if (!input.isBound())
        return procgenBindingFailure<ProcgenGridHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                           "heightmap parameters handle is stale", "params");
    auto               grid  = std::make_unique<Grid2D>();
    const TerrainBands bands = TerrainBands::fromParams(*input);
    if (!map->toGrid(*grid, bands))
        return procgenBindingFailure<ProcgenGridHandleRef>(eve::DiagnosticCode::Failed, "heightmap is empty",
                                                           "heightmap");
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
    settings.heightOffset = heightOffset;
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
            ++quantized[channel];
            remainder[channel] = -1.f;
            ++total;
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
    const HSQUIRRELVM vm  = table.getHandle();
    auto cls = table.addClass(name, Procgen::create, false);
    expose(cls);
    exposeBiomeRules(table);
    exposePointGraph(table);
    exposeShapeGrammar(table);

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
    pointDelta.addFunc("getBaseFingerprint",
                       [](PointDelta* value) { return std::to_string(value->baseFingerprint); });
    pointDelta.addFunc("getTargetFingerprint",
                       [](PointDelta* value) { return std::to_string(value->targetFingerprint); });

    auto cellRequest = table.addClass<ProcgenCellRequest>(
        "ProcgenCellRequest", std::function<ProcgenCellRequest*()>([]() -> ProcgenCellRequest* { return nullptr; }),
        true);
    cellRequest.addFunc("getLevel", &ProcgenCellRequest::getLevel);
    cellRequest.addFunc("getX", &ProcgenCellRequest::getX);
    cellRequest.addFunc("getZ", &ProcgenCellRequest::getZ);
    cellRequest.addFunc("getSeed", &ProcgenCellRequest::getSeed);
    cellRequest.addFunc("getTicket", &ProcgenCellRequest::getTicket);
    cellRequest.addFunc("getMinX", &ProcgenCellRequest::getMinX);
    cellRequest.addFunc("getMinZ", &ProcgenCellRequest::getMinZ);
    cellRequest.addFunc("getMaxX", &ProcgenCellRequest::getMaxX);
    cellRequest.addFunc("getMaxZ", &ProcgenCellRequest::getMaxZ);

    auto runtimeGeneration = table.addClass<RuntimeGeneration>(
        "ProcgenRuntimeGeneration", std::function<RuntimeGeneration*()>([]() -> RuntimeGeneration* { return nullptr; }),
        true);
    runtimeGeneration.addFunc("clear", &RuntimeGeneration::clear);
    runtimeGeneration.addFunc("addLevel", &RuntimeGeneration::addLevel);
    runtimeGeneration.addFunc("getLevelCount", &RuntimeGeneration::getLevelCount);
    runtimeGeneration.addFunc("getLevelCellSize", &RuntimeGeneration::getLevelCellSize);
    runtimeGeneration.addFunc("getLevelGenerationRadius", &RuntimeGeneration::getLevelGenerationRadius);
    runtimeGeneration.addFunc("getLevelCleanupRadius", &RuntimeGeneration::getLevelCleanupRadius);
    runtimeGeneration.addFunc("setDirectionWeight", &RuntimeGeneration::setDirectionWeight);
    runtimeGeneration.addFunc("getDirectionWeight", &RuntimeGeneration::getDirectionWeight);
    runtimeGeneration.addFunc("setMaxGenerating", &RuntimeGeneration::setMaxGenerating);
    runtimeGeneration.addFunc("getMaxGenerating", &RuntimeGeneration::getMaxGenerating);
    runtimeGeneration.addFunc("setMaxActiveCells", &RuntimeGeneration::setMaxActiveCells);
    runtimeGeneration.addFunc("getMaxActiveCells", &RuntimeGeneration::getMaxActiveCells);
    runtimeGeneration.addFunc("setMaxPointsPerCell", &RuntimeGeneration::setMaxPointsPerCell);
    runtimeGeneration.addFunc("getMaxPointsPerCell", &RuntimeGeneration::getMaxPointsPerCell);
    runtimeGeneration.addFunc("setMaxResidentPoints", &RuntimeGeneration::setMaxResidentPoints);
    runtimeGeneration.addFunc("getMaxResidentPoints", &RuntimeGeneration::getMaxResidentPoints);
    runtimeGeneration.addFunc("getResidentPointCount", &RuntimeGeneration::getResidentPointCount);
    runtimeGeneration.addFunc("getRejectedOutputCount", &RuntimeGeneration::getRejectedOutputCount);
    runtimeGeneration.addFunc("trimToResidentPoints", &RuntimeGeneration::trimToResidentPoints);
    runtimeGeneration.addFunc("setMaxGenerationRetries", &RuntimeGeneration::setMaxGenerationRetries);
    runtimeGeneration.addFunc("getMaxGenerationRetries", &RuntimeGeneration::getMaxGenerationRetries);
    runtimeGeneration.addFunc("setFrameTimeBudget", &RuntimeGeneration::setFrameTimeBudget);
    runtimeGeneration.addFunc("getFrameTimeBudget", &RuntimeGeneration::getFrameTimeBudget);
    runtimeGeneration.addFunc("beginFrame", &RuntimeGeneration::beginFrame);
    runtimeGeneration.addFunc("setRefreshWorkBudget", &RuntimeGeneration::setRefreshWorkBudget);
    runtimeGeneration.addFunc("getRefreshWorkBudget", &RuntimeGeneration::getRefreshWorkBudget);
    runtimeGeneration.addFunc("isRefreshPending", &RuntimeGeneration::isRefreshPending);
    runtimeGeneration.addFunc("getCommittedRefreshRevision", &RuntimeGeneration::getCommittedRefreshRevision);
    runtimeGeneration.addFunc(
        "continueGenerationRefresh", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* value) {
            return eve::script::projectResult(
                vm,
                value ? value->continueGenerationRefresh()
                      : procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                             "continueGenerationRefresh requires RuntimeGeneration",
                                                             "runtimeGeneration"),
                [](std::uint64_t processed) { return eve::Value(std::to_string(processed)); });
        });
    runtimeGeneration.addFunc("updateSource", &RuntimeGeneration::updateSource);
    runtimeGeneration.addFunc("setGenerationSource", &RuntimeGeneration::setGenerationSource);
    runtimeGeneration.addFunc("removeGenerationSource", &RuntimeGeneration::removeGenerationSource);
    runtimeGeneration.addFunc("clearGenerationSources", &RuntimeGeneration::clearGenerationSources);
    runtimeGeneration.addFunc("getGenerationSourceCount", &RuntimeGeneration::getGenerationSourceCount);
    runtimeGeneration.addFunc("getGenerationSourceId", &RuntimeGeneration::getGenerationSourceId);
    runtimeGeneration.addFunc("refreshGenerationSources", &RuntimeGeneration::refreshGenerationSources);
    runtimeGeneration.addFunc("setFrustumCulling", &RuntimeGeneration::setFrustumCulling);
    runtimeGeneration.addFunc("isFrustumCullingEnabled", &RuntimeGeneration::isFrustumCullingEnabled);
    runtimeGeneration.addFunc("getFrustumHalfAngle", &RuntimeGeneration::getFrustumHalfAngle);
    runtimeGeneration.addFunc("getFrustumBehindRadius", &RuntimeGeneration::getFrustumBehindRadius);
    runtimeGeneration.addFunc("getPendingGenerateCount", &RuntimeGeneration::getPendingGenerateCount);
    runtimeGeneration.addFunc("getGeneratingCount", &RuntimeGeneration::getGeneratingCount);
    runtimeGeneration.addFunc("getActiveCellCount", &RuntimeGeneration::getActiveCellCount);
    runtimeGeneration.addFunc("getPendingCleanupCount", &RuntimeGeneration::getPendingCleanupCount);
    runtimeGeneration.addFunc("getCancelledGenerationCount", &RuntimeGeneration::getCancelledGenerationCount);
    runtimeGeneration.addFunc("getFailedCellCount", &RuntimeGeneration::getFailedCellCount);
    runtimeGeneration.addFunc("retryFailedCells", &RuntimeGeneration::retryFailedCells);
    runtimeGeneration.addFunc("nextGenerate", &RuntimeGeneration::nextGenerate);
    runtimeGeneration.addFunc("nextCleanup", &RuntimeGeneration::nextCleanup);
    runtimeGeneration.addFunc("isRequestCurrent", &RuntimeGeneration::isRequestCurrent);
    runtimeGeneration.addFunc("completeGeneration", &RuntimeGeneration::completeGeneration);
    runtimeGeneration.addFunc("failGeneration", &RuntimeGeneration::failGeneration);
    runtimeGeneration.addFunc("completeCleanup", &RuntimeGeneration::completeCleanup);
    runtimeGeneration.addFunc("completeCleanupsAtomic", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* value,
                                                                                             ssq::Array requestArray) {
        std::vector<const ProcgenCellRequest*> requests;
        requests.reserve(requestArray.size());
        for (size_t index = 0; index < requestArray.size(); ++index)
            requests.push_back(requestArray.get<ProcgenCellRequest*>(index));
        return eve::script::projectResult(
            vm,
            value
                ? value->completeCleanupsAtomic(requests)
                : procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                       "completeCleanupsAtomic requires RuntimeGeneration", "runtime"),
            [](std::uint64_t completed) { return eve::Value(std::to_string(completed)); });
    });
    runtimeGeneration.addFunc("hasCell", &RuntimeGeneration::hasCell);
    runtimeGeneration.addFunc("getCellOutput", &RuntimeGeneration::getCellOutput);
    runtimeGeneration.addFunc("getCellRevision", &RuntimeGeneration::getCellRevision);
    runtimeGeneration.addFunc(
        "applyCellUpdate", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* value, int level, int x, int z,
                                                                 const std::string& revisionText, PointSet* output) {
            std::uint64_t revision = 0;
            const auto [end, error] =
                std::from_chars(revisionText.data(), revisionText.data() + revisionText.size(), revision);
            if (!output || error != std::errc{} || end != revisionText.data() + revisionText.size() || revision == 0)
                return eve::script::projectResult(
                    vm, procgenBindingFailure<std::uint64_t>(
                            eve::DiagnosticCode::InvalidArgument,
                            "applyCellUpdate requires an output and a non-zero decimal revision", "revision"),
                    [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
            return eve::script::projectResult(
                vm, value->applyCellUpdate(level, x, z, revision, *output),
                [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
        });
    runtimeGeneration.addFunc(
        "migrateCellPointIds", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* value, int level, int x, int z,
                                                                     const std::string& revisionText) {
            std::uint64_t revision = 0;
            const auto [end, error] =
                std::from_chars(revisionText.data(), revisionText.data() + revisionText.size(), revision);
            if (error != std::errc{} || end != revisionText.data() + revisionText.size() || revision == 0)
                return eve::script::projectResult(
                    vm, procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                             "migrateCellPointIds requires a non-zero decimal revision",
                                                             "revision"),
                    [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
            return eve::script::projectResult(
                vm, value->migrateCellPointIds(level, x, z, revision),
                [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
        });
    runtimeGeneration.addFunc("getCellDelta", &RuntimeGeneration::getCellDelta);
    runtimeGeneration.addFunc("serializeCell", &RuntimeGeneration::serializeCell);
    runtimeGeneration.addFunc("deserializeCell", &RuntimeGeneration::deserializeCell);
    runtimeGeneration.addFunc("debugReport", &RuntimeGeneration::debugReport);

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
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "owned procgen params proxy must not be null", "params"));
        return eve::script::projectResult(vm, Procgen::release(value->reference));
    });
    ownedParams.addFunc("setSeed", [vm](ScriptProcgenParams* value, uint32_t seed) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setSeed(seed);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setSize", [vm](ScriptProcgenParams* value, int width, int height) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setSize(width, height);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setInt", [vm](ScriptProcgenParams* value, const std::string& key, int number) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setInt(key, number);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setFloat", [vm](ScriptProcgenParams* value, const std::string& key, float number) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setFloat(key, number);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setBool", [vm](ScriptProcgenParams* value, const std::string& key, bool flag) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setBool(key, flag);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setString", [vm](ScriptProcgenParams* value, const std::string& key, const std::string& text) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
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
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "owned procgen grid proxy must not be null", "grid"));
        return eve::script::projectResult(vm, Procgen::release(value->reference));
    });
    ownedGrid.addFunc("resize", [vm](ScriptProcgenGrid* value, int width, int height) {
        if (!value) return staleProcgenResult<void>(vm, "grid");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "grid");
        view->resize(width, height);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedGrid.addFunc("fill", [vm](ScriptProcgenGrid* value, int semantic) {
        if (!value) return staleProcgenResult<void>(vm, "grid");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "grid");
        view->fill(semantic);
        return eve::script::projectResult(vm,
                                          eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedGrid.addFunc("setCell", [vm](ScriptProcgenGrid* value, int x, int y, int semantic) {
        if (!value) return staleProcgenResult<void>(vm, "grid");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "grid");
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
        if (!value) return staleProcgenResult<void>(vm, "grid");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "grid");
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
        if (!value) return staleProcgenResult<void>(vm, "grid");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "grid");
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
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "owned procgen context proxy must not be null", "context"));
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
        if (!value) return staleProcgenResult<void>(vm, "context");
        auto* module = ModuleManager::getInstance<Procgen>("Procgen");
        if (!module)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                                                "context"));
        return eve::script::projectResult(vm, module->commitSystem(value->reference));
    });
    ownedContext.addFunc("abort", [vm](ScriptProcgenContext* value) {
        if (!value) return staleProcgenResult<void>(vm, "context");
        auto* module = ModuleManager::getInstance<Procgen>("Procgen");
        if (!module)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                                                "context"));
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
    mesh.addFunc("getIndex", &MeshBuild::getIndex);
    mesh.addFunc("setMeta", &MeshBuild::setMeta);
    mesh.addFunc("getMeta", &MeshBuild::getMeta);

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

    auto heightmap = table.addClass<Heightmap>(
        "ProcgenHeightmap", std::function<Heightmap*()>([]() -> Heightmap* { return nullptr; }), true);
    heightmap.addFunc("resize", &Heightmap::resize);
    heightmap.addFunc("getWidth", &Heightmap::getWidth);
    heightmap.addFunc("getHeight", &Heightmap::getHeight);
    heightmap.addFunc("setHeight", &Heightmap::setHeight);
    heightmap.addFunc("height", &Heightmap::height);
    heightmap.addFunc("sampleBilinear", &Heightmap::sampleBilinear);
    heightmap.addFunc("sampleBilinearSeamless", &Heightmap::sampleBilinearSeamless);

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
    cls.addFunc("newParams", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        return makeOwnedProxy<ProcgenParamsHandleRef, ScriptProcgenParams>(
            vm, Procgen::newParamsHandle(), [](ProcgenParamsHandleRef ref) { return Procgen::release(ref); });
    });
    cls.addFunc("newGrid", [vm = cls.getHandle()](Procgen*, int width, int height) -> ssq::Table {
        return makeOwnedProxy<ProcgenGridHandleRef, ScriptProcgenGrid>(
            vm, Procgen::newGridHandle(width, height), [](ProcgenGridHandleRef ref) { return Procgen::release(ref); });
    });
    cls.addFunc("newOutput", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<OutputSpec>(
            vm, module->newOutputHandle(), [module](ProcgenOutputHandleRef ref) { return module->resolveOutput(ref); },
            [](ProcgenOutputHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releaseOutput(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "output");
            });
    });
    cls.addFunc("newPointSet", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<PointSet>(
            vm, module->newPointSetHandle(),
            [module](ProcgenPointSetHandleRef ref) { return module->resolvePointSet(ref); },
            [](ProcgenPointSetHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releasePointSet(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "pointSet");
            });
    });
    cls.addFunc("sampleGrid",
                [vm = cls.getHandle()](Procgen*, int width, int depth, float spacing, uint32_t seed,
                                       float jitter) -> ssq::Table {
                    auto* module = Procgen::create();
                    return makeOwnedNativeProxy<PointSet>(
                        vm, module->sampleGridHandle(width, depth, spacing, seed, jitter),
                        [module](ProcgenPointSetHandleRef ref) { return module->resolvePointSet(ref); },
                        [](ProcgenPointSetHandleRef ref) {
                            auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                            return owner
                                       ? owner->releasePointSet(ref)
                                       : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                                     "Procgen module is no longer loaded", "pointSet");
                         });
                 });
    cls.addFunc("filterHeight", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minimum, float maximum) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                    "filterHeight requires owned points", "points"));
                    return makeOwnedPointSetProxy(vm, value->filterHeightHandle(*reference, minimum, maximum));
                });
    cls.addFunc("filterDensity", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minimum, float maximum) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                    "filterDensity requires owned points", "points"));
                    return makeOwnedPointSetProxy(vm, value->filterDensityHandle(*reference, minimum, maximum));
                });
    cls.addFunc("filterBox", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minX, float minY, float minZ,
                                       float maxX, float maxY, float maxZ) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                    "filterBox requires owned points", "points"));
        return makeOwnedPointSetProxy(vm,
                                      value->filterBoxHandle(*reference, minX, minY, minZ, maxX, maxY, maxZ, false));
                });
    cls.addFunc("excludeBox", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minX, float minY,
                                                     float minZ, float maxX, float maxY, float maxZ) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                    "excludeBox requires owned points", "points"));
        return makeOwnedPointSetProxy(vm, value->filterBoxHandle(*reference, minX, minY, minZ, maxX, maxY, maxZ, true));
                });
    cls.addFunc("filterSlope", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minimum, float maximum) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                    "filterSlope requires owned points", "points"));
                    return makeOwnedPointSetProxy(vm, value->filterSlopeHandle(*reference, minimum, maximum));
                });
    cls.addFunc(
        "excludeRadius", [vm = cls.getHandle()](Procgen* value, PointSet* input, float x, float z, float radius) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                    vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                            eve::DiagnosticCode::InvalidArgument, "excludeRadius requires owned points", "points"));
                    return makeOwnedPointSetProxy(vm, value->excludeRadiusHandle(*reference, x, z, radius));
                });
    cls.addFunc("jitterPoints",
                [vm = cls.getHandle()](Procgen* value, PointSet* input, uint32_t seed, float amountX, float amountZ) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                                                              eve::DiagnosticCode::InvalidArgument,
                                                              "jitterPoints requires owned points", "points"));
                    return makeOwnedPointSetProxy(vm, value->jitterPointsHandle(*reference, seed, amountX, amountZ));
                });
    cls.addFunc("poissonDisk", [vm = cls.getHandle()](Procgen* value, int width, int depth, float radius, uint32_t seed,
                                       int maxPoints) {
                    if (!value)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                    "poissonDisk requires Procgen", "procgen"));
                    return makeOwnedPointSetProxy(vm, value->poissonDiskHandle(width, depth, radius, seed, maxPoints));
                });
    cls.addFunc("mergePoints", [vm = cls.getHandle()](Procgen* value, PointSet* first, PointSet* second) {
        const auto firstRef  = nativeProxyReference<ProcgenPointSetHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenPointSetHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                   "mergePoints requires owned point sets", "points"));
        return makeOwnedPointSetProxy(vm, value->mergePointsHandle(*firstRef, *secondRef));
    });
    cls.addFunc("unionPoints", [vm = cls.getHandle()](Procgen* value, PointSet* first, PointSet* second) {
        const auto firstRef = nativeProxyReference<ProcgenPointSetHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenPointSetHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                   "unionPoints requires owned point sets", "points"));
        return makeOwnedPointSetProxy(vm, value->unionPointsHandle(*firstRef, *secondRef));
    });
    cls.addFunc("intersectPoints", [vm = cls.getHandle()](Procgen* value, PointSet* first, PointSet* second) {
        const auto firstRef = nativeProxyReference<ProcgenPointSetHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenPointSetHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "intersectPoints requires owned point sets", "points"));
        return makeOwnedPointSetProxy(vm, value->intersectPointsHandle(*firstRef, *secondRef));
    });
    cls.addFunc("differencePoints", [vm = cls.getHandle()](Procgen* value, PointSet* first, PointSet* second) {
        const auto firstRef = nativeProxyReference<ProcgenPointSetHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenPointSetHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "differencePoints requires owned point sets", "points"));
        return makeOwnedPointSetProxy(vm, value->differencePointsHandle(*firstRef, *secondRef));
    });
    cls.addFunc("transformPoints", [vm = cls.getHandle()](Procgen* value, PointSet* input, float x, float y, float z,
                                                          float yaw, float scaleX, float scaleY, float scaleZ) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                    "transformPoints requires owned points", "points"));
        return makeOwnedPointSetProxy(vm,
                                      value->transformPointsHandle(*reference, x, y, z, yaw, scaleX, scaleY, scaleZ));
                });
    cls.addFunc("transformPoints3D", [vm = cls.getHandle()](Procgen* value, PointSet* input, float x, float y, float z,
                                                            float pitch, float yaw, float roll, float scaleX,
                                                            float scaleY, float scaleZ) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "transformPoints3D requires owned points", "points"));
                    return makeOwnedPointSetProxy(
            vm, value->transformPoints3DHandle(*reference, x, y, z, pitch, yaw, roll, scaleX, scaleY, scaleZ));
                });
    cls.addFunc("copyPoints", [vm = cls.getHandle()](Procgen* value, PointSet* source, PointSet* targets,
                                       bool inheritTargetAttributes) {
                    const auto sourceRef = nativeProxyReference<ProcgenPointSetHandleRef>(source);
                    const auto targetRef = nativeProxyReference<ProcgenPointSetHandleRef>(targets);
                    if (!value || !sourceRef || !targetRef)
                        return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                    "copyPoints requires owned point sets", "points"));
        return makeOwnedPointSetProxy(vm, value->copyPointsHandle(*sourceRef, *targetRef, inheritTargetAttributes));
                });
    cls.addFunc("remapDensity", [vm = cls.getHandle()](Procgen* value, PointSet* input, float inputMin, float inputMax,
                                                       float outputMin, float outputMax, bool clampOutput) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                    "remapDensity requires owned points", "points"));
                    return makeOwnedPointSetProxy(
            vm, value->remapDensityHandle(*reference, inputMin, inputMax, outputMin, outputMax, clampOutput));
                });
    cls.addFunc(
        "mathFloatAttribute", [vm = cls.getHandle()](Procgen* value, PointSet* input, const std::string& attribute,
                                                     const std::string& outputAttribute, const std::string& operation,
                                                     float operand, float defaultValue) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                return makeOwnedPointSetProxy(vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "mathFloatAttribute requires owned points", "points"));
            return makeOwnedPointSetProxy(vm, value->mathFloatAttributeHandle(*reference, attribute, outputAttribute,
                                                            operation, operand, defaultValue));
                });
    cls.addFunc("filterFloatAttribute", [vm = cls.getHandle()](Procgen* value, PointSet* input, const std::string& name,
                                                               float minimum, float maximum, bool invert) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "filterFloatAttribute requires owned points", "points"));
        return makeOwnedPointSetProxy(vm,
                                      value->filterFloatAttributeHandle(*reference, name, minimum, maximum, invert));
                });
    cls.addFunc("filterStringAttribute", [vm = cls.getHandle()](Procgen* value, PointSet* input,
                                                                const std::string& name, const std::string& expected,
                                                                bool invert) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "filterStringAttribute requires owned points", "points"));
        return makeOwnedPointSetProxy(vm, value->filterStringAttributeHandle(*reference, name, expected, invert));
                });
    cls.addFunc(
        "densityCull", [vm = cls.getHandle()](Procgen* value, PointSet* input, uint32_t seed, float multiplier) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                        "densityCull requires owned points", "points"));
                    return makeOwnedPointSetProxy(vm, value->densityCullHandle(*reference, seed, multiplier));
                });
    cls.addFunc("projectToWorld", [vm = cls.getHandle()](Procgen* value, PointSet* input, float maxY, float minY,
                                       std::int64_t maskBits, bool keepUnmatched) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference || maskBits < 0)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "projectToWorld requires owned points and a non-negative mask", "points"));
                    return makeOwnedPointSetProxy(
            vm,
            value->projectToWorldHandle(*reference, maxY, minY, static_cast<std::uint64_t>(maskBits), keepUnmatched));
                });
    cls.addFunc("newTerrainSampler", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<TerrainSampler>(
            vm, module->newTerrainSamplerHandle(),
            [module](ProcgenTerrainSamplerHandleRef ref) { return module->resolveTerrainSampler(ref); },
            [](ProcgenTerrainSamplerHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releaseTerrainSampler(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "sampler");
            });
    });
    cls.addFunc("newHeightmap", [vm = cls.getHandle()](Procgen*, int width, int height) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<Heightmap>(
            vm, module->newHeightmapHandle(width, height),
            [module](ProcgenHeightmapHandleRef ref) { return module->resolveHeightmap(ref); },
            [module](ProcgenHeightmapHandleRef ref) { return module->releaseHeightmap(ref); });
    });
    cls.addFunc("newCloudField", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<CloudField>(
            vm, module->newCloudFieldHandle(),
            [module](ProcgenCloudFieldHandleRef ref) { return module->resolveCloudField(ref); },
            [](ProcgenCloudFieldHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releaseCloudField(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "cloudField");
            });
    });
    cls.addFunc("newCloudShadow", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<CloudShadow>(
            vm, module->newCloudShadowHandle(),
            [module](ProcgenCloudShadowHandleRef ref) { return module->resolveCloudShadow(ref); },
            [](ProcgenCloudShadowHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releaseCloudShadow(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "cloudShadow");
            });
    });
    cls.addFunc(
        "generate",
        [vm = cls.getHandle()](Procgen*, const std::string& algorithm, ScriptProcgenParams* params) -> ssq::Table {
            auto* module = Procgen::create();
            if (!params)
                return eve::script::projectStatusResult(
                    vm,
                    procgenBindingFailure<ProcgenGridHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                "generate params proxy must not be null", "params")
                        .status(),
                    false, false);
            return makeOwnedProxy<ProcgenGridHandleRef, ScriptProcgenGrid>(
                vm, module->generateHandle(algorithm, params->reference),
                [](ProcgenGridHandleRef ref) { return Procgen::release(ref); });
        });
    cls.addFunc(
        "generateImage",
        [vm = cls.getHandle()](Procgen*, const std::string& recipe, ScriptProcgenParams* params) -> ssq::Table {
            auto* module = Procgen::create();
            if (!params)
                return eve::script::projectStatusResult(
                    vm,
                    procgenBindingFailure<ProcgenImageHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "generateImage params proxy must not be null", "params")
                        .status(),
                    false, false);
            return makeOwnedNativeProxy<image::ImageData>(
                vm, module->generateImageHandle(recipe, params->reference),
                [](ProcgenImageHandleRef ref) { return Procgen::resolve(ref); },
                [](ProcgenImageHandleRef ref) { return Procgen::release(ref); });
        });
    cls.addFunc("generateNormalImage",
                [vm = cls.getHandle()](Procgen*, const std::string& recipe, ScriptProcgenParams* params) -> ssq::Table {
                    auto* module = Procgen::create();
                    if (!params)
                        return eve::script::projectStatusResult(
                            vm,
                            procgenBindingFailure<ProcgenNormalImageHandleRef>(
                                eve::DiagnosticCode::InvalidArgument,
                                "generateNormalImage params proxy must not be null", "params")
                                .status(),
                            false, false);
                    return makeOwnedNativeProxy<image::ImageData>(
                        vm, module->generateNormalImageHandle(recipe, params->reference),
                        [](ProcgenNormalImageHandleRef ref) { return Procgen::resolve(ref); },
                        [](ProcgenNormalImageHandleRef ref) { return Procgen::release(ref); });
                });
    cls.addFunc("generatePbrMaterial",
                [vm = cls.getHandle()](Procgen*, const std::string& recipe, ScriptProcgenParams* params) -> ssq::Table {
                    auto* module = Procgen::create();
                    if (!params)
                        return eve::script::projectStatusResult(
                            vm,
                            procgenBindingFailure<ProcgenPbrMaterialHandleRef>(
                                eve::DiagnosticCode::InvalidArgument,
                                "generatePbrMaterial params proxy must not be null", "params")
                                .status(),
                            false, false);
                    return makeOwnedNativeProxy<PbrTextureSet>(
                        vm, module->generatePbrMaterialHandle(recipe, params->reference),
                        [module](ProcgenPbrMaterialHandleRef ref) { return module->resolvePbrMaterial(ref); },
                        [](ProcgenPbrMaterialHandleRef ref) {
                            auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                            return owner ? owner->ownership_->pbr.erase(ref)
                                         : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                                       "Procgen module is no longer loaded", "pbr");
                        });
                });
    cls.addFunc("buildMesh",
                [vm = cls.getHandle()](Procgen*, const std::string& recipe, ScriptProcgenParams* params) -> ssq::Table {
                    auto* module = Procgen::create();
                    if (!params)
                        return eve::script::projectStatusResult(vm,
                                                                procgenBindingFailure<ProcgenMeshBuildHandleRef>(
                                                                    eve::DiagnosticCode::InvalidArgument,
                                                                    "buildMesh params proxy must not be null", "params")
                                                                    .status(),
                                                                false, false);
                    return makeOwnedNativeProxy<MeshBuild>(
                        vm, module->buildMeshHandle(recipe, params->reference),
                        [module](ProcgenMeshBuildHandleRef ref) { return module->resolveMeshBuild(ref); },
                        [](ProcgenMeshBuildHandleRef ref) {
                            auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                            return owner ? owner->ownership_->meshes.erase(ref)
                                         : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                                       "Procgen module is no longer loaded", "mesh");
                        });
                });
    cls.addFunc("generateHeightmap", [vm = cls.getHandle()](Procgen*, ScriptProcgenParams* params) -> ssq::Table {
        auto* module = Procgen::create();
        if (!params)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<ProcgenHeightmapHandleRef>(
                    eve::DiagnosticCode::InvalidArgument, "generateHeightmap params proxy must not be null", "params")
                    .status(),
                false, false);
        return makeOwnedNativeProxy<Heightmap>(
            vm, module->generateHeightmapHandle(params->reference),
            [module](ProcgenHeightmapHandleRef ref) { return module->resolveHeightmap(ref); },
            [](ProcgenHeightmapHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->ownership_->heightmaps.erase(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "heightmap");
            });
    });
    cls.addFunc("newRuntimeGeneration", [vm = cls.getHandle()](Procgen* value, uint32_t worldSeed) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                            "runtime generation requires a Procgen module", "procgen")
                    .status(),
                false, false);
        return makeOwnedNativeProxy<RuntimeGeneration>(
            vm, value->newRuntimeGenerationHandle(worldSeed),
            [value](ProcgenRuntimeGenerationHandleRef ref) { return value->resolveRuntimeGeneration(ref); },
            [](ProcgenRuntimeGenerationHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->release(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "runtimeGeneration");
            });
    });
    cls.addFunc("newPointGraph", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument, "Procgen module must not be null",
                                            "procgen")
                    .status(),
                false, false);
        return makeOwnedNativeProxy<PointGraph>(
            vm, value->newPointGraphHandle(),
            [value](ProcgenPointGraphHandleRef ref) { return value->resolvePointGraph(ref); },
            [](ProcgenPointGraphHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->release(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "pointGraph");
            });
    });
    cls.addFunc("newBiomeRules", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument, "Procgen module must not be null",
                                            "procgen")
                    .status(),
                false, false);
        return makeOwnedNativeProxy<BiomeRules>(
            vm, value->newBiomeRulesHandle(),
            [value](ProcgenBiomeRulesHandleRef ref) { return value->resolveBiomeRules(ref); },
            [](ProcgenBiomeRulesHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->release(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "biomeRules");
            });
    });
    cls.addFunc("boxVolume",
                [vm = cls.getHandle()](Procgen* value, float minX, float minY, float minZ, float maxX, float maxY,
                                       float maxZ) -> ssq::Table {
                    if (!value)
                        return eve::script::projectStatusResult(
                            vm,
                            procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                        "Procgen module must not be null", "procgen")
                                .status(),
                            false, false);
                    return makeOwnedSpatialProxy(vm, value->boxVolumeHandle(minX, minY, minZ, maxX, maxY, maxZ));
                });
    cls.addFunc("sphereVolume", [vm = cls.getHandle()](Procgen* value, float x, float y, float z, float radius) {
        if (!value)
            return makeOwnedSpatialProxy(
                vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                       "sphereVolume requires Procgen", "procgen"));
        return makeOwnedSpatialProxy(vm, value->sphereVolumeHandle(x, y, z, radius));
    });
    cls.addFunc("polygonVolume", [vm = cls.getHandle()](Procgen* value, PointSet* points, float minY, float maxY) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(points);
                    if (!value || !reference)
                        return makeOwnedSpatialProxy(
                            vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "polygonVolume requires owned control points", "points"));
                    return makeOwnedSpatialProxy(vm, value->polygonVolumeHandle(*reference, minY, maxY));
                });
    cls.addFunc("pointData", [vm = cls.getHandle()](Procgen* value, PointSet* points) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(points);
        if (!value || !reference)
            return makeOwnedSpatialProxy(
                vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                       "pointData requires owned points", "points"));
        return makeOwnedSpatialProxy(vm, value->pointDataHandle(*reference));
    });
    cls.addFunc("heightfieldData", [vm = cls.getHandle()](Procgen* value, Heightmap* heightmap, float originX,
                                                          float originZ, float cellSize, float heightScale) {
                    const auto reference = nativeProxyReference<ProcgenHeightmapHandleRef>(heightmap);
                    if (!value || !reference)
            return makeOwnedSpatialProxy(vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "heightfieldData requires an owned heightmap", "heightmap"));
        return makeOwnedSpatialProxy(vm,
                                     value->heightfieldDataHandle(*reference, originX, originZ, cellSize, heightScale));
                });
    cls.addFunc("textureMaskData", [vm = cls.getHandle()](Procgen* value, Heightmap* values, float originX,
                                                          float originZ, float cellSize, float minValue, float maxValue,
                                                          float minY, float maxY) {
                    const auto reference = nativeProxyReference<ProcgenHeightmapHandleRef>(values);
                    if (!value || !reference)
            return makeOwnedSpatialProxy(vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "textureMaskData requires an owned scalar map", "values"));
                    return makeOwnedSpatialProxy(
            vm, value->textureMaskDataHandle(*reference, originX, originZ, cellSize, minValue, maxValue, minY, maxY));
                });
    cls.addFunc("meshSurfaceData", [vm = cls.getHandle()](Procgen* value, MeshBuild* mesh, float tolerance) {
                    const auto reference = nativeProxyReference<ProcgenMeshBuildHandleRef>(mesh);
                    if (!value || !reference)
                        return makeOwnedSpatialProxy(
                            vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "meshSurfaceData requires an owned mesh build", "mesh"));
                    return makeOwnedSpatialProxy(vm, value->meshSurfaceDataHandle(*reference, tolerance));
                });
    cls.addFunc("unionSpatial", [vm = cls.getHandle()](Procgen* value, SpatialData* first, SpatialData* second) {
        const auto firstRef  = nativeProxyReference<ProcgenSpatialDataHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedSpatialProxy(
                vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "unionSpatial requires owned spatial data", "spatial"));
        return makeOwnedSpatialProxy(vm, value->unionSpatialHandle(*firstRef, *secondRef));
    });
    cls.addFunc("intersectSpatial", [vm = cls.getHandle()](Procgen* value, SpatialData* first, SpatialData* second) {
                    const auto firstRef  = nativeProxyReference<ProcgenSpatialDataHandleRef>(first);
                    const auto secondRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(second);
                    if (!value || !firstRef || !secondRef)
            return makeOwnedSpatialProxy(vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "intersectSpatial requires owned spatial data", "spatial"));
                    return makeOwnedSpatialProxy(vm, value->intersectSpatialHandle(*firstRef, *secondRef));
                });
    cls.addFunc("differenceSpatial", [vm = cls.getHandle()](Procgen* value, SpatialData* first, SpatialData* second) {
                    const auto firstRef  = nativeProxyReference<ProcgenSpatialDataHandleRef>(first);
                    const auto secondRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(second);
                    if (!value || !firstRef || !secondRef)
            return makeOwnedSpatialProxy(vm, procgenBindingFailure<ProcgenSpatialDataHandleRef>(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "differenceSpatial requires owned spatial data", "spatial"));
                    return makeOwnedSpatialProxy(vm, value->differenceSpatialHandle(*firstRef, *secondRef));
                });
    cls.addFunc("sampleSpatial", [vm = cls.getHandle()](Procgen* value, SpatialData* spatial, float spacing,
                                                        uint32_t seed, float jitter) {
                    const auto reference = nativeProxyReference<ProcgenSpatialDataHandleRef>(spatial);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "sampleSpatial requires owned spatial data", "spatial"));
                    return makeOwnedPointSetProxy(vm, value->sampleSpatialHandle(*reference, spacing, seed, jitter));
                });
    cls.addFunc(
        "filterSpatial", [vm = cls.getHandle()](Procgen* value, PointSet* points, SpatialData* spatial, bool invert) {
                    const auto pointsRef  = nativeProxyReference<ProcgenPointSetHandleRef>(points);
                    const auto spatialRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(spatial);
                    if (!value || !pointsRef || !spatialRef)
                        return makeOwnedPointSetProxy(
                    vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                            eve::DiagnosticCode::InvalidArgument, "filterSpatial requires owned inputs", "input"));
                    return makeOwnedPointSetProxy(vm, value->filterSpatialHandle(*pointsRef, *spatialRef, invert));
                });
    cls.addFunc("projectToSpatial", [vm = cls.getHandle()](Procgen* value, PointSet* points, SpatialData* spatial) {
        const auto pointsRef  = nativeProxyReference<ProcgenPointSetHandleRef>(points);
        const auto spatialRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(spatial);
        if (!value || !pointsRef || !spatialRef)
            return makeOwnedPointSetProxy(
                vm, procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                                   "projectToSpatial requires owned inputs", "input"));
        return makeOwnedPointSetProxy(vm, value->projectToSpatialHandle(*pointsRef, *spatialRef));
    });
    cls.addFunc("splineData", [vm = cls.getHandle()](Procgen* value, PointSet* points, float radius) -> ssq::Table {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(points);
        if (!value || !reference)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                            "splineData requires an owned point set", "points")
                    .status(),
                false, false);
        return makeOwnedSpatialProxy(vm, value->splineDataHandle(*reference, radius));
    });
    cls.addFunc("sampleSpline",
                [vm = cls.getHandle()](Procgen* value, PointSet* points, float spacing, uint32_t seed,
                                       float jitter) -> ssq::Table {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(points);
                    if (!value || !reference)
                        return eve::script::projectStatusResult(
                            vm,
                            procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                        "sampleSpline requires an owned point set", "points")
                                .status(),
                            false, false);
                    return makeOwnedPointSetProxy(vm, value->sampleSplineHandle(*reference, spacing, seed, jitter));
                });
    cls.addFunc("filterSplineDistance",
                [vm = cls.getHandle()](Procgen* value, PointSet* input, PointSet* spline, float minDistance,
                                       float maxDistance) -> ssq::Table {
                    const auto inputRef  = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    const auto splineRef = nativeProxyReference<ProcgenPointSetHandleRef>(spline);
                    if (!value || !inputRef || !splineRef)
                        return eve::script::projectStatusResult(
                            vm,
                            procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                        "filterSplineDistance requires owned point sets", "points")
                                .status(),
                            false, false);
                    return makeOwnedPointSetProxy(
                        vm, value->filterSplineDistanceHandle(*inputRef, *splineRef, minDistance, maxDistance));
                });
    cls.addFunc("selfPrune", [vm = cls.getHandle()](Procgen* value, PointSet* input, float radius) -> ssq::Table {
        const auto inputRef = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !inputRef)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument, "selfPrune requires owned points",
                                            "points")
                    .status(),
                false, false);
        return makeOwnedPointSetProxy(vm, value->selfPruneHandle(*inputRef, radius));
    });
    cls.addFunc(
        "publishCellInstances",
        [vm = cls.getHandle()](Procgen* value, const std::string& prefix, ProcgenCellRequest* request, PointSet* points,
                               const std::string& assetAttribute, const std::string& defaultAsset) {
            const auto pointsRef = nativeProxyReference<ProcgenPointSetHandleRef>(points);
            if (!value || !request || !pointsRef)
                return eve::script::projectResult(
                    vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                    "publishCellInstances requires request and owned points",
                                                    "publishCellInstances"));
            return eve::script::projectResult(
                vm, value->publishCellInstances(prefix, *request, *pointsRef, assetAttribute, defaultAsset));
        });
    cls.addFunc("publishCellSnapshot", [vm = cls.getHandle()](
                                           Procgen* value, const std::string& prefix, ProcgenCellRequest* request,
                                           PointSet* points, const std::string& revisionText,
                                           const std::string& assetAttribute, const std::string& defaultAsset) {
        const auto    pointsRef      = nativeProxyReference<ProcgenPointSetHandleRef>(points);
        std::uint64_t targetRevision = 0;
        const auto [end, error] =
            std::from_chars(revisionText.data(), revisionText.data() + revisionText.size(), targetRevision);
        if (!value || !request || !pointsRef || error != std::errc{} ||
            end != revisionText.data() + revisionText.size() || targetRevision == 0)
            return eve::script::projectResult(
                vm,
                procgenBindingFailure<std::uint64_t>(
                    eve::DiagnosticCode::InvalidArgument,
                    "publishCellSnapshot requires request, owned points, and a non-zero decimal target revision",
                    "publishCellSnapshot"),
                [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
        return eve::script::projectResult(
            vm, value->publishCellSnapshot(prefix, *request, *pointsRef, targetRevision, assetAttribute, defaultAsset),
            [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
    });
    cls.addFunc("publishCellInstanceDelta", [vm = cls.getHandle()](
                                                Procgen* value, const std::string& prefix, ProcgenCellRequest* request,
                                                PointDelta* delta, const std::string& revisionText,
                                                const std::string& assetAttribute, const std::string& defaultAsset) {
        std::uint64_t targetRevision = 0;
        const auto [end, error] =
            std::from_chars(revisionText.data(), revisionText.data() + revisionText.size(), targetRevision);
        if (!value || !request || !delta || error != std::errc{} || end != revisionText.data() + revisionText.size() ||
            targetRevision < 2)
            return eve::script::projectResult(
                vm,
                procgenBindingFailure<std::uint64_t>(
                    eve::DiagnosticCode::InvalidArgument,
                    "publishCellInstanceDelta requires request, delta, and a decimal target revision greater than one",
                    "publishCellInstanceDelta"),
                [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
        return eve::script::projectResult(
            vm, value->publishCellInstanceDelta(prefix, *request, *delta, targetRevision, assetAttribute, defaultAsset),
            [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
    });
    cls.addFunc("synchronizeCellInstances",
                [vm = cls.getHandle()](Procgen* value, const std::string& prefix, RuntimeGeneration* runtime,
                                       ProcgenCellRequest* request, const std::string& assetAttribute,
                                       const std::string& defaultAsset) {
                    if (!value || !runtime || !request)
                        return eve::script::projectResult(
                            vm,
                            procgenBindingFailure<std::uint64_t>(
                                eve::DiagnosticCode::InvalidArgument,
                                "synchronizeCellInstances requires runtime and request", "synchronizeCellInstances"),
                            [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
                    return eve::script::projectResult(
                        vm, value->synchronizeCellInstances(prefix, *runtime, *request, assetAttribute, defaultAsset),
                        [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
                });
    cls.addFunc("synchronizeCellInstancesAtomic", [vm = cls.getHandle()](Procgen* value, const std::string& prefix,
                                                                         ssq::Array         runtimeArray,
                                                                         ssq::Array         requestArray,
                                                                         const std::string& assetAttribute,
                                                                         const std::string& defaultAsset) {
        std::vector<const RuntimeGeneration*>  runtimes;
        std::vector<const ProcgenCellRequest*> requests;
        if (runtimeArray.size() == requestArray.size()) {
            runtimes.reserve(runtimeArray.size());
            requests.reserve(requestArray.size());
            for (size_t index = 0; index < runtimeArray.size(); ++index) {
                runtimes.push_back(runtimeArray.get<RuntimeGeneration*>(index));
                requests.push_back(requestArray.get<ProcgenCellRequest*>(index));
            }
        }
        return eve::script::projectResult(
            vm,
            value ? value->synchronizeCellInstancesAtomic(prefix, runtimes, requests, assetAttribute, defaultAsset)
                  : procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                         "synchronizeCellInstancesAtomic requires Procgen", "procgen"),
            [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
    });
    cls.addFunc("removeCellInstances",
                [vm = cls.getHandle()](Procgen* value, const std::string& prefix, ProcgenCellRequest* request) {
                    if (!value || !request)
                        return eve::script::projectResult(
                            vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                            "removeCellInstances requires a request", "request"));
                    return eve::script::projectResult(vm, value->removeCellInstances(prefix, *request));
                });
    cls.addFunc("removeCellInstancesAtomic", [vm = cls.getHandle()](Procgen* value, const std::string& prefix,
                                                                    ssq::Array requestArray) {
        std::vector<const ProcgenCellRequest*> requests;
        requests.reserve(requestArray.size());
        for (size_t index = 0; index < requestArray.size(); ++index)
            requests.push_back(requestArray.get<ProcgenCellRequest*>(index));
        return eve::script::projectResult(
            vm,
            value ? value->removeCellInstancesAtomic(prefix, requests)
                  : procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                         "removeCellInstancesAtomic requires Procgen", "procgen"),
            [](std::uint64_t removed) { return eve::Value(std::to_string(removed)); });
    });
    cls.addFunc("completeCellCleanupAtomic", [vm = cls.getHandle()](Procgen* value, const std::string& prefix,
                                                                    ssq::Array runtimeArray, ssq::Array requestArray) {
        std::vector<RuntimeGeneration*>        runtimes;
        std::vector<const ProcgenCellRequest*> requests;
        runtimes.reserve(runtimeArray.size());
        requests.reserve(requestArray.size());
        for (size_t index = 0; index < runtimeArray.size(); ++index)
            runtimes.push_back(runtimeArray.get<RuntimeGeneration*>(index));
        for (size_t index = 0; index < requestArray.size(); ++index)
            requests.push_back(requestArray.get<ProcgenCellRequest*>(index));
        return eve::script::projectResult(
            vm,
            value ? value->completeCellCleanupAtomic(prefix, runtimes, requests)
                  : procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                         "completeCellCleanupAtomic requires Procgen", "procgen"),
            [](std::uint64_t removed) { return eve::Value(std::to_string(removed)); });
    });
    cls.addFunc("removeInstances", [vm = cls.getHandle()](Procgen* value, const std::string& batchId) {
        if (!value)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "removeInstances requires Procgen", "procgen"));
        return eve::script::projectResult(vm, value->removeInstances(batchId));
    });
    cls.addFunc(
        "generateTexture",
        [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params,
                               graphics::Graphics* gfx) -> ssq::Table {
            if (!value || !params || !gfx)
                return eve::script::projectStatusResult(
                    vm,
                    procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "generateTexture requires params and Graphics", "generateTexture")
                        .status(),
                    false, false);
            return projectBorrowedResult(vm, value->generateTextureBorrowed(recipe, params->reference, gfx), "texture");
        });
    cls.addFunc("generateMesh",
                [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params,
                                       graphics::Graphics* gfx) -> ssq::Table {
                    if (!value || !params || !gfx)
                        return eve::script::projectStatusResult(
                            vm,
                            procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                        "generateMesh requires params and Graphics", "generateMesh")
                                .status(),
                            false, false);
                    return projectBorrowedResult(vm, value->generateMeshBorrowed(recipe, params->reference, gfx),
                                                 "mesh");
                });
    cls.addFunc("uploadMesh",
                [vm = cls.getHandle()](Procgen* value, MeshBuild* mesh, graphics::Graphics* gfx) -> ssq::Table {
                    if (!value || !mesh || !gfx)
                        return eve::script::projectStatusResult(
                            vm,
                            procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                        "uploadMesh requires a mesh and Graphics", "uploadMesh")
                                .status(),
                            false, false);
                    return projectBorrowedResult(vm, value->uploadMeshBorrowed(*mesh, *gfx), "mesh");
                });
    cls.addFunc("deriveSeed", &Procgen::deriveSeed);
    cls.addFunc("beginSystem", [vm = cls.getHandle()](Procgen*, const std::string& name, uint32_t seed) -> ssq::Table {
        return makeOwnedProxy<ProcgenContextHandleRef, ScriptProcgenContext>(
            vm, Procgen::beginSystemHandle(name, seed),
            [](ProcgenContextHandleRef ref) { return Procgen::release(ref); });
    });
    cls.addFunc("beginCachedSystem",
                [vm = cls.getHandle()](Procgen*, const std::string& name, uint32_t seed,
                                       const std::string& buildKey) -> ssq::Table {
                    return makeOwnedProxy<ProcgenContextHandleRef, ScriptProcgenContext>(
                        vm, Procgen::beginCachedSystemHandle(name, seed, buildKey),
                        [](ProcgenContextHandleRef ref) { return Procgen::release(ref); });
                });
    cls.addFunc("commitSystem", [vm = cls.getHandle()](Procgen* value, ScriptProcgenContext* context) {
        if (!value || !context)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "commitSystem requires a context proxy", "context"));
        return eve::script::projectResult(vm, value->commitSystem(context->reference));
    });
    cls.addFunc("abortSystem", [vm = cls.getHandle()](Procgen* value, ScriptProcgenContext* context) {
        if (!value || !context)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "abortSystem requires a context proxy", "context"));
        return eve::script::projectResult(vm, value->abortSystem(context->reference));
    });
    cls.addFunc("getSystemOutput",
                [vm = cls.getHandle()](Procgen* value, const std::string& system, const std::string& output) {
                    if (!value)
                        return makeOwnedPointSetProxy(vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                                                              eve::DiagnosticCode::InvalidArgument,
                                                              "Procgen module must not be null", "procgen"));
                    return makeOwnedPointSetProxy(vm, value->getSystemOutputHandle(system, output));
                });
    cls.addFunc("getSystemDebugStage",
                [vm = cls.getHandle()](Procgen* value, const std::string& system, const std::string& stage) {
                    if (!value)
                        return makeOwnedPointSetProxy(vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                                                              eve::DiagnosticCode::InvalidArgument,
                                                              "Procgen module must not be null", "procgen"));
                    return makeOwnedPointSetProxy(vm, value->getSystemDebugStageHandle(system, stage));
                });
    cls.addFunc("getPreviousSystemDebugStage",
                [vm = cls.getHandle()](Procgen* value, const std::string& system, const std::string& stage) {
                    if (!value)
                        return makeOwnedPointSetProxy(vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                                                              eve::DiagnosticCode::InvalidArgument,
                                                              "Procgen module must not be null", "procgen"));
                    return makeOwnedPointSetProxy(vm, value->getPreviousSystemDebugStageHandle(system, stage));
                });
    cls.addFunc("removeSystem", [vm = cls.getHandle()](Procgen* value, const std::string& name) {
        if (!value)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "removeSystem requires a Procgen module", "procgen"));
        return eve::script::projectResult(vm, value->removeSystem(name));
    });
    cls.addFunc("hasSystem", &Procgen::hasSystem);
    cls.addFunc("getSystemRevision", &Procgen::getSystemRevision);
    cls.addFunc("getSystemSeed", &Procgen::getSystemSeed);
    cls.addFunc("getSystemBuildKey", &Procgen::getSystemBuildKey);
    cls.addFunc("getSystemOutputCount", &Procgen::getSystemOutputCount);
    cls.addFunc("getSystemOutputName", &Procgen::getSystemOutputName);
    cls.addFunc("getSystemDebugStageCount", &Procgen::getSystemDebugStageCount);
    cls.addFunc("getSystemDebugStageName", &Procgen::getSystemDebugStageName);
    cls.addFunc("getPreviousSystemRevision", &Procgen::getPreviousSystemRevision);
    cls.addFunc("getSystemDebugReport", &Procgen::getSystemDebugReport);
    cls.addFunc("getSystemDebugDiffReport", &Procgen::getSystemDebugDiffReport);
    cls.addFunc("setPaletteGid", &Procgen::setPaletteGid);
    cls.addFunc("getPaletteGid", &Procgen::getPaletteGid);
    cls.addFunc("getAlgorithmCount", &Procgen::getAlgorithmCount);
    cls.addFunc("getAlgorithmId", &Procgen::getAlgorithmId);
    cls.addFunc("hasAlgorithm", &Procgen::hasAlgorithm);
    cls.addFunc("getAlgorithmSchema", [vm = cls.getHandle()](Procgen* value, const std::string& algorithm) {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::InvalidArgument,
                                                        "algorithm schema requires a Procgen module", "procgen")
                    .status(),
                false, false);
        return projectRecipeDescriptorResult(vm, value->getAlgorithmSchema(algorithm));
    });
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
    cls.addFunc("applyAlgorithmDefaults",
                [vm = cls.getHandle()](Procgen* value, const std::string& algorithm, ScriptProcgenParams* params) {
                    if (!value || !params)
                        return eve::script::projectResult(
                            vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                            "algorithm defaults require a params proxy", "params"));
                    return eve::script::projectResult(vm, value->applyAlgorithmDefaults(algorithm, params->reference));
                });
    cls.addFunc("generateTo", [vm = cls.getHandle()](Procgen* value, const std::string& algorithm,
                                                     ScriptProcgenParams* params, OutputSpec* output) {
        const auto outputRef = nativeProxyReference<ProcgenOutputHandleRef>(output);
        if (!value || !params || !outputRef)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "generateTo requires params and owned output", "generateTo"));
        return eve::script::projectResult(vm, value->generateTo(algorithm, params->reference, *outputRef));
    });
    cls.addFunc("autotileGrid", [vm = cls.getHandle()](Procgen* value, ScriptProcgenGrid* grid) {
        if (!value || !grid)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "autotileGrid requires a grid proxy", "grid"));
        return eve::script::projectResult(vm, value->autotileGrid(grid->reference));
    });
    cls.addFunc("randomSeed", &Procgen::randomSeed);
    cls.addFunc("gridToJson", [vm = cls.getHandle()](Procgen* value, ScriptProcgenGrid* grid) {
        if (!value || !grid)
            return eve::script::projectResult(
                vm,
                procgenBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                   "gridToJson requires a grid proxy", "grid"),
                [](std::string&& json) { return eve::Value(std::move(json)); });
        return eve::script::projectResult(vm, value->gridToJson(grid->reference),
                                          [](std::string&& json) { return eve::Value(std::move(json)); });
    });
    cls.addFunc("getTextureRecipeCount", &Procgen::getTextureRecipeCount);
    cls.addFunc("getTextureRecipeId", &Procgen::getTextureRecipeId);
    cls.addFunc("hasTextureRecipe", &Procgen::hasTextureRecipe);
    cls.addFunc("getTextureRecipeSchema", [vm = cls.getHandle()](Procgen* value, const std::string& recipe) {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::InvalidArgument,
                                                        "texture schema requires a Procgen module", "procgen")
                    .status(),
                false, false);
        return projectRecipeDescriptorResult(vm, value->getTextureRecipeSchema(recipe));
    });
    cls.addFunc("applyTextureRecipeDefaults",
                [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params) {
                    if (!value || !params)
                        return eve::script::projectResult(
                            vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                            "texture defaults require a params proxy", "params"));
                    return eve::script::projectResult(vm, value->applyTextureRecipeDefaults(recipe, params->reference));
                });
    cls.addFunc("getPbrRecipeCount", &Procgen::getPbrRecipeCount);
    cls.addFunc("getPbrRecipeId", &Procgen::getPbrRecipeId);
    cls.addFunc("hasPbrRecipe", &Procgen::hasPbrRecipe);
    cls.addFunc("getPbrRecipeSchema", [vm = cls.getHandle()](Procgen* value, const std::string& recipe) {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::InvalidArgument,
                                                        "PBR schema requires a Procgen module", "procgen")
                    .status(),
                false, false);
        return projectRecipeDescriptorResult(vm, value->getPbrRecipeSchema(recipe));
    });
    cls.addFunc("applyPbrRecipeDefaults",
                [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params) {
                    if (!value || !params)
                        return eve::script::projectResult(
                            vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                            "PBR defaults require a params proxy", "params"));
                    return eve::script::projectResult(vm, value->applyPbrRecipeDefaults(recipe, params->reference));
                });
    cls.addFunc(
        "buildArtifact", [vm = cls.getHandle()](Procgen* value, const std::string& recipeId,
                                                ScriptProcgenParams* params, const std::string& artifactIdentity) {
            if (!value)
                return eve::script::projectResult(
                    vm,
                    procgenBindingFailure<GeneratedArtifact>(eve::DiagnosticCode::InvalidArgument,
                                                             "procgen module must not be null", "procgen"),
                    [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
            const auto parsed = ArtifactId::parse(artifactIdentity);
            if (!parsed || parsed->isNil())
                return eve::script::projectResult(
                    vm,
                    procgenBindingFailure<GeneratedArtifact>(eve::DiagnosticCode::InvalidArgument,
                                                             "artifact identity must be a non-nil canonical UUID",
                                                             "artifactIdentity"),
                    [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
            if (!params)
                return eve::script::projectResult(
                    vm,
                    procgenBindingFailure<GeneratedArtifact>(eve::DiagnosticCode::InvalidArgument,
                                                             "buildArtifact requires a params proxy", "params"),
                    [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
            return eve::script::projectResult(
                vm, value->buildArtifact(recipeId, params->reference, *parsed),
                [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
        });
    cls.addFunc(
        "publishArtifact",
        [vm = cls.getHandle()](Procgen* value, const std::string& recipeId, ScriptProcgenParams* params,
                               const std::string& artifactIdentity, bool scene, bool graphics, bool physics, bool map) {
            if (!value)
                return eve::script::projectResult(
                    vm,
                    procgenBindingFailure<ArtifactPublishReceipt>(eve::DiagnosticCode::InvalidArgument,
                                                                  "procgen module must not be null", "procgen"),
                    [](ArtifactPublishReceipt&& receipt) { return publishReceiptProjection(std::move(receipt)); });
            const auto parsed = ArtifactId::parse(artifactIdentity);
            if (!parsed || parsed->isNil())
                return eve::script::projectResult(
                    vm,
                    procgenBindingFailure<ArtifactPublishReceipt>(eve::DiagnosticCode::InvalidArgument,
                                                                  "artifact identity must be a non-nil canonical UUID",
                                                                  "artifactIdentity"),
                    [](ArtifactPublishReceipt&& receipt) { return publishReceiptProjection(std::move(receipt)); });
            ArtifactPublishOptions options;
            options.scene    = scene;
            options.graphics = graphics;
            options.physics  = physics;
            options.map      = map;
            if (!params)
                return eve::script::projectResult(
                    vm,
                    procgenBindingFailure<ArtifactPublishReceipt>(eve::DiagnosticCode::InvalidArgument,
                                                                  "publishArtifact requires a params proxy", "params"),
                    [](ArtifactPublishReceipt&& receipt) { return publishReceiptProjection(std::move(receipt)); });
            return eve::script::projectResult(
                vm, value->publishArtifact(recipeId, params->reference, *parsed, options),
                [](ArtifactPublishReceipt&& receipt) { return publishReceiptProjection(std::move(receipt)); });
        });
    cls.addFunc("getMeshRecipeCount", &Procgen::getMeshRecipeCount);
    cls.addFunc("getMeshRecipeId", &Procgen::getMeshRecipeId);
    cls.addFunc("hasMeshRecipe", &Procgen::hasMeshRecipe);
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
    cls.addFunc("getMeshRecipeSchema", [vm = cls.getHandle()](Procgen* value, const std::string& recipe) {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::InvalidArgument,
                                                        "mesh schema requires a Procgen module", "procgen")
                    .status(),
                false, false);
        return projectRecipeDescriptorResult(vm, value->getMeshRecipeSchema(recipe));
    });
    cls.addFunc("applyMeshRecipeDefaults",
                [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params) {
                    if (!value || !params)
                        return eve::script::projectResult(
                            vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                            "mesh defaults require a params proxy", "params"));
                    return eve::script::projectResult(vm, value->applyMeshRecipeDefaults(recipe, params->reference));
                });
}

}  // namespace eve::procgen
