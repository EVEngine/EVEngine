#include "procgen/Procgen.h"

#include "common/SquirrelBinding.h"
#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "procgen/BiomeScript.h"
#include "procgen/PointGraphScript.h"
#include "procgen/ShapeGrammarScript.h"
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

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <unordered_set>
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

struct ProcgenAnyHandle {
    std::uint64_t ownerEpoch = 0;
    std::uint64_t packedValue = 0;
    std::function<eve::Result<void>()> release;
    std::function<bool()> stale;
    [[nodiscard]] std::uint64_t packed() const noexcept { return packedValue; }
};

/** @brief Script-owned opaque handle for procgen objects with typed C++ refs. */
struct ScriptProcgenHandle {
    explicit ScriptProcgenHandle(ProcgenAnyHandle value)
        : ownerEpoch(value.ownerEpoch), packed(value.packedValue), release(std::move(value.release)),
          stale(std::move(value.stale)) {}
    std::uint64_t ownerEpoch = 0;
    std::uint64_t packed = 0;
    std::function<eve::Result<void>()> release;
    std::function<bool()> stale;
    bool released = false;

    ~ScriptProcgenHandle() {
        if (!released && release) release().ignore("script procgen proxy destructor release");
    }
};

template <class T>
eve::Result<T> procgenBindingFailure(eve::DiagnosticCode code, std::string message, std::string path = {});

std::mutex heightmapProxyMutex;
std::unordered_map<Heightmap*, ProcgenHeightmapHandleRef> heightmapProxyRefs;

SQInteger releaseHeightmapProxy(SQUserPointer pointer, SQInteger) {
    auto* heightmap = static_cast<Heightmap*>(pointer);
    if (!heightmap) return 0;

    std::optional<ProcgenHeightmapHandleRef> reference;
    {
        std::lock_guard lock(heightmapProxyMutex);
        const auto      found = heightmapProxyRefs.find(heightmap);
        if (found == heightmapProxyRefs.end()) return 0;
        reference = found->second;
        heightmapProxyRefs.erase(found);
    }

    if (auto* owner = ModuleManager::getInstance<Procgen>("Procgen"); owner && reference)
        owner->releaseHeightmap(*reference).ignore("release Squirrel heightmap proxy");
    return 0;
}

ssq::Table makeOwnedHeightmapProxy(HSQUIRRELVM vm, eve::Result<ProcgenHeightmapHandleRef>&& reference) {
    if (!reference)
        return eve::script::projectStatusResult(vm, reference.status(), false, false);

    const auto ref    = std::move(reference).takeValue();
    auto*      module = ModuleManager::getInstance<Procgen>("Procgen");
    const auto view   = module ? module->resolveHeightmap(ref) : eve::script::Borrowed<Heightmap>();
    if (!view.isBound()) {
        if (module) module->releaseHeightmap(ref).ignore("rollback unbound Squirrel heightmap proxy");
        return eve::script::projectStatusResult(
            vm, procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                            "heightmap proxy could not resolve its owned object", "heightmap")
                    .status(),
            false, false);
    }

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, ssq::detail::getClassObj(vm, typeid(Heightmap*).hash_code()));
    if (SQ_FAILED(sq_createinstance(vm, -1))) {
        sq_settop(vm, top);
        module->releaseHeightmap(ref).ignore("rollback failed Squirrel heightmap instance");
        return eve::script::projectStatusResult(
            vm, procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                            "failed to create Squirrel heightmap proxy", "heightmap")
                    .status(),
            false, false);
    }
    sq_remove(vm, -2);
    auto* native = view.get();
    {
        std::lock_guard lock(heightmapProxyMutex);
        heightmapProxyRefs.emplace(native, ref);
    }
    sq_setinstanceup(vm, -1, native);
    sq_settypetag(vm, -1, reinterpret_cast<SQUserPointer>(typeid(Heightmap*).hash_code()));
    sq_setreleasehook(vm, -1, &releaseHeightmapProxy);

    ssq::Instance value(vm);
    sq_getstackobj(vm, -1, &value.getRaw());
    sq_addref(vm, &value.getRaw());
    sq_settop(vm, top);

    auto result = eve::script::projectStatusResult(
        vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", value);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

template <class T>
eve::Result<T> procgenBindingFailure(eve::DiagnosticCode code, std::string message,
                                     std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "procgen.squirrel"));
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
    for (const ArtifactId& dependency : artifact.dependencies)
        dependencies.emplace_back(dependency.format());

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
        result.emplace("partCount", eve::Value(static_cast<std::int64_t>(
            composite ? composite->children.size() : 0)));
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
    if (!reference)
        return eve::script::projectStatusResult(vm, reference.status(), false, false);
    const Ref ref = std::move(reference).takeValue();
    auto object = eve::script::makeOwnedSquirrelInstance<Proxy>(
        vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned procgen proxy");
        std::invoke(std::forward<Release>(release), ref).ignore(
            "rollback failed owned procgen allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(
        vm, eve::Status::success(eve::StatusCode::Applied), true, false);
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
                                     std::string("owned procgen ") + objectName +
                                         " handle is stale", objectName));
}

template <class Ref, class Release, class Stale>
ssq::Table makeAnyOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference,
                             Release&& release, Stale&& stale) {
    if (!reference)
        return eve::script::projectStatusResult(vm, reference.status(), false, false);
    const Ref ref = std::move(reference).takeValue();
    ProcgenAnyHandle any;
    any.ownerEpoch = ref.ownerEpoch;
    any.packedValue = ref.packed();
    any.release = [ref, release = std::forward<Release>(release)]() mutable {
        return std::invoke(release, ref);
    };
    any.stale = [ref, stale = std::forward<Stale>(stale)]() mutable {
        return std::invoke(stale, ref);
    };
    return makeOwnedProxy<ProcgenAnyHandle, ScriptProcgenHandle>(
        vm, eve::Result<ProcgenAnyHandle>::success(std::move(any)),
        [](ProcgenAnyHandle) { return eve::Result<void>::success(); });
}

/** @brief Projects one native recipe schema through the canonical Result table.
 *
 * The schema object is rooted by Squirrel and remains an owning value of the
 * returned `value` field. A failed lookup never returns an empty schema
 * object, so callers cannot mistake a missing recipe for valid metadata.
 */
ssq::Table projectRecipeDescriptorResult(HSQUIRRELVM vm,
                                          eve::Result<RecipeDescriptor>&& result) {
    const bool ok = result.ok();
    const eve::Status status = result.status();
    if (!ok) return eve::script::projectStatusResult(vm, status, false, false);

    auto descriptor = std::move(result).takeValue();
    auto object = eve::script::makeOwnedSquirrelInstance<RecipeDescriptor>(
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
eve::Result<eve::script::RuntimeHandleRef<Tag>> ownProcgenObject(
    eve::script::RuntimeObjectRegistry<T, Tag>& registry, eve::script::Owned<T> object) {
    return registry.emplace(std::move(object));
}

}  // namespace

struct Procgen::OwnershipState {
    eve::script::RuntimeObjectRegistry<OutputSpec, ProcgenOutputHandleTag> outputs;
    eve::script::RuntimeObjectRegistry<PointSet, ProcgenPointSetHandleTag> points;
    eve::script::RuntimeObjectRegistry<TerrainSampler, ProcgenTerrainSamplerHandleTag> samplers;
    eve::script::RuntimeObjectRegistry<Heightmap, ProcgenHeightmapHandleTag> heightmaps;
    eve::script::RuntimeObjectRegistry<CloudField, ProcgenCloudFieldHandleTag> clouds;
    eve::script::RuntimeObjectRegistry<CloudShadow, ProcgenCloudShadowHandleTag> shadows;
    eve::script::RuntimeObjectRegistry<PbrTextureSet, ProcgenPbrMaterialHandleTag> pbr;
    eve::script::RuntimeObjectRegistry<MeshBuild, ProcgenMeshBuildHandleTag> meshes;
    eve::script::RuntimeObjectRegistry<image::ImageData, ProcgenImageHandleTag> images;
    eve::script::RuntimeObjectRegistry<image::ImageData, ProcgenNormalImageHandleTag> normalImages;
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

eve::script::Borrowed<Params> Procgen::resolve(
    ProcgenParamsHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module) return {};
    return module->params_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenParamsHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "Procgen module is no longer loaded", "params");
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

eve::script::Borrowed<OutputSpec> Procgen::resolveOutput(
    ProcgenOutputHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return module ? module->ownership_->outputs.resolve(reference)
                  : eve::script::Borrowed<OutputSpec>();
}

eve::Result<void> Procgen::releaseOutput(ProcgenOutputHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "Procgen module is no longer loaded", "output");
    return module->ownership_->outputs.erase(reference);
}

bool Procgen::isOutputStale(ProcgenOutputHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->outputs.isStale(reference);
}

eve::Result<ProcgenGridHandleRef> Procgen::newGridHandle(int width, int height) {
    if (width <= 0 || height <= 0)
        return procgenBindingFailure<ProcgenGridHandleRef>(
            eve::DiagnosticCode::InvalidArgument,
            "procedural grid dimensions must be positive", "grid");
    auto grid = std::make_unique<Grid2D>();
    grid->resize(width, height);
    Procgen* module = Procgen::create();
    return module->grids_.emplace(std::move(grid));
}

eve::script::Borrowed<Grid2D> Procgen::resolve(
    ProcgenGridHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module) return {};
    return module->grids_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenGridHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "Procgen module is no longer loaded", "grid");
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

eve::script::Borrowed<PointSet> Procgen::resolvePointSet(
    ProcgenPointSetHandleRef reference) noexcept {
    return ownership_->points.resolve(reference);
}

eve::Result<void> Procgen::releasePointSet(ProcgenPointSetHandleRef reference) {
    return ownership_->points.erase(reference);
}

bool Procgen::isPointSetStale(ProcgenPointSetHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->points.isStale(reference);
}

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleGridHandle(
    int width, int depth, float spacing, uint32_t seed, float jitter) {
    if (width <= 0 || depth <= 0 || spacing <= 0.f)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument,
            "sampleGrid requires positive dimensions and spacing", "pointSet");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(sampleGridPoints(width, depth, spacing, seed, jitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterHeightHandle(
    ProcgenPointSetHandleRef input, float minHeight, float maxHeight) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "filterHeight input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointHeight(*view, minHeight, maxHeight)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterDensityHandle(
    ProcgenPointSetHandleRef input, float minDensity, float maxDensity) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "filterDensity input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointDensity(*view, minDensity, maxDensity)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterBoxHandle(
    ProcgenPointSetHandleRef input, float minX, float minY, float minZ, float maxX,
    float maxY, float maxZ, bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "filterBox input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(
                                                    filterPointBox(*view, minX, minY, minZ, maxX, maxY, maxZ, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSlopeHandle(
    ProcgenPointSetHandleRef input, float minDegrees, float maxDegrees) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "filterSlope input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointSlope(*view, minDegrees, maxDegrees)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterPolygonHandle(
    ProcgenPointSetHandleRef input, ProcgenPointSetHandleRef polygon, bool invert) {
    auto source = resolvePointSet(input);
    auto shape = resolvePointSet(polygon);
    if (!source.isBound() || !shape.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "filterPolygon input handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointsByPolygon(*source, *shape, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSplineDistanceHandle(
    ProcgenPointSetHandleRef input, ProcgenPointSetHandleRef controlPoints,
    float minDistance, float maxDistance) {
    auto source = resolvePointSet(input);
    auto control = resolvePointSet(controlPoints);
    if (!source.isBound() || !control.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "filterSplineDistance input handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(filterPointsBySplineDistance(
                                                    *source, *control, minDistance, maxDistance)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::excludeRadiusHandle(
    ProcgenPointSetHandleRef input, float x, float z, float radius) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "excludeRadius input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(excludePointRadius(*view, x, z, radius)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::jitterPointsHandle(
    ProcgenPointSetHandleRef input, uint32_t seed, float amountX, float amountZ) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "jitterPoints input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(jitterPointPositions(*view, seed, amountX, amountZ)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::selfPruneHandle(
    ProcgenPointSetHandleRef input, float radius) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "selfPrune input point-set handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(selfPrunePoints(*view, radius)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::projectToHeightmapHandle(
    ProcgenPointSetHandleRef input, ProcgenHeightmapHandleRef heightmap, float originX,
    float originZ, float cellSize, float heightScale) {
    auto points = resolvePointSet(input);
    auto map = resolveHeightmap(heightmap);
    if (!points.isBound() || !map.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "projectToHeightmap input handle is stale", "input");
    if (cellSize <= 0.f)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument, "projectToHeightmap cellSize must be positive", "cellSize");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(projectPointsToHeightmap(
                                                    *points, *map, originX, originZ, cellSize, heightScale)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleSplineHandle(
    ProcgenPointSetHandleRef controlPoints, float spacing, uint32_t seed, float lateralJitter) {
    auto control = resolvePointSet(controlPoints);
    if (!control.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "sampleSpline control point-set handle is stale", "controlPoints");
    if (spacing <= 0.f)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument, "sampleSpline spacing must be positive", "spacing");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(samplePolylinePoints(*control, spacing, seed, lateralJitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::mergePointsHandle(
    ProcgenPointSetHandleRef first, ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "mergePoints input handle is stale", "points");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(mergePointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::transformPointsHandle(
    ProcgenPointSetHandleRef input, float translateX, float translateY, float translateZ,
    float yawDegrees, float scaleX, float scaleY, float scaleZ) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "transformPoints input handle is stale", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(transformPointSet(*view, translateX, translateY, translateZ,
                                                                         yawDegrees, scaleX, scaleY, scaleZ)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterFloatAttributeHandle(
    ProcgenPointSetHandleRef input, const std::string& name, float minValue, float maxValue,
    bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound() || name.empty())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            name.empty() ? eve::DiagnosticCode::InvalidArgument : eve::DiagnosticCode::StaleHandle,
            "filterFloatAttribute requires a live input and attribute name", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(filterPointFloatAttribute(
                                                    *view, name, minValue, maxValue, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterStringAttributeHandle(
    ProcgenPointSetHandleRef input, const std::string& name, const std::string& value,
    bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound() || name.empty())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            name.empty() ? eve::DiagnosticCode::InvalidArgument : eve::DiagnosticCode::StaleHandle,
            "filterStringAttribute requires a live input and attribute name", "input");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointStringAttribute(*view, name, value, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::densityCullHandle(
    ProcgenPointSetHandleRef input, uint32_t seed, float multiplier) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "densityCull input handle is stale", "input");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(densityCullPoints(*view, seed, multiplier)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::poissonDiskHandle(
    int width, int depth, float radius, uint32_t seed, int maxPoints) {
    if (width < 0 || depth < 0 || radius <= 0.f || maxPoints < 0)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::InvalidArgument,
            "poissonDisk requires non-negative dimensions/count and a positive radius");
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(poissonDiskPoints(width, depth, radius, seed, maxPoints)));
}

eve::Result<void> Procgen::publishInstances(
    const std::string& batchId, ProcgenPointSetHandleRef points,
    const std::string& assetAttribute, const std::string& defaultAsset) {
    const auto view = resolvePointSet(points);
    if (batchId.empty() || !view.isBound())
        return procgenBindingFailure<void>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "publishInstances requires a batch id and a live point-set handle");
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                           "publishInstances scene sink is unavailable");

    std::vector<eve::ProcgenInstanceDesc> instances;
    instances.reserve(view->points().size());
    std::unordered_map<uint32_t, size_t> seedOccurrences;
    std::unordered_set<std::string>      instanceIds;
    for (const auto& point : view->points()) {
        eve::ProcgenInstanceDesc instance;
        const auto explicitId = point.stringAttributes.find("instanceId");
        if (explicitId != point.stringAttributes.end() && !explicitId->second.empty())
            instance.id = explicitId->second;
        else
            instance.id = "pcg-" + std::to_string(point.seed) + "-" +
                          std::to_string(seedOccurrences[point.seed]++);
        if (!instanceIds.insert(instance.id).second)
            return procgenBindingFailure<void>(eve::DiagnosticCode::Conflict,
                                               "publishInstances duplicate instance id: " + instance.id);
        instance.asset = defaultAsset;
        if (!assetAttribute.empty()) {
            const auto found = point.stringAttributes.find(assetAttribute);
            if (found != point.stringAttributes.end()) instance.asset = found->second;
        }
        instance.x   = point.x;
        instance.y   = point.y;
        instance.z   = point.z;
        instance.yaw = point.yaw;
        instance.scaleX = point.scaleX;
        instance.scaleY = point.scaleY;
        instance.scaleZ = point.scaleZ;
        instance.seed   = point.seed;
        instances.push_back(std::move(instance));
    }
    if (!sink->applyBatch(batchId, instances))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                           "publishInstances scene sink rejected batch");
    return eve::Result<void>::success();
}

eve::Result<void> Procgen::removeInstances(const std::string& batchId) {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (batchId.empty())
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "removeInstances requires a batch id");
    if (!sink)
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                           "removeInstances scene sink is unavailable");
    if (!sink->removeBatch(batchId))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                           "removeInstances scene sink rejected batch");
    return eve::Result<void>::success();
}

eve::Result<void> Procgen::publishCellInstances(
    const std::string& prefix, const ProcgenCellRequest& request,
    ProcgenPointSetHandleRef points, const std::string& assetAttribute,
    const std::string& defaultAsset) {
    if (prefix.empty())
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "publishCellInstances requires a prefix");
    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return publishInstances(batchId, points, assetAttribute, defaultAsset);
}

eve::Result<void> Procgen::removeCellInstances(
    const std::string& prefix, const ProcgenCellRequest& request) {
    if (prefix.empty())
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "removeCellInstances requires a prefix");
    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return removeInstances(batchId);
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

eve::Result<ProcgenSpatialDataHandleRef> Procgen::pointDataHandle(
    ProcgenPointSetHandleRef points) {
    auto view = resolvePointSet(points);
    if (!view.isBound())
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            eve::DiagnosticCode::StaleHandle, "pointData point-set handle is stale", "points");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::fromPoints(*view)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::boxVolumeHandle(
    float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    return spatialData_.emplace(std::make_unique<SpatialData>(
        SpatialData::box(minX, minY, minZ, maxX, maxY, maxZ)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::sphereVolumeHandle(
    float x, float y, float z, float radius) {
    if (radius <= 0.f)
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            eve::DiagnosticCode::InvalidArgument, "sphereVolume radius must be positive", "radius");
    return spatialData_.emplace(
        std::make_unique<SpatialData>(SpatialData::sphere(x, y, z, radius)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::splineDataHandle(
    ProcgenPointSetHandleRef controlPoints, float radius) {
    auto view = resolvePointSet(controlPoints);
    if (!view.isBound() || view->getCount() < 2 || radius < 0.f)
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "splineData requires a live set with at least two points and non-negative radius",
            "controlPoints");
    return spatialData_.emplace(
        std::make_unique<SpatialData>(SpatialData::spline(*view, radius)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::heightfieldDataHandle(
    ProcgenHeightmapHandleRef heightmap, float originX, float originZ, float cellSize,
    float heightScale) {
    auto view = resolveHeightmap(heightmap);
    if (!view.isBound() || view->getWidth() <= 0 || view->getHeight() <= 0 || cellSize <= 0.f)
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "heightfieldData requires a live non-empty heightmap and positive cell size", "heightmap");
    return spatialData_.emplace(std::make_unique<SpatialData>(
        SpatialData::heightfield(*view, originX, originZ, cellSize, heightScale)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::unionSpatialHandle(
    ProcgenSpatialDataHandleRef left, ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left); auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            eve::DiagnosticCode::StaleHandle, "unionSpatial input handle is stale", "spatial");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::unite(*a, *b)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::intersectSpatialHandle(
    ProcgenSpatialDataHandleRef left, ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left); auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            eve::DiagnosticCode::StaleHandle, "intersectSpatial input handle is stale", "spatial");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::intersect(*a, *b)));
}

eve::Result<ProcgenSpatialDataHandleRef> Procgen::differenceSpatialHandle(
    ProcgenSpatialDataHandleRef left, ProcgenSpatialDataHandleRef right) {
    auto a = resolveSpatialData(left); auto b = resolveSpatialData(right);
    if (!a.isBound() || !b.isBound())
        return procgenBindingFailure<ProcgenSpatialDataHandleRef>(
            eve::DiagnosticCode::StaleHandle, "differenceSpatial input handle is stale", "spatial");
    return spatialData_.emplace(std::make_unique<SpatialData>(SpatialData::subtract(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleSpatialHandle(
    ProcgenSpatialDataHandleRef spatial, float spacing, uint32_t seed, float jitter) {
    auto view = resolveSpatialData(spatial);
    if (!view.isBound() || spacing <= 0.f)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "sampleSpatial requires live spatial data and positive spacing", "spatial");
    return ownership_->points.emplace(
        std::make_unique<PointSet>(view->sample(spacing, seed, jitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSpatialHandle(
    ProcgenPointSetHandleRef input, ProcgenSpatialDataHandleRef spatial, bool invert) {
    auto points = resolvePointSet(input); auto domain = resolveSpatialData(spatial);
    if (!points.isBound() || !domain.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "filterSpatial input handle is stale", "input");
    return ownership_->points.emplace(
        std::make_unique<PointSet>(domain->filter(*points, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::projectToSpatialHandle(
    ProcgenPointSetHandleRef input, ProcgenSpatialDataHandleRef spatial) {
    auto points = resolvePointSet(input); auto domain = resolveSpatialData(spatial);
    if (!points.isBound() || !domain.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::StaleHandle, "projectToSpatial input handle is stale", "input");
    return ownership_->points.emplace(
        std::make_unique<PointSet>(domain->project(*points)));
}

eve::script::Borrowed<SpatialData> Procgen::resolveSpatialData(
    ProcgenSpatialDataHandleRef reference) noexcept { return spatialData_.resolve(reference); }

eve::Result<void> Procgen::release(ProcgenSpatialDataHandleRef reference) {
    return spatialData_.erase(reference);
}

bool Procgen::isStale(ProcgenSpatialDataHandleRef reference) const noexcept {
    return spatialData_.isStale(reference);
}

eve::Result<ProcgenRuntimeGenerationHandleRef> Procgen::newRuntimeGenerationHandle(
    uint32_t worldSeed) {
    return runtimeGenerations_.emplace(std::make_unique<RuntimeGeneration>(worldSeed));
}

eve::Result<ProcgenLSystemHandleRef> Procgen::newLSystemHandle() {
    return lsystems_.emplace(std::make_unique<LSystem>());
}

eve::script::Borrowed<RuntimeGeneration> Procgen::resolveRuntimeGeneration(
    ProcgenRuntimeGenerationHandleRef reference) noexcept {
    return runtimeGenerations_.resolve(reference);
}

eve::script::Borrowed<PointGraph> Procgen::resolvePointGraph(
    ProcgenPointGraphHandleRef reference) noexcept { return pointGraphs_.resolve(reference); }

eve::script::Borrowed<BiomeRules> Procgen::resolveBiomeRules(
    ProcgenBiomeRulesHandleRef reference) noexcept { return biomeRules_.resolve(reference); }

eve::script::Borrowed<ShapeGrammar> Procgen::resolveShapeGrammar(
    ProcgenShapeGrammarHandleRef reference) noexcept { return shapeGrammars_.resolve(reference); }

eve::script::Borrowed<LSystem> Procgen::resolveLSystem(
    ProcgenLSystemHandleRef reference) noexcept { return lsystems_.resolve(reference); }

eve::Result<void> Procgen::release(ProcgenRuntimeGenerationHandleRef reference) {
    return runtimeGenerations_.erase(reference);
}
eve::Result<void> Procgen::release(ProcgenPointGraphHandleRef reference) {
    return pointGraphs_.erase(reference);
}
eve::Result<void> Procgen::release(ProcgenBiomeRulesHandleRef reference) {
    return biomeRules_.erase(reference);
}
eve::Result<void> Procgen::release(ProcgenShapeGrammarHandleRef reference) {
    return shapeGrammars_.erase(reference);
}
eve::Result<void> Procgen::release(ProcgenLSystemHandleRef reference) {
    return lsystems_.erase(reference);
}

bool Procgen::isStale(ProcgenRuntimeGenerationHandleRef reference) const noexcept {
    return runtimeGenerations_.isStale(reference);
}
bool Procgen::isStale(ProcgenPointGraphHandleRef reference) const noexcept {
    return pointGraphs_.isStale(reference);
}
bool Procgen::isStale(ProcgenBiomeRulesHandleRef reference) const noexcept {
    return biomeRules_.isStale(reference);
}
bool Procgen::isStale(ProcgenShapeGrammarHandleRef reference) const noexcept {
    return shapeGrammars_.isStale(reference);
}
bool Procgen::isStale(ProcgenLSystemHandleRef reference) const noexcept {
    return lsystems_.isStale(reference);
}

uint32_t Procgen::deriveSeed(uint32_t parent, const std::string& scope) const {
    return eve::procgen::deriveSeed(parent, scope);
}

eve::Result<ProcgenContextHandleRef> Procgen::beginSystemHandle(
    const std::string& name, uint32_t seed) {
    Procgen* module = Procgen::create();
    module->lastError_.clear();

    if (name.empty()) {
        module->lastError_ = "beginSystem: name is empty";
        return procgenBindingFailure<ProcgenContextHandleRef>(
            eve::DiagnosticCode::InvalidArgument, module->lastError_, "context");
    }
    auto context = std::make_unique<ProcgenContext>(name, seed);
    const auto previous = module->systems_.find(name);
    if (previous != module->systems_.end()) context->stageCache_ = previous->second.stageCache;
    return module->contexts_.emplace(std::move(context));
}

eve::Result<ProcgenContextHandleRef> Procgen::beginCachedSystemHandle(
    const std::string& name, uint32_t seed, const std::string& buildKey) {
    Procgen* module = Procgen::create();
    module->lastError_.clear();
    if (name.empty()) {
        module->lastError_ = "beginCachedSystem: name is empty";
        return procgenBindingFailure<ProcgenContextHandleRef>(
            eve::DiagnosticCode::InvalidArgument, module->lastError_, "context");
    }
    if (buildKey.empty()) {
        module->lastError_ = "beginCachedSystem: build key is empty";
        return procgenBindingFailure<ProcgenContextHandleRef>(
            eve::DiagnosticCode::InvalidArgument, module->lastError_, "buildKey");
    }
    const uint32_t normalizedSeed = seed ? seed : 1u;
    const auto     found          = module->systems_.find(name);
    const bool     cacheHit =
        found != module->systems_.end() && found->second.seed == normalizedSeed && found->second.buildKey == buildKey;
    auto context = std::make_unique<ProcgenContext>(name, normalizedSeed, buildKey, cacheHit);
    if (found != module->systems_.end()) context->stageCache_ = found->second.stageCache;
    return module->contexts_.emplace(std::move(context));
}

eve::script::Borrowed<ProcgenContext> Procgen::resolve(
    ProcgenContextHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module) return {};
    return module->contexts_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenContextHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "Procgen module is no longer loaded", "context");
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
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "commitSystem context handle is stale", "context");
    ProcgenContext* context = view.get();
    if (!context->isActive())
        return procgenBindingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                           "commitSystem: transaction is closed", "context");
    if (context->hasFailed()) {
        const std::string error = context->getError();
        context->close();
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                           "commitSystem: " + error, "context");
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
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "abortSystem context handle is stale", "context");
    view->abort();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::removeSystem(const std::string& name) {
    previousSystems_.erase(name);
    if (systems_.erase(name) == 0)
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound,
                                           "procgen system was not committed", "system");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
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

eve::Result<ProcgenPointSetHandleRef> Procgen::getSystemOutputHandle(
    const std::string& name, const std::string& outputName) const {
    const auto system = systems_.find(name);
    if (system == systems_.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::NotFound, "procgen system is not committed", "system");
    const auto output = system->second.outputs.find(outputName);
    if (output == system->second.outputs.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::NotFound, "procgen system output was not found", "output");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(output->second));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::getSystemDebugStageHandle(
    const std::string& name, const std::string& stageName) const {
    const auto system = systems_.find(name);
    if (system == systems_.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::NotFound, "procgen system is not committed", "system");
    const auto stage = system->second.debugStages.find(stageName);
    if (stage == system->second.debugStages.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::NotFound, "procgen debug stage was not found", "stage");
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(stage->second));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::getPreviousSystemDebugStageHandle(
    const std::string& name, const std::string& stageName) const {
    const auto system = previousSystems_.find(name);
    if (system == previousSystems_.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::NotFound, "procgen system has no previous revision", "system");
    const auto stage = system->second.debugStages.find(stageName);
    if (stage == system->second.debugStages.end())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(
            eve::DiagnosticCode::NotFound, "previous procgen debug stage was not found", "stage");
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

eve::Result<ProcgenGridHandleRef> Procgen::generateHandle(
    const std::string& algorithmId, ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenGridHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generate parameters handle is stale", "params");
    auto grid = std::make_unique<Grid2D>();
    if (!runGenerate(algorithmId, *input, *grid))
        return procgenBindingFailure<ProcgenGridHandleRef>(
            eve::DiagnosticCode::Failed, lastError_.empty() ? "generate failed" : lastError_, "algorithm");
    return ownProcgenObject(grids_, std::move(grid));
}

eve::Result<void> Procgen::generateTo(const std::string &algorithmId,
                                      ProcgenParamsHandleRef params,
                                      ProcgenOutputHandleRef output) {
    auto paramsView = Procgen::resolve(params);
    auto outputView = resolveOutput(output);
    if (!paramsView.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "generateTo parameter handle is stale", "params");
    if (!outputView.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "generateTo output handle is stale", "output");
    Grid2D grid;
    if (!runGenerate(algorithmId, *paramsView, grid))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                           lastError_.empty() ? "generateTo failed" : lastError_,
                                           "algorithm");

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

eve::Result<void> Procgen::applyToLayer(ProcgenGridHandleRef grid,
                                        const std::string &palette,
                                        map::TileLayer& layer) {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "applyToLayer grid handle is stale", "grid");
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

eve::Result<RecipeDescriptor> Procgen::getAlgorithmSchema(const std::string &algorithmId) const {
    const RecipeDescriptor *schema = GeneratorRegistry::instance().descriptor(algorithmId);
    if (!schema)
        return procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::NotFound,
                                                       "algorithm schema was not found", "algorithm");
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

eve::Result<void> Procgen::applyAlgorithmDefaults(const std::string &algorithmId,
                                                  ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "algorithm default parameters handle is stale", "params");
    if (!GeneratorRegistry::instance().applyDefaults(algorithmId, *view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound,
                                           "algorithm schema was not found", "algorithm");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::autotileGrid(ProcgenGridHandleRef grid) {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "autotileGrid grid handle is stale", "grid");
    if (!eve::procgen::autotileGridInPlace(*view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::Failed,
                                           "autotileGrid failed", "grid");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

uint32_t Procgen::randomSeed() { return eve::procgen::randomSeedValue(); }

eve::Result<std::string> Procgen::gridToJson(ProcgenGridHandleRef grid) const {
    auto view = Procgen::resolve(grid);
    if (!view.isBound())
        return procgenBindingFailure<std::string>(eve::DiagnosticCode::StaleHandle,
                                                  "gridToJson grid handle is stale", "grid");
    return eve::Result<std::string>::success(eve::procgen::gridToJson(*view));
}

eve::Result<ProcgenImageHandleRef> Procgen::generateImageHandle(
    const std::string& recipeId, ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenImageHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generateImage parameters handle is stale", "params");
    lastError_.clear();
    TextureRecipeRegistry::instance().registerBuiltins();
    auto image = TextureRecipeRegistry::instance().generate(recipeId, *input, lastError_);
    if (!image)
        return procgenBindingFailure<ProcgenImageHandleRef>(
            eve::DiagnosticCode::Failed, lastError_.empty() ? "generateImage failed" : lastError_, "recipe");
    return ownProcgenObject(ownership_->images, std::move(image));
}

eve::script::Borrowed<image::ImageData> Procgen::resolve(
    ProcgenImageHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return module ? module->ownership_->images.resolve(reference)
                  : eve::script::Borrowed<image::ImageData>();
}

eve::Result<void> Procgen::release(ProcgenImageHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "Procgen module is no longer loaded", "image");
    return module->ownership_->images.erase(reference);
}

bool Procgen::isStale(ProcgenImageHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return !module || module->ownership_->images.isStale(reference);
}

eve::Result<ProcgenNormalImageHandleRef> Procgen::generateNormalImageHandle(
    const std::string& recipeId, ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenNormalImageHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generateNormalImage parameters handle is stale", "params");
    auto image = generateImageHandle(recipeId, params);
    if (!image)
        return procgenBindingFailure<ProcgenNormalImageHandleRef>(
            eve::DiagnosticCode::Failed, image.status().describe(), "recipe");
    const auto imageRef = std::move(image).takeValue();
    auto albedo = ownership_->images.resolve(imageRef);
    if (!albedo.isBound()) {
        ownership_->images.erase(imageRef).ignore("release unresolvable temporary albedo image");
        return procgenBindingFailure<ProcgenNormalImageHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generated albedo image handle is stale", "image");
    }
    const int w = albedo->getWidth();
    const int h = albedo->getHeight();
    auto* px = static_cast<const uint8_t*>(albedo->getData());
    std::vector<float> height(size_t(w * h));
    for (int i = 0; i < w * h; ++i) {
        const size_t o = size_t(i) * 4u;
        height[size_t(i)] = (float(px[o]) * 0.299f + float(px[o + 1]) * 0.587f +
                             float(px[o + 2]) * 0.114f) / 255.f;
    }
    const bool seamless = input->getInt("seamless", 1) != 0;
    const float strength = input->getFloat("normalStrength", 4.f);
    auto normal = heightToNormalImage(height, w, h, strength, seamless);
    ownership_->images.erase(imageRef).ignore("release temporary albedo image");
    if (!normal)
        return procgenBindingFailure<ProcgenNormalImageHandleRef>(
            eve::DiagnosticCode::Failed, "generateNormalImage failed", "recipe");
    return ownProcgenObject(ownership_->normalImages,
                            std::move(normal));
}

eve::script::Borrowed<image::ImageData> Procgen::resolve(
    ProcgenNormalImageHandleRef reference) noexcept {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return module ? module->ownership_->normalImages.resolve(reference)
                  : eve::script::Borrowed<image::ImageData>();
}

eve::Result<void> Procgen::release(ProcgenNormalImageHandleRef reference) {
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    if (!module)
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "Procgen module is no longer loaded", "normalImage");
    return module->ownership_->normalImages.erase(reference);
}

bool Procgen::isStale(ProcgenNormalImageHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = ModuleManager::getInstance<Procgen>("Procgen");
    return !module || module->ownership_->normalImages.isStale(reference);
}

eve::script::Borrowed<graphics::Texture> Procgen::generateTextureBorrowed(
    const std::string& recipeId, ProcgenParamsHandleRef params, graphics::Graphics* gfx) {
    auto input = Procgen::resolve(params);
    if (!input.isBound() || !gfx) return {};
    auto generated = generateImageHandle(recipeId, params);
    if (!generated) return {};
    const auto imageRef = std::move(generated).takeValue();
    auto image = ownership_->images.resolve(imageRef);
    if (!image.isBound()) {
        ownership_->images.erase(imageRef).ignore("release unresolvable temporary image");
        return {};
    }
    const bool seamless = input->getInt("seamless", 1) != 0;
    graphics::Texture* texture = gfx->newTexture(
        image->getWidth(), image->getHeight(), static_cast<const uint8_t*>(image->getData()),
        seamless, seamless);
    ownership_->images.erase(imageRef).ignore("release temporary generated image");
    return eve::script::Borrowed<graphics::Texture>(
        texture, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(gfx)));
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

eve::Result<RecipeDescriptor> Procgen::getTextureRecipeSchema(const std::string &recipeId) const {
    TextureRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor *schema = TextureRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::NotFound,
                                                       "texture recipe schema was not found", "recipe");
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyTextureRecipeDefaults(
    const std::string &recipeId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "texture default parameters handle is stale", "params");
    TextureRecipeRegistry::instance().registerBuiltins();
    if (!TextureRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound,
                                           "texture recipe schema was not found", "recipe");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenCloudFieldHandleRef> Procgen::newCloudFieldHandle() {
    return ownProcgenObject(ownership_->clouds, std::make_unique<CloudField>());
}

eve::script::Borrowed<CloudField> Procgen::resolveCloudField(
    ProcgenCloudFieldHandleRef reference) noexcept {
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

eve::script::Borrowed<CloudShadow> Procgen::resolveCloudShadow(
    ProcgenCloudShadowHandleRef reference) noexcept {
    return ownership_->shadows.resolve(reference);
}

eve::Result<void> Procgen::releaseCloudShadow(ProcgenCloudShadowHandleRef reference) {
    return ownership_->shadows.erase(reference);
}

bool Procgen::isCloudShadowStale(ProcgenCloudShadowHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->shadows.isStale(reference);
}

eve::Result<float> Procgen::cloudCoverageAt(
    ProcgenCloudFieldHandleRef field, float x, float z, float time) {
    auto view = resolveCloudField(field);
    if (!view.isBound())
        return procgenBindingFailure<float>(eve::DiagnosticCode::StaleHandle,
                                            "cloud field handle is stale", "field");
    return eve::Result<float>::success(view->coverageAt(x, z, time));
}

eve::Result<float> Procgen::cloudShadowFactor(
    ProcgenCloudShadowHandleRef shadow, float x, float z, float time) {
    auto view = resolveCloudShadow(shadow);
    if (!view.isBound())
        return procgenBindingFailure<float>(eve::DiagnosticCode::StaleHandle,
                                            "cloud shadow handle is stale", "shadow");
    return eve::Result<float>::success(view->shadowFactorAt(x, z, time));
}

eve::Result<void> Procgen::sampleCloud(
    ProcgenCloudFieldHandleRef field, std::span<float> out, int w, int h, float time,
    float x0, float z0, float extent) {
    auto view = resolveCloudField(field);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "cloud field handle is stale", "field");
    if (w <= 0 || h <= 0 || out.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "cloud output buffer is smaller than width*height", "out");
    view->sample(out.data(), w, h, time, x0, z0, extent);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Procgen::sampleCloudShadow(
    ProcgenCloudShadowHandleRef shadow, std::span<float> out, int w, int h, float time,
    float x0, float z0, float extent) {
    auto view = resolveCloudShadow(shadow);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "cloud shadow handle is stale", "shadow");
    if (w <= 0 || h <= 0 || out.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
        return procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "cloud output buffer is smaller than width*height", "out");
    view->sampleCoverage(out.data(), w, h, time, x0, z0, extent);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenPbrMaterialHandleRef> Procgen::generatePbrMaterialHandle(
    const std::string& recipeId, ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenPbrMaterialHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generatePbrMaterial parameters handle is stale", "params");
    lastError_.clear();
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    auto set = PbrRecipeRegistry::instance().generate(recipeId, *input, lastError_);
    if (!set)
        return procgenBindingFailure<ProcgenPbrMaterialHandleRef>(
            eve::DiagnosticCode::Failed,
            lastError_.empty() ? "generatePbrMaterial failed" : lastError_, "recipe");
    return ownProcgenObject(ownership_->pbr,
                            std::move(set));
}

eve::script::Borrowed<PbrTextureSet> Procgen::resolvePbrMaterial(
    ProcgenPbrMaterialHandleRef reference) noexcept {
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

eve::Result<RecipeDescriptor> Procgen::getPbrRecipeSchema(const std::string &recipeId) const {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    const RecipeDescriptor *schema = PbrRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::NotFound,
                                                       "PBR recipe schema was not found", "recipe");
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyPbrRecipeDefaults(
    const std::string &recipeId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "PBR default parameters handle is stale", "params");
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    if (!PbrRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound,
                                           "PBR recipe schema was not found", "recipe");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ProcgenMeshBuildHandleRef> Procgen::buildMeshHandle(
    const std::string& recipeId, ProcgenParamsHandleRef params) {
    auto built = buildArtifact(recipeId, params, nextCompatibilityArtifactId());
    if (!built)
        return procgenBindingFailure<ProcgenMeshBuildHandleRef>(
            eve::DiagnosticCode::Failed, built.status().describe(), "recipe");
    GeneratedArtifact artifact = std::move(built).takeValue();
    const MeshBuild* source = nullptr;
    if (artifact.type == ArtifactType::MeshData) {
        source = &std::get<MeshData>(artifact.payload);
    } else if (artifact.type == ArtifactType::Composite) {
        const ArtifactPart* part = std::get<CompositeArtifact>(artifact.payload).find("mesh");
        if (part && part->type == ArtifactType::MeshData)
            source = &std::get<MeshData>(part->payload);
    }
    if (!source)
        return procgenBindingFailure<ProcgenMeshBuildHandleRef>(
            eve::DiagnosticCode::Failed, "generated artifact has no mesh payload", "recipe");
    auto mesh = std::make_unique<MeshBuild>(*source);
    ArtifactPublisher publisher(artifactStore_);
    auto published = publisher.publish(std::move(artifact), {});
    if (!published)
        return procgenBindingFailure<ProcgenMeshBuildHandleRef>(
            eve::DiagnosticCode::Failed, published.status().describe(), "artifact");
    std::move(published).takeValue();
    return ownProcgenObject(ownership_->meshes, std::move(mesh));
}

eve::script::Borrowed<MeshBuild> Procgen::resolveMeshBuild(
    ProcgenMeshBuildHandleRef reference) noexcept {
    return ownership_->meshes.resolve(reference);
}

eve::Result<void> Procgen::releaseMeshBuild(ProcgenMeshBuildHandleRef reference) {
    return ownership_->meshes.erase(reference);
}

bool Procgen::isMeshBuildStale(ProcgenMeshBuildHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->meshes.isStale(reference);
}

eve::Result<GeneratedArtifact> Procgen::buildArtifact(
    const std::string &recipeId, ProcgenParamsHandleRef params, ArtifactId id) {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<GeneratedArtifact>(
            eve::DiagnosticCode::StaleHandle, "buildArtifact parameters handle is stale", "params");
    return generateMeshArtifact(recipeId, *view, id);
}

eve::Result<ArtifactPublishReceipt> Procgen::publishArtifact(
    const std::string &recipeId, ProcgenParamsHandleRef params, ArtifactId id,
    ArtifactPublishOptions options) {
    auto artifact = buildArtifact(recipeId, params, id);
    if (!artifact.ok())
        return eve::Result<ArtifactPublishReceipt>::failure(artifact.status());
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

eve::script::Borrowed<graphics::Mesh> Procgen::generateMeshBorrowed(
    const std::string& recipeId, ProcgenParamsHandleRef params, graphics::Graphics* gfx) {
    auto input = Procgen::resolve(params);
    if (!input.isBound() || !gfx) return {};
    auto built = buildMeshHandle(recipeId, params);
    if (!built) return {};
    auto cpu = ownership_->meshes.resolve(std::move(built).takeValue());
    if (!cpu.isBound()) return {};
    return uploadMeshBorrowed(*cpu, *gfx);
}

eve::script::Borrowed<graphics::Mesh> Procgen::uploadMeshBorrowed(
    const MeshBuild& mesh, graphics::Graphics& gfx) {
    if (mesh.empty()) return {};
    graphics::Mesh* uploaded = gfx.newMeshFromArrays(
        mesh.positions().data(), mesh.normals().data(), mesh.uvs().data(),
        mesh.getVertexCount(), mesh.indices().data(), mesh.getIndexCount());
    return eve::script::Borrowed<graphics::Mesh>(
        uploaded, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&gfx)));
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

eve::Result<RecipeDescriptor> Procgen::getMeshRecipeSchema(const std::string &recipeId) const {
    MeshRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor *schema = MeshRecipeRegistry::instance().descriptor(recipeId);
    if (!schema)
        return procgenBindingFailure<RecipeDescriptor>(eve::DiagnosticCode::NotFound,
                                                       "mesh recipe schema was not found", "recipe");
    return eve::Result<RecipeDescriptor>::success(*schema);
}

eve::Result<void> Procgen::applyMeshRecipeDefaults(
    const std::string &recipeId, ProcgenParamsHandleRef params) const {
    auto view = Procgen::resolve(params);
    if (!view.isBound())
        return procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                           "mesh default parameters handle is stale", "params");
    MeshRecipeRegistry::instance().registerBuiltins();
    if (!MeshRecipeRegistry::instance().applyDefaults(recipeId, *view))
        return procgenBindingFailure<void>(eve::DiagnosticCode::NotFound,
                                           "mesh recipe schema was not found", "recipe");
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
        return procgenBindingFailure<ProcgenHeightmapHandleRef>(
            eve::DiagnosticCode::InvalidArgument, "heightmap dimensions must be positive", "heightmap");
    return ownProcgenObject(ownership_->heightmaps,
                            std::make_unique<Heightmap>(width, height));
}

eve::script::Borrowed<Heightmap> Procgen::resolveHeightmap(
    ProcgenHeightmapHandleRef reference) noexcept {
    return ownership_->heightmaps.resolve(reference);
}

eve::Result<void> Procgen::releaseHeightmap(ProcgenHeightmapHandleRef reference) {
    return ownership_->heightmaps.erase(reference);
}

bool Procgen::isHeightmapStale(ProcgenHeightmapHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->heightmaps.isStale(reference);
}

eve::Result<ProcgenHeightmapHandleRef> Procgen::generateHeightmapHandle(
    ProcgenParamsHandleRef params) {
    auto input = Procgen::resolve(params);
    if (!input.isBound())
        return procgenBindingFailure<ProcgenHeightmapHandleRef>(
            eve::DiagnosticCode::StaleHandle, "generateHeightmap parameters handle is stale", "params");
    const TerrainSampler sampler = TerrainSampler::fromParams(*input);
    return ownProcgenObject(ownership_->heightmaps, std::make_unique<Heightmap>(
        Heightmap::generate(sampler, input->getWidth(), input->getHeight())));
}

eve::Result<ProcgenGridHandleRef> Procgen::heightmapToGrid(
    ProcgenHeightmapHandleRef heightmap, ProcgenParamsHandleRef params) {
    auto map = resolveHeightmap(heightmap);
    auto input = Procgen::resolve(params);
    if (!map.isBound())
        return procgenBindingFailure<ProcgenGridHandleRef>(
            eve::DiagnosticCode::StaleHandle, "heightmap handle is stale", "heightmap");
    if (!input.isBound())
        return procgenBindingFailure<ProcgenGridHandleRef>(
            eve::DiagnosticCode::StaleHandle, "heightmap parameters handle is stale", "params");
    auto grid = std::make_unique<Grid2D>();
    const TerrainBands bands = TerrainBands::fromParams(*input);
    if (!map->toGrid(*grid, bands))
        return procgenBindingFailure<ProcgenGridHandleRef>(
            eve::DiagnosticCode::Failed, "heightmap is empty", "heightmap");
    Procgen* module = Procgen::create();
    return module->grids_.emplace(std::move(grid));
}

void Procgen::expose(ssq::Table &table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto cls = table.addClass(name, Procgen::create, false);
    expose(cls);
    exposeBiomeRules(table);
    exposePointGraph(table);
    exposeShapeGrammar(table);

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
    params.addFunc("setBool", &Params::setBool);
    params.addFunc("setString", &Params::setString);
    params.addFunc("has", &Params::has);
    params.addFunc("getInt", &Params::getInt);
    params.addFunc("getFloat", &Params::getFloat);
    params.addFunc("getBool", &Params::getBool);
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

auto lsystem = table.addClass<LSystem>(
        "ProcgenLSystem", std::function<LSystem*()>([]() -> LSystem* { return nullptr; }), true);
    lsystem.addFunc("setAxiom", &LSystem::setAxiom);
    lsystem.addFunc("addRule", &LSystem::addRule);
    lsystem.addFunc("addRules",
                    [](LSystem* ls, char symbol, ssq::Array productions, ssq::Array weights) {
                        ls->addRules(symbol, productions.convert<std::string>(),
                                     weights.convert<float>());
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
        "ProcgenSpatialData",
        std::function<SpatialData*()>([]() -> SpatialData* { return nullptr; }), true);
    spatial.addFunc("getKind", &SpatialData::getKind);
    spatial.addFunc("contains", &SpatialData::contains);
    spatial.addFunc("hasBounds", &SpatialData::hasBounds);
    spatial.addFunc("getMinX", &SpatialData::getMinX);
    spatial.addFunc("getMinY", &SpatialData::getMinY);
    spatial.addFunc("getMinZ", &SpatialData::getMinZ);
    spatial.addFunc("getMaxX", &SpatialData::getMaxX);
    spatial.addFunc("getMaxY", &SpatialData::getMaxY);
    spatial.addFunc("getMaxZ", &SpatialData::getMaxZ);

    auto cellRequest = table.addClass<ProcgenCellRequest>(
        "ProcgenCellRequest",
        std::function<ProcgenCellRequest*()>([]() -> ProcgenCellRequest* { return nullptr; }),
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
        "ProcgenRuntimeGeneration",
        std::function<RuntimeGeneration*()>([]() -> RuntimeGeneration* { return nullptr; }), true);
    runtimeGeneration.addFunc("clear", &RuntimeGeneration::clear);
    runtimeGeneration.addFunc("addLevel", &RuntimeGeneration::addLevel);
    runtimeGeneration.addFunc("getLevelCount", &RuntimeGeneration::getLevelCount);
    runtimeGeneration.addFunc("getLevelCellSize", &RuntimeGeneration::getLevelCellSize);
    runtimeGeneration.addFunc("getLevelGenerationRadius",
                              &RuntimeGeneration::getLevelGenerationRadius);
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
    runtimeGeneration.addFunc("getResidentPointCount",
                              &RuntimeGeneration::getResidentPointCount);
    runtimeGeneration.addFunc("getRejectedOutputCount",
                              &RuntimeGeneration::getRejectedOutputCount);
    runtimeGeneration.addFunc("trimToResidentPoints", &RuntimeGeneration::trimToResidentPoints);
    runtimeGeneration.addFunc("setMaxGenerationRetries",
                              &RuntimeGeneration::setMaxGenerationRetries);
    runtimeGeneration.addFunc("getMaxGenerationRetries",
                              &RuntimeGeneration::getMaxGenerationRetries);
    runtimeGeneration.addFunc("setFrameTimeBudget", &RuntimeGeneration::setFrameTimeBudget);
    runtimeGeneration.addFunc("getFrameTimeBudget", &RuntimeGeneration::getFrameTimeBudget);
    runtimeGeneration.addFunc("beginFrame", &RuntimeGeneration::beginFrame);
    runtimeGeneration.addFunc("updateSource", &RuntimeGeneration::updateSource);
    runtimeGeneration.addFunc("setGenerationSource", &RuntimeGeneration::setGenerationSource);
    runtimeGeneration.addFunc("removeGenerationSource",
                              &RuntimeGeneration::removeGenerationSource);
    runtimeGeneration.addFunc("clearGenerationSources",
                              &RuntimeGeneration::clearGenerationSources);
    runtimeGeneration.addFunc("getGenerationSourceCount",
                              &RuntimeGeneration::getGenerationSourceCount);
    runtimeGeneration.addFunc("getGenerationSourceId",
                              &RuntimeGeneration::getGenerationSourceId);
    runtimeGeneration.addFunc("refreshGenerationSources",
                              &RuntimeGeneration::refreshGenerationSources);
    runtimeGeneration.addFunc("setFrustumCulling", &RuntimeGeneration::setFrustumCulling);
    runtimeGeneration.addFunc("isFrustumCullingEnabled",
                              &RuntimeGeneration::isFrustumCullingEnabled);
    runtimeGeneration.addFunc("getFrustumHalfAngle",
                              &RuntimeGeneration::getFrustumHalfAngle);
    runtimeGeneration.addFunc("getFrustumBehindRadius",
                              &RuntimeGeneration::getFrustumBehindRadius);
    runtimeGeneration.addFunc("getPendingGenerateCount",
                              &RuntimeGeneration::getPendingGenerateCount);
    runtimeGeneration.addFunc("getGeneratingCount", &RuntimeGeneration::getGeneratingCount);
    runtimeGeneration.addFunc("getActiveCellCount", &RuntimeGeneration::getActiveCellCount);
    runtimeGeneration.addFunc("getPendingCleanupCount",
                              &RuntimeGeneration::getPendingCleanupCount);
    runtimeGeneration.addFunc("getFailedCellCount", &RuntimeGeneration::getFailedCellCount);
    runtimeGeneration.addFunc("retryFailedCells", &RuntimeGeneration::retryFailedCells);
    runtimeGeneration.addFunc("nextGenerate", &RuntimeGeneration::nextGenerate);
    runtimeGeneration.addFunc("nextCleanup", &RuntimeGeneration::nextCleanup);
    runtimeGeneration.addFunc("completeGeneration", &RuntimeGeneration::completeGeneration);
    runtimeGeneration.addFunc("failGeneration", &RuntimeGeneration::failGeneration);
    runtimeGeneration.addFunc("completeCleanup", &RuntimeGeneration::completeCleanup);
    runtimeGeneration.addFunc("hasCell", &RuntimeGeneration::hasCell);
    runtimeGeneration.addFunc("getCellOutput", &RuntimeGeneration::getCellOutput);
    runtimeGeneration.addFunc("getCellRevision", &RuntimeGeneration::getCellRevision);
    runtimeGeneration.addFunc("serializeCell", &RuntimeGeneration::serializeCell);
    runtimeGeneration.addFunc("deserializeCell", &RuntimeGeneration::deserializeCell);
    runtimeGeneration.addFunc("debugReport", &RuntimeGeneration::debugReport);

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

    auto ownedParams = table.addClass<ScriptProcgenParams>(
        "ProcgenOwnedParams",
        std::function<ScriptProcgenParams*()>([] { return nullptr; }), true);
    ownedParams.addFunc("ownership", [](ScriptProcgenParams*) { return std::string("owned"); });
    ownedParams.addFunc("ownerEpoch", [](ScriptProcgenParams* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedParams.addFunc("handle", [](ScriptProcgenParams* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedParams.addFunc("isStale", [](ScriptProcgenParams* value) {
        return !value || Procgen::isStale(value->reference);
    });
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
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setSize", [vm](ScriptProcgenParams* value, int width, int height) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setSize(width, height);
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setInt", [vm](ScriptProcgenParams* value, const std::string& key, int number) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setInt(key, number);
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setFloat", [vm](ScriptProcgenParams* value, const std::string& key, float number) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setFloat(key, number);
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setBool", [vm](ScriptProcgenParams* value, const std::string& key, bool flag) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setBool(key, flag);
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedParams.addFunc("setString", [vm](ScriptProcgenParams* value, const std::string& key,
                                            const std::string& text) {
        if (!value) return staleProcgenResult<void>(vm, "params");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "params");
        view->setString(key, text);
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
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
    ownedParams.addFunc("canonicalString", [](ScriptProcgenParams* value) {
        auto view = value ? Procgen::resolve(value->reference) : eve::script::Borrowed<Params>();
        return view.isBound() ? view->canonicalString() : std::string{};
    });

    auto ownedHandle = table.addClass<ScriptProcgenHandle>(
        "ProcgenOwnedHandle", std::function<ScriptProcgenHandle*()>([] { return nullptr; }), true);
    ownedHandle.addFunc("ownership", [](ScriptProcgenHandle*) { return std::string("owned"); });
    ownedHandle.addFunc("ownerEpoch", [](ScriptProcgenHandle* value) {
        return value ? static_cast<int64_t>(value->ownerEpoch) : int64_t{0};
    });
    ownedHandle.addFunc("handle", [](ScriptProcgenHandle* value) {
        return value ? static_cast<int64_t>(value->packed) : int64_t{0};
    });
    ownedHandle.addFunc("isStale", [](ScriptProcgenHandle* value) {
        return !value || (value->stale && value->stale());
    });
    ownedHandle.addFunc("release", [vm](ScriptProcgenHandle* value) {
        if (!value)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                 "owned procgen handle must not be null", "handle"));
        if (value->released)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                 "owned procgen handle was already released", "handle"));
        value->released = true;
        return eve::script::projectResult(
            vm, value->release ? value->release() : eve::Result<void>::success());
    });

    auto ownedGrid = table.addClass<ScriptProcgenGrid>(
        "ProcgenOwnedGrid2D",
        std::function<ScriptProcgenGrid*()>([] { return nullptr; }), true);
    ownedGrid.addFunc("ownership", [](ScriptProcgenGrid*) { return std::string("owned"); });
    ownedGrid.addFunc("ownerEpoch", [](ScriptProcgenGrid* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedGrid.addFunc("handle", [](ScriptProcgenGrid* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedGrid.addFunc("isStale", [](ScriptProcgenGrid* value) {
        return !value || Procgen::isStale(value->reference);
    });
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
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedGrid.addFunc("fill", [vm](ScriptProcgenGrid* value, int semantic) {
        if (!value) return staleProcgenResult<void>(vm, "grid");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "grid");
        view->fill(semantic);
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    ownedGrid.addFunc("setCell", [vm](ScriptProcgenGrid* value, int x, int y, int semantic) {
        if (!value) return staleProcgenResult<void>(vm, "grid");
        auto view = Procgen::resolve(value->reference);
        if (!view.isBound()) return staleProcgenResult<void>(vm, "grid");
        view->setCell(x, y, semantic);
        return eve::script::projectResult(
            vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
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

    auto ownedContext = table.addClass<ScriptProcgenContext>(
        "ProcgenOwnedContext",
        std::function<ScriptProcgenContext*()>([] { return nullptr; }), true);
    ownedContext.addFunc("ownership", [](ScriptProcgenContext*) { return std::string("owned"); });
    ownedContext.addFunc("ownerEpoch", [](ScriptProcgenContext* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedContext.addFunc("handle", [](ScriptProcgenContext* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedContext.addFunc("isStale", [](ScriptProcgenContext* value) {
        return !value || Procgen::isStale(value->reference);
    });
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
    ownedContext.addFunc("commit", [vm](ScriptProcgenContext* value) {
        if (!value) return staleProcgenResult<void>(vm, "context");
        auto* module = ModuleManager::getInstance<Procgen>("Procgen");
        if (!module)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                 "Procgen module is no longer loaded", "context"));
        return eve::script::projectResult(vm, module->commitSystem(value->reference));
    });
    ownedContext.addFunc("abort", [vm](ScriptProcgenContext* value) {
        if (!value) return staleProcgenResult<void>(vm, "context");
        auto* module = ModuleManager::getInstance<Procgen>("Procgen");
        if (!module)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                 "Procgen module is no longer loaded", "context"));
        return eve::script::projectResult(vm, module->abortSystem(value->reference));
    });

    auto mesh = table.addClass<MeshBuild>(
        "ProcgenMeshBuild", std::function<MeshBuild *()>([]() -> MeshBuild * { return nullptr; }),
        true);
    mesh.addFunc("clear", &MeshBuild::clear);
    mesh.addFunc("appendTransformed", &MeshBuild::appendTransformed);
    mesh.addFunc("setActiveGroup", &MeshBuild::setActiveGroup);
    mesh.addFunc("getGroupCount", &MeshBuild::getGroupCount);
    mesh.addFunc("getGroupName", &MeshBuild::getGroupName);
    mesh.addFunc("getTriangleGroup", &MeshBuild::getTriangleGroup);
    mesh.addFunc("copyGroup", [vm](MeshBuild *self, int groupIndex) -> ssq::Object {
        if (!self) return ssq::Object(vm);
        auto object = eve::script::makeOwnedSquirrelInstance<MeshBuild>(vm,
                                                                         self->copyGroup(groupIndex));
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
    cls.addFunc("newParams", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        return makeOwnedProxy<ProcgenParamsHandleRef, ScriptProcgenParams>(
            vm, Procgen::newParamsHandle(),
            [](ProcgenParamsHandleRef ref) { return Procgen::release(ref); });
    });
    cls.addFunc("newGrid", [vm = cls.getHandle()](Procgen*, int width, int height) -> ssq::Table {
        return makeOwnedProxy<ProcgenGridHandleRef, ScriptProcgenGrid>(
            vm, Procgen::newGridHandle(width, height),
            [](ProcgenGridHandleRef ref) { return Procgen::release(ref); });
    });
    cls.addFunc("newOutput", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeAnyOwnedProxy(
            vm, module->newOutputHandle(),
            [](ProcgenOutputHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releaseOutput(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "output");
            },
            [](ProcgenOutputHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->isOutputStale(ref);
            });
    });
    cls.addFunc("newPointSet", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeAnyOwnedProxy(
            vm, module->newPointSetHandle(),
            [](ProcgenPointSetHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releasePointSet(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "pointSet");
            },
            [](ProcgenPointSetHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->isPointSetStale(ref);
            });
    });
    cls.addFunc("sampleGrid", [vm = cls.getHandle()](Procgen*, int width, int depth,
                                                            float spacing, uint32_t seed,
                                                            float jitter) -> ssq::Table {
        auto* module = Procgen::create();
        return makeAnyOwnedProxy(
            vm, module->sampleGridHandle(width, depth, spacing, seed, jitter),
            [](ProcgenPointSetHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releasePointSet(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "pointSet");
            },
            [](ProcgenPointSetHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->isPointSetStale(ref);
            });
    });
    cls.addFunc("newTerrainSampler", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeAnyOwnedProxy(
            vm, module->newTerrainSamplerHandle(),
            [](ProcgenTerrainSamplerHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releaseTerrainSampler(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "sampler");
            },
            [](ProcgenTerrainSamplerHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->isTerrainSamplerStale(ref);
            });
    });
    cls.addFunc("newHeightmap", [vm = cls.getHandle()](Procgen*, int width, int height) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedHeightmapProxy(vm, module->newHeightmapHandle(width, height));
    });
    cls.addFunc("newCloudField", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeAnyOwnedProxy(
            vm, module->newCloudFieldHandle(),
            [](ProcgenCloudFieldHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releaseCloudField(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "cloudField");
            },
            [](ProcgenCloudFieldHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->isCloudFieldStale(ref);
            });
    });
    cls.addFunc("newCloudShadow", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeAnyOwnedProxy(
            vm, module->newCloudShadowHandle(),
            [](ProcgenCloudShadowHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->releaseCloudShadow(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "cloudShadow");
            },
            [](ProcgenCloudShadowHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->isCloudShadowStale(ref);
            });
    });
    cls.addFunc("generate", [vm = cls.getHandle()](Procgen*, const std::string& algorithm,
                                                          ScriptProcgenParams* params) -> ssq::Table {
        auto* module = Procgen::create();
        if (!params)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<ProcgenGridHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "generate params proxy must not be null", "params")
                         .status(),
                false, false);
        return makeAnyOwnedProxy(
            vm, module->generateHandle(algorithm, params->reference),
            [](ProcgenGridHandleRef ref) { return Procgen::release(ref); },
            [](ProcgenGridHandleRef ref) { return Procgen::isStale(ref); });
    });
    cls.addFunc("generateImage", [vm = cls.getHandle()](Procgen*, const std::string& recipe,
                                                               ScriptProcgenParams* params) -> ssq::Table {
        auto* module = Procgen::create();
        if (!params)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<ProcgenImageHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "generateImage params proxy must not be null", "params")
                         .status(),
                false, false);
        return makeAnyOwnedProxy(
            vm, module->generateImageHandle(recipe, params->reference),
            [](ProcgenImageHandleRef ref) { return Procgen::release(ref); },
            [](ProcgenImageHandleRef ref) { return Procgen::isStale(ref); });
    });
    cls.addFunc("generateNormalImage", [vm = cls.getHandle()](Procgen*, const std::string& recipe,
                                                                      ScriptProcgenParams* params) -> ssq::Table {
        auto* module = Procgen::create();
        if (!params)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<ProcgenNormalImageHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "generateNormalImage params proxy must not be null", "params")
                         .status(),
                false, false);
        return makeAnyOwnedProxy(
            vm, module->generateNormalImageHandle(recipe, params->reference),
            [](ProcgenNormalImageHandleRef ref) { return Procgen::release(ref); },
            [](ProcgenNormalImageHandleRef ref) { return Procgen::isStale(ref); });
    });
    cls.addFunc("generatePbrMaterial", [vm = cls.getHandle()](Procgen*, const std::string& recipe,
                                                                       ScriptProcgenParams* params) -> ssq::Table {
        auto* module = Procgen::create();
        if (!params)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<ProcgenPbrMaterialHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "generatePbrMaterial params proxy must not be null", "params")
                         .status(),
                false, false);
        return makeAnyOwnedProxy(
            vm, module->generatePbrMaterialHandle(recipe, params->reference),
            [](ProcgenPbrMaterialHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->ownership_->pbr.erase(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "pbr");
            },
            [](ProcgenPbrMaterialHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->ownership_->pbr.isStale(ref);
            });
    });
    cls.addFunc("buildMesh", [vm = cls.getHandle()](Procgen*, const std::string& recipe,
                                                           ScriptProcgenParams* params) -> ssq::Table {
        auto* module = Procgen::create();
        if (!params)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<ProcgenMeshBuildHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "buildMesh params proxy must not be null", "params")
                         .status(),
                false, false);
        return makeAnyOwnedProxy(
            vm, module->buildMeshHandle(recipe, params->reference),
            [](ProcgenMeshBuildHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->ownership_->meshes.erase(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "mesh");
            },
            [](ProcgenMeshBuildHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->ownership_->meshes.isStale(ref);
            });
    });
    cls.addFunc("generateHeightmap", [vm = cls.getHandle()](Procgen*, ScriptProcgenParams* params) -> ssq::Table {
        auto* module = Procgen::create();
        if (!params)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<ProcgenHeightmapHandleRef>(
                        eve::DiagnosticCode::InvalidArgument, "generateHeightmap params proxy must not be null", "params")
                         .status(),
                false, false);
        return makeAnyOwnedProxy(
            vm, module->generateHeightmapHandle(params->reference),
            [](ProcgenHeightmapHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return owner ? owner->ownership_->heightmaps.erase(ref)
                             : procgenBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                           "Procgen module is no longer loaded", "heightmap");
            },
            [](ProcgenHeightmapHandleRef ref) {
                auto* owner = ModuleManager::getInstance<Procgen>("Procgen");
                return !owner || owner->ownership_->heightmaps.isStale(ref);
            });
    });
    cls.addFunc("deriveSeed", &Procgen::deriveSeed);
    cls.addFunc("beginSystem", [vm = cls.getHandle()](Procgen*, const std::string& name,
                                                               uint32_t seed) -> ssq::Table {
        return makeOwnedProxy<ProcgenContextHandleRef, ScriptProcgenContext>(
            vm, Procgen::beginSystemHandle(name, seed),
            [](ProcgenContextHandleRef ref) { return Procgen::release(ref); });
    });
    cls.addFunc("beginCachedSystem", [vm = cls.getHandle()](Procgen*, const std::string& name,
                                                                     uint32_t seed,
                                                                     const std::string& buildKey) -> ssq::Table {
        return makeOwnedProxy<ProcgenContextHandleRef, ScriptProcgenContext>(
            vm, Procgen::beginCachedSystemHandle(name, seed, buildKey),
            [](ProcgenContextHandleRef ref) { return Procgen::release(ref); });
    });
    cls.addFunc("commitSystem", [vm = cls.getHandle()](Procgen* value,
                                                         ScriptProcgenContext* context) {
        if (!value || !context)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "commitSystem requires a context proxy", "context"));
        return eve::script::projectResult(vm, value->commitSystem(context->reference));
    });
    cls.addFunc("abortSystem", [vm = cls.getHandle()](Procgen* value,
                                                        ScriptProcgenContext* context) {
        if (!value || !context)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "abortSystem requires a context proxy", "context"));
        return eve::script::projectResult(vm, value->abortSystem(context->reference));
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
    cls.addFunc("getAlgorithmSchema", [vm = cls.getHandle()](Procgen* value,
                                                                const std::string& algorithm) {
        if (!value)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<RecipeDescriptor>(
                        eve::DiagnosticCode::InvalidArgument,
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
    cls.addFunc("applyAlgorithmDefaults", [vm = cls.getHandle()](Procgen* value,
                                                                    const std::string& algorithm,
                                                                    ScriptProcgenParams* params) {
        if (!value || !params)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "algorithm defaults require a params proxy", "params"));
        return eve::script::projectResult(vm, value->applyAlgorithmDefaults(algorithm, params->reference));
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
                vm, procgenBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                       "gridToJson requires a grid proxy", "grid"),
                [](std::string&& json) { return eve::Value(std::move(json)); });
        return eve::script::projectResult(
            vm, value->gridToJson(grid->reference),
            [](std::string&& json) { return eve::Value(std::move(json)); });
    });
    cls.addFunc("getTextureRecipeCount", &Procgen::getTextureRecipeCount);
    cls.addFunc("getTextureRecipeId", &Procgen::getTextureRecipeId);
    cls.addFunc("hasTextureRecipe", &Procgen::hasTextureRecipe);
    cls.addFunc("getTextureRecipeSchema", [vm = cls.getHandle()](Procgen* value,
                                                                    const std::string& recipe) {
        if (!value)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<RecipeDescriptor>(
                        eve::DiagnosticCode::InvalidArgument,
                        "texture schema requires a Procgen module", "procgen")
                        .status(),
                false, false);
        return projectRecipeDescriptorResult(vm, value->getTextureRecipeSchema(recipe));
    });
    cls.addFunc("applyTextureRecipeDefaults", [vm = cls.getHandle()](Procgen* value,
                                                                        const std::string& recipe,
                                                                        ScriptProcgenParams* params) {
        if (!value || !params)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "texture defaults require a params proxy", "params"));
        return eve::script::projectResult(vm, value->applyTextureRecipeDefaults(recipe, params->reference));
    });
    cls.addFunc("getPbrRecipeCount", &Procgen::getPbrRecipeCount);
    cls.addFunc("getPbrRecipeId", &Procgen::getPbrRecipeId);
    cls.addFunc("hasPbrRecipe", &Procgen::hasPbrRecipe);
    cls.addFunc("getPbrRecipeSchema", [vm = cls.getHandle()](Procgen* value,
                                                               const std::string& recipe) {
        if (!value)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<RecipeDescriptor>(
                        eve::DiagnosticCode::InvalidArgument,
                        "PBR schema requires a Procgen module", "procgen")
                        .status(),
                false, false);
        return projectRecipeDescriptorResult(vm, value->getPbrRecipeSchema(recipe));
    });
    cls.addFunc("applyPbrRecipeDefaults", [vm = cls.getHandle()](Procgen* value,
                                                                    const std::string& recipe,
                                                                    ScriptProcgenParams* params) {
        if (!value || !params)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "PBR defaults require a params proxy", "params"));
        return eve::script::projectResult(vm, value->applyPbrRecipeDefaults(recipe, params->reference));
    });
    cls.addFunc("buildArtifact", [vm = cls.getHandle()](Procgen* value,
                                                                  const std::string& recipeId,
                                                                  ScriptProcgenParams* params,
                                                                  const std::string& artifactIdentity) {
        if (!value)
            return eve::script::projectResult(
                vm, procgenBindingFailure<GeneratedArtifact>(
                        eve::DiagnosticCode::InvalidArgument,
                        "procgen module must not be null", "procgen"),
                [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
        const auto parsed = ArtifactId::parse(artifactIdentity);
        if (!parsed || parsed->isNil())
            return eve::script::projectResult(
                vm, procgenBindingFailure<GeneratedArtifact>(
                        eve::DiagnosticCode::InvalidArgument,
                        "artifact identity must be a non-nil canonical UUID", "artifactIdentity"),
                [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
        if (!params)
            return eve::script::projectResult(
                vm, procgenBindingFailure<GeneratedArtifact>(
                        eve::DiagnosticCode::InvalidArgument,
                        "buildArtifact requires a params proxy", "params"),
                [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
        return eve::script::projectResult(
            vm, value->buildArtifact(recipeId, params->reference, *parsed),
            [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
    });
    cls.addFunc("publishArtifact", [vm = cls.getHandle()](Procgen* value,
                                                                    const std::string& recipeId,
                                                                    ScriptProcgenParams* params,
                                                                    const std::string& artifactIdentity,
                                                                    bool scene,
                                                                    bool graphics,
                                                                    bool physics,
                                                                    bool map) {
        if (!value)
            return eve::script::projectResult(
                vm, procgenBindingFailure<ArtifactPublishReceipt>(
                        eve::DiagnosticCode::InvalidArgument,
                        "procgen module must not be null", "procgen"),
                [](ArtifactPublishReceipt&& receipt) {
                    return publishReceiptProjection(std::move(receipt));
                });
        const auto parsed = ArtifactId::parse(artifactIdentity);
        if (!parsed || parsed->isNil())
            return eve::script::projectResult(
                vm, procgenBindingFailure<ArtifactPublishReceipt>(
                        eve::DiagnosticCode::InvalidArgument,
                        "artifact identity must be a non-nil canonical UUID", "artifactIdentity"),
                [](ArtifactPublishReceipt&& receipt) {
                    return publishReceiptProjection(std::move(receipt));
                });
        ArtifactPublishOptions options;
        options.scene = scene;
        options.graphics = graphics;
        options.physics = physics;
        options.map = map;
        if (!params)
            return eve::script::projectResult(
                vm, procgenBindingFailure<ArtifactPublishReceipt>(
                        eve::DiagnosticCode::InvalidArgument,
                        "publishArtifact requires a params proxy", "params"),
                [](ArtifactPublishReceipt&& receipt) {
                    return publishReceiptProjection(std::move(receipt));
                });
        return eve::script::projectResult(
            vm, value->publishArtifact(recipeId, params->reference, *parsed, options),
            [](ArtifactPublishReceipt&& receipt) {
                return publishReceiptProjection(std::move(receipt));
            });
    });
    cls.addFunc("getMeshRecipeCount", &Procgen::getMeshRecipeCount);
    cls.addFunc("getMeshRecipeId", &Procgen::getMeshRecipeId);
    cls.addFunc("hasMeshRecipe", &Procgen::hasMeshRecipe);
    cls.addFunc("getMeshRecipeSchema", [vm = cls.getHandle()](Procgen* value,
                                                                const std::string& recipe) {
        if (!value)
            return eve::script::projectStatusResult(
                vm, procgenBindingFailure<RecipeDescriptor>(
                        eve::DiagnosticCode::InvalidArgument,
                        "mesh schema requires a Procgen module", "procgen")
                        .status(),
                false, false);
        return projectRecipeDescriptorResult(vm, value->getMeshRecipeSchema(recipe));
    });
    cls.addFunc("applyMeshRecipeDefaults", [vm = cls.getHandle()](Procgen* value,
                                                                     const std::string& recipe,
                                                                     ScriptProcgenParams* params) {
        if (!value || !params)
            return eve::script::projectResult(
                vm, procgenBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "mesh defaults require a params proxy", "params"));
        return eve::script::projectResult(vm, value->applyMeshRecipeDefaults(recipe, params->reference));
    });
}

}  // namespace eve::procgen
