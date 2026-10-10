#include "procgen/ProcgenScriptSupport.h"
#include "procgen/ProcgenLive.h"

#include "procgen/GeneratedArtifact.h"
#include "procgen/heightmap/TerrainFile.h"

#include <cstdint>
#include <string>

namespace eve::procgen {

ssq::Table makeOwnedPointSetProxy(HSQUIRRELVM vm, eve::Result<ProcgenPointSetHandleRef>&& reference) {
    auto* module = Procgen::create();
    return makeOwnedNativeProxy<PointSet>(
        vm, std::move(reference), [module](ProcgenPointSetHandleRef ref) { return module->resolvePointSet(ref); },
        [module](ProcgenPointSetHandleRef ref) {
            if (liveProcgen() != module)
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                     "Procgen module is no longer loaded",
                                                                     "pointSet", {}, "procgen.squirrel"));
            return module->releasePointSet(ref);
        });
}

ssq::Table makeOwnedGridProxy(HSQUIRRELVM vm, eve::Result<ProcgenGridHandleRef>&& reference) {
    auto* module = Procgen::create();
    return makeOwnedNativeProxy<Grid2D>(
        vm, std::move(reference), [module](ProcgenGridHandleRef ref) { return module->resolve(ref); },
        [module](ProcgenGridHandleRef ref) {
            if (liveProcgen() != module)
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                     "Procgen module is no longer loaded",
                                                                     "grid", {}, "procgen.squirrel"));
            return module->release(ref);
        });
}

ssq::Table makeOwnedSpatialProxy(HSQUIRRELVM vm, eve::Result<ProcgenSpatialDataHandleRef>&& reference) {
    auto* module = Procgen::create();
    return makeOwnedNativeProxy<SpatialData>(
        vm, std::move(reference), [module](ProcgenSpatialDataHandleRef ref) { return module->resolveSpatialData(ref); },
        [module](ProcgenSpatialDataHandleRef ref) {
            if (liveProcgen() != module)
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                     "Procgen module is no longer loaded",
                                                                     "spatialData", {}, "procgen.squirrel"));
            return module->release(ref);
        });
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

/** @brief Projects a decoded terrain file as an owned heightmap proxy plus metadata.
 *
 * The heightmap arrives through the same owning-handle path as `newHeightmap`, so
 * scripts sample it with the ordinary `ProcgenHeightmap` methods. Spacing is only
 * authoritative when `hasSpacing` is true: an EVTR archive stores no
 * metres-per-cell and leaves that to the level that references it.
 */
ssq::Table projectDecodedTerrainResult(HSQUIRRELVM vm, eve::Result<DecodedTerrainFile>&& decoded) {
    if (!decoded) return eve::script::projectStatusResult(vm, decoded.status());
    DecodedTerrainFile terrain = std::move(decoded).takeValue();
    const std::int64_t width   = terrain.heightmap.getWidth();
    const std::int64_t height  = terrain.heightmap.getHeight();
    auto*              module  = Procgen::create();
    auto               result  = makeOwnedNativeProxy<Heightmap>(
        vm, module->adoptHeightmap(std::move(terrain.heightmap)),
        [module](ProcgenHeightmapHandleRef ref) { return module->resolveHeightmap(ref); },
        [module](ProcgenHeightmapHandleRef ref) {
            if (liveProcgen() != module)
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                     "Procgen module is no longer loaded",
                                                                     "heightmap", {}, "procgen.squirrel"));
            return module->releaseHeightmap(ref);
        });
    result.set("spacingX", terrain.spacingX);
    result.set("spacingZ", terrain.spacingZ);
    result.set("hasSpacing", terrain.hasSpacing);
    result.set("minHeight", terrain.minHeight);
    result.set("maxHeight", terrain.maxHeight);
    result.set("format", terrain.format);
    result.set("width", width);
    result.set("height", height);
    return result;
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
    if (!ok) return eve::script::projectStatusResult(vm, status);

    auto descriptor = std::move(result).takeValue();
    auto object     = eve::script::makeOwnedSquirrelInstance<RecipeDescriptor>(
        vm, std::make_unique<RecipeDescriptor>(std::move(descriptor)));
    if (!object) {
        const eve::Status failure = object.status();
        object.ignore("failed to create owned Procgen recipe schema");
        return eve::script::projectStatusResult(vm, failure);
    }

    return eve::script::projectStatusResult(vm, status, std::move(object).takeValue());
}

}  // namespace eve::procgen
