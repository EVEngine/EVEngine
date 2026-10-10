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

void Procgen::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Procgen::getName);
    exposeScriptGeneratorHost(cls);
    exposeHexTerrain(cls);
    cls.addFunc("newParams", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        return makeOwnedProxy<ProcgenParamsHandleRef, ScriptProcgenParams>(
            vm, Procgen::newParamsHandle(), [](ProcgenParamsHandleRef ref) { return Procgen::release(ref); });
    });
    cls.addFunc("newGrid", [vm = cls.getHandle()](Procgen*, int width, int height) -> ssq::Table {
        return makeOwnedGridProxy(vm, Procgen::newGridHandle(width, height));
    });
    cls.addFunc("newOutput", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<OutputSpec>(
            vm, module->newOutputHandle(), [module](ProcgenOutputHandleRef ref) { return module->resolveOutput(ref); },
            [module](ProcgenOutputHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "output", {}, "procgen.squirrel"));
                return module->releaseOutput(ref);
            });
    });
    cls.addFunc("newPointSet", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<PointSet>(
            vm, module->newPointSetHandle(),
            [module](ProcgenPointSetHandleRef ref) { return module->resolvePointSet(ref); },
            [module](ProcgenPointSetHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "pointSet", {}, "procgen.squirrel"));
                return module->releasePointSet(ref);
            });
    });
    cls.addFunc("sampleGrid",
                [vm = cls.getHandle()](Procgen*, int width, int depth, float spacing, uint32_t seed,
                                       float jitter) -> ssq::Table {
                    auto* module = Procgen::create();
                    return makeOwnedNativeProxy<PointSet>(
                        vm, module->sampleGridHandle(width, depth, spacing, seed, jitter),
                        [module](ProcgenPointSetHandleRef ref) { return module->resolvePointSet(ref); },
                        [module](ProcgenPointSetHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "pointSet", {}, "procgen.squirrel"));
                return module->releasePointSet(ref);
            });
                });
    cls.addFunc("filterHeight", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minimum, float maximum) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "filterHeight requires owned points", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->filterHeightHandle(*reference, minimum, maximum));
    });
    cls.addFunc("filterDensity", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minimum, float maximum) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "filterDensity requires owned points", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->filterDensityHandle(*reference, minimum, maximum));
    });
    cls.addFunc("filterBox", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minX, float minY, float minZ,
                                                    float maxX, float maxY, float maxZ) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "filterBox requires owned points",
                                               "points", {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm,
                                      value->filterBoxHandle(*reference, minX, minY, minZ, maxX, maxY, maxZ, false));
    });
    cls.addFunc("excludeBox", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minX, float minY,
                                                     float minZ, float maxX, float maxY, float maxZ) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "excludeBox requires owned points",
                                               "points", {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->filterBoxHandle(*reference, minX, minY, minZ, maxX, maxY, maxZ, true));
    });
    cls.addFunc("filterSlope", [vm = cls.getHandle()](Procgen* value, PointSet* input, float minimum, float maximum) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                               "filterSlope requires owned points", "points", {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->filterSlopeHandle(*reference, minimum, maximum));
    });
    cls.addFunc(
        "excludeRadius", [vm = cls.getHandle()](Procgen* value, PointSet* input, float x, float z, float radius) {
            const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
            if (!value || !reference)
                return makeOwnedPointSetProxy(
                    vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument, "excludeRadius requires owned points", "points", {},
                            "procgen.squirrel")));
            return makeOwnedPointSetProxy(vm, value->excludeRadiusHandle(*reference, x, z, radius));
        });
    cls.addFunc("jitterPoints",
                [vm = cls.getHandle()](Procgen* value, PointSet* input, uint32_t seed, float amountX, float amountZ) {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    if (!value || !reference)
                        return makeOwnedPointSetProxy(
                            vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "jitterPoints requires owned points",
                                    "points", {}, "procgen.squirrel")));
                    return makeOwnedPointSetProxy(vm, value->jitterPointsHandle(*reference, seed, amountX, amountZ));
                });
    cls.addFunc("poissonDisk", [vm = cls.getHandle()](Procgen* value, int width, int depth, float radius, uint32_t seed,
                                                      int maxPoints) {
        if (!value)
            return makeOwnedPointSetProxy(vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument, "poissonDisk requires Procgen",
                                                  "procgen", {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->poissonDiskHandle(width, depth, radius, seed, maxPoints));
    });
    cls.addFunc("mergePoints", [vm = cls.getHandle()](Procgen* value, PointSet* first, PointSet* second) {
        const auto firstRef  = nativeProxyReference<ProcgenPointSetHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenPointSetHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "mergePoints requires owned point sets", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->mergePointsHandle(*firstRef, *secondRef));
    });
    cls.addFunc("unionPoints", [vm = cls.getHandle()](Procgen* value, PointSet* first, PointSet* second) {
        const auto firstRef  = nativeProxyReference<ProcgenPointSetHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenPointSetHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "unionPoints requires owned point sets", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->unionPointsHandle(*firstRef, *secondRef));
    });
    cls.addFunc("intersectPoints", [vm = cls.getHandle()](Procgen* value, PointSet* first, PointSet* second) {
        const auto firstRef  = nativeProxyReference<ProcgenPointSetHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenPointSetHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "intersectPoints requires owned point sets", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->intersectPointsHandle(*firstRef, *secondRef));
    });
    cls.addFunc("differencePoints", [vm = cls.getHandle()](Procgen* value, PointSet* first, PointSet* second) {
        const auto firstRef  = nativeProxyReference<ProcgenPointSetHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenPointSetHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "differencePoints requires owned point sets", "points",
                        {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->differencePointsHandle(*firstRef, *secondRef));
    });
    cls.addFunc("transformPoints", [vm = cls.getHandle()](Procgen* value, PointSet* input, float x, float y, float z,
                                                          float yaw, float scaleX, float scaleY, float scaleZ) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "transformPoints requires owned points", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm,
                                      value->transformPointsHandle(*reference, x, y, z, yaw, scaleX, scaleY, scaleZ));
    });
    cls.addFunc("transformPoints3D", [vm = cls.getHandle()](Procgen* value, PointSet* input, float x, float y, float z,
                                                            float pitch, float yaw, float roll, float scaleX,
                                                            float scaleY, float scaleZ) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "transformPoints3D requires owned points", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(
            vm, value->transformPoints3DHandle(*reference, x, y, z, pitch, yaw, roll, scaleX, scaleY, scaleZ));
    });
    cls.addFunc("copyPoints", [vm = cls.getHandle()](Procgen* value, PointSet* source, PointSet* targets,
                                                     bool inheritTargetAttributes) {
        const auto sourceRef = nativeProxyReference<ProcgenPointSetHandleRef>(source);
        const auto targetRef = nativeProxyReference<ProcgenPointSetHandleRef>(targets);
        if (!value || !sourceRef || !targetRef)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "copyPoints requires owned point sets", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->copyPointsHandle(*sourceRef, *targetRef, inheritTargetAttributes));
    });
    cls.addFunc("remapDensity", [vm = cls.getHandle()](Procgen* value, PointSet* input, float inputMin, float inputMax,
                                                       float outputMin, float outputMax, bool clampOutput) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "remapDensity requires owned points", "points", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(
            vm, value->remapDensityHandle(*reference, inputMin, inputMax, outputMin, outputMax, clampOutput));
    });
    cls.addFunc(
        "mathFloatAttribute", [vm = cls.getHandle()](Procgen* value, PointSet* input, const std::string& attribute,
                                                     const std::string& outputAttribute, const std::string& operation,
                                                     float operand, float defaultValue) {
            const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
            if (!value || !reference)
                return makeOwnedPointSetProxy(
                    vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument, "mathFloatAttribute requires owned points", "points",
                            {}, "procgen.squirrel")));
            return makeOwnedPointSetProxy(vm, value->mathFloatAttributeHandle(*reference, attribute, outputAttribute,
                                                                              operation, operand, defaultValue));
        });
    cls.addFunc("filterFloatAttribute", [vm = cls.getHandle()](Procgen* value, PointSet* input, const std::string& name,
                                                               float minimum, float maximum, bool invert) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "filterFloatAttribute requires owned points", "points",
                        {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm,
                                      value->filterFloatAttributeHandle(*reference, name, minimum, maximum, invert));
    });
    cls.addFunc("filterStringAttribute", [vm = cls.getHandle()](Procgen* value, PointSet* input,
                                                                const std::string& name, const std::string& expected,
                                                                bool invert) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "filterStringAttribute requires owned points", "points",
                        {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->filterStringAttributeHandle(*reference, name, expected, invert));
    });
    cls.addFunc("excludeGridMask",
                [vm = cls.getHandle()](Procgen* value, PointSet* input, Grid2D* mask, float originX, float originZ,
                                       float cellSize, int semantic, float clearance, int maximumChecks) {
                    const auto pointRef = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    const auto gridRef  = nativeProxyReference<ProcgenGridHandleRef>(mask);
                    if (!value || !pointRef || !gridRef)
                        return makeOwnedPointSetProxy(
                            vm, procgenBindingFailure<ProcgenPointSetHandleRef>(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "excludeGridMask requires owned points and grid", "input"));
                    return makeOwnedPointSetProxy(vm, value->excludeGridMaskHandle(
                                                          *pointRef, *gridRef, originX, originZ, cellSize, semantic,
                                                          clearance, maximumChecks));
                });
    cls.addFunc(
        "densityCull", [vm = cls.getHandle()](Procgen* value, PointSet* input, uint32_t seed, float multiplier) {
            const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
            if (!value || !reference)
                return makeOwnedPointSetProxy(
                    vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument, "densityCull requires owned points", "points", {},
                            "procgen.squirrel")));
            return makeOwnedPointSetProxy(vm, value->densityCullHandle(*reference, seed, multiplier));
        });
    cls.addFunc("projectToWorld", [vm = cls.getHandle()](Procgen* value, PointSet* input, float maxY, float minY,
                                                         std::int64_t maskBits, bool keepUnmatched) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !reference || maskBits < 0)
            return makeOwnedPointSetProxy(vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument,
                                                  "projectToWorld requires owned points and a non-negative mask",
                                                  "points", {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(
            vm,
            value->projectToWorldHandle(*reference, maxY, minY, static_cast<std::uint64_t>(maskBits), keepUnmatched));
    });
    cls.addFunc("newTerrainSampler", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<TerrainSampler>(
            vm, module->newTerrainSamplerHandle(),
            [module](ProcgenTerrainSamplerHandleRef ref) { return module->resolveTerrainSampler(ref); },
            [module](ProcgenTerrainSamplerHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "sampler", {}, "procgen.squirrel"));
                return module->releaseTerrainSampler(ref);
            });
    });
    cls.addFunc("newHeightmap", [vm = cls.getHandle()](Procgen*, int width, int height) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<Heightmap>(
            vm, module->newHeightmapHandle(width, height),
            [module](ProcgenHeightmapHandleRef ref) { return module->resolveHeightmap(ref); },
            [module](ProcgenHeightmapHandleRef ref) { return module->releaseHeightmap(ref); });
    });
    // Decoded terrain files return the same heightmap proxy as newHeightmap, with
    // the file's own metadata attached to the result envelope. Spacing is only
    // authoritative for EVTRN: an EVTR archive stores no metres-per-cell, so the
    // referencing level owns that value and `hasSpacing` is false.
    cls.addFunc("loadTerrainFile",
                [vm = cls.getHandle()](Procgen*, const std::string& path, const std::string& format) -> ssq::Table {
                    return projectDecodedTerrainResult(vm, loadTerrainFile(path, parseTerrainFileFormat(format)));
                });
    cls.addFunc("loadTerrainBytes",
                [vm = cls.getHandle()](Procgen*, const std::string& bytes, const std::string& format) -> ssq::Table {
                    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
                    return projectDecodedTerrainResult(
                        vm, decodeTerrainFile(std::span<const std::uint8_t>(data, bytes.size()),
                                              parseTerrainFileFormat(format)));
                });
    cls.addFunc("newCloudField", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<CloudField>(
            vm, module->newCloudFieldHandle(),
            [module](ProcgenCloudFieldHandleRef ref) { return module->resolveCloudField(ref); },
            [module](ProcgenCloudFieldHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "cloudField", {}, "procgen.squirrel"));
                return module->releaseCloudField(ref);
            });
    });
    cls.addFunc("newCloudShadow", [vm = cls.getHandle()](Procgen*) -> ssq::Table {
        auto* module = Procgen::create();
        return makeOwnedNativeProxy<CloudShadow>(
            vm, module->newCloudShadowHandle(),
            [module](ProcgenCloudShadowHandleRef ref) { return module->resolveCloudShadow(ref); },
            [module](ProcgenCloudShadowHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "cloudShadow", {}, "procgen.squirrel"));
                return module->releaseCloudShadow(ref);
            });
    });
    cls.addFunc(
        "generate",
        [vm = cls.getHandle()](Procgen*, const std::string& algorithm, ScriptProcgenParams* params) -> ssq::Table {
            auto* module = Procgen::create();
            if (!params)
                return eve::script::projectStatusResult(
                    vm, eve::Result<ProcgenGridHandleRef>::failure(
                            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                   "generate params proxy must not be null", "params", {},
                                                   "procgen.squirrel"))
                            .status());
            return makeOwnedGridProxy(vm, module->generateHandle(algorithm, params->reference));
        });
    cls.addFunc(
        "generateImage",
        [vm = cls.getHandle()](Procgen*, const std::string& recipe, ScriptProcgenParams* params) -> ssq::Table {
            auto* module = Procgen::create();
            if (!params)
                return eve::script::projectStatusResult(
                    vm, eve::Result<ProcgenImageHandleRef>::failure(
                            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                   "generateImage params proxy must not be null", "params", {},
                                                   "procgen.squirrel"))
                            .status());
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
                            vm, eve::Result<ProcgenNormalImageHandleRef>::failure(
                                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                           "generateNormalImage params proxy must not be null",
                                                           "params", {}, "procgen.squirrel"))
                                    .status());
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
                            vm, eve::Result<ProcgenPbrMaterialHandleRef>::failure(
                                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                           "generatePbrMaterial params proxy must not be null",
                                                           "params", {}, "procgen.squirrel"))
                                    .status());
                    return makeOwnedNativeProxy<PbrTextureSet>(
                        vm, module->generatePbrMaterialHandle(recipe, params->reference),
                        [module](ProcgenPbrMaterialHandleRef ref) { return module->resolvePbrMaterial(ref); },
                        [module](ProcgenPbrMaterialHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "pbr", {}, "procgen.squirrel"));
                return module->releasePbrMaterial(ref);
            });
                });
    cls.addFunc("buildMesh",
                [vm = cls.getHandle()](Procgen*, const std::string& recipe, ScriptProcgenParams* params) -> ssq::Table {
                    auto* module = Procgen::create();
                    if (!params)
                        return eve::script::projectStatusResult(
                            vm, eve::Result<ProcgenMeshBuildHandleRef>::failure(
                                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                           "buildMesh params proxy must not be null", "params", {},
                                                           "procgen.squirrel"))
                                    .status());
                    return makeOwnedNativeProxy<MeshBuild>(
                        vm, module->buildMeshHandle(recipe, params->reference),
                        [module](ProcgenMeshBuildHandleRef ref) { return module->resolveMeshBuild(ref); },
                        [module](ProcgenMeshBuildHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "mesh", {}, "procgen.squirrel"));
                return module->releaseMeshBuild(ref);
            });
                });
    cls.addFunc("generateHeightmap", [vm = cls.getHandle()](Procgen*, ScriptProcgenParams* params) -> ssq::Table {
        auto* module = Procgen::create();
        if (!params)
            return eve::script::projectStatusResult(
                vm, eve::Result<ProcgenHeightmapHandleRef>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                               "generateHeightmap params proxy must not be null", "params", {},
                                               "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<Heightmap>(
            vm, module->generateHeightmapHandle(params->reference),
            [module](ProcgenHeightmapHandleRef ref) { return module->resolveHeightmap(ref); },
            [module](ProcgenHeightmapHandleRef ref) {
                if (liveProcgen() != module)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "heightmap", {}, "procgen.squirrel"));
                return module->releaseHeightmap(ref);
            });
    });
    cls.addFunc("newRuntimeGeneration", [vm = cls.getHandle()](Procgen* value, uint32_t worldSeed) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "runtime generation requires a Procgen module",
                                                                      "procgen", {}, "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<RuntimeGeneration>(
            vm, value->newRuntimeGenerationHandle(worldSeed),
            [value](ProcgenRuntimeGenerationHandleRef ref) { return value->resolveRuntimeGeneration(ref); },
            [value](ProcgenRuntimeGenerationHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "runtimeGeneration", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("newPointGraph", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "Procgen module must not be null", "procgen", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<PointGraph>(
            vm, value->newPointGraphHandle(),
            [value](ProcgenPointGraphHandleRef ref) { return value->resolvePointGraph(ref); },
            [value](ProcgenPointGraphHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "pointGraph", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("newMeshModifierGraph", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "Procgen module must not be null", "procgen", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<MeshModifierGraph>(
            vm, value->newMeshModifierGraphHandle(),
            [value](ProcgenMeshModifierGraphHandleRef ref) { return value->resolveMeshModifierGraph(ref); },
            [value](ProcgenMeshModifierGraphHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "meshModifierGraph", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("newSplinePath", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "Procgen module must not be null", "procgen", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<SplinePath>(
            vm, value->newSplinePathHandle(),
            [value](ProcgenSplinePathHandleRef ref) { return value->resolveSplinePath(ref); },
            [value](ProcgenSplinePathHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "splinePath", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("newMeshDeformationSession", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "Procgen module must not be null", "procgen", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<MeshDeformationSession>(
            vm, value->newMeshDeformationSessionHandle(),
            [value](ProcgenMeshDeformationSessionHandleRef ref) {
                return value->resolveMeshDeformationSession(ref);
            },
            [value](ProcgenMeshDeformationSessionHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "meshDeformation", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("newDynamicMeshUvPaintSession", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "Procgen module must not be null", "procgen", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<DynamicMeshUvPaintSession>(
            vm, value->newDynamicMeshUvPaintSessionHandle(),
            [value](ProcgenDynamicMeshUvPaintSessionHandleRef ref) {
                return value->resolveDynamicMeshUvPaintSession(ref);
            },
            [value](ProcgenDynamicMeshUvPaintSessionHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded",
                        "dynamicUvPaint", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("newGridGraph", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "Procgen module must not be null", "procgen", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<GridGraph>(
            vm, value->newGridGraphHandle(),
            [value](ProcgenGridGraphHandleRef ref) { return value->resolveGridGraph(ref); },
            [value](ProcgenGridGraphHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "gridGraph", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("newMeshGraph", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "Procgen module must not be null", "procgen", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<MeshGraph>(
            vm, value->newMeshGraphHandle(),
            [value](ProcgenMeshGraphHandleRef ref) { return value->resolveMeshGraph(ref); },
            [value](ProcgenMeshGraphHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "meshGraph", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("newBiomeRules", [vm = cls.getHandle()](Procgen* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "Procgen module must not be null", "procgen", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedNativeProxy<BiomeRules>(
            vm, value->newBiomeRulesHandle(),
            [value](ProcgenBiomeRulesHandleRef ref) { return value->resolveBiomeRules(ref); },
            [value](ProcgenBiomeRulesHandleRef ref) {
                if (liveProcgen() != value)
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                         "Procgen module is no longer loaded",
                                                                         "biomeRules", {}, "procgen.squirrel"));
                return value->release(ref);
            });
    });
    cls.addFunc("boxVolume",
                [vm = cls.getHandle()](Procgen* value, float minX, float minY, float minZ, float maxX, float maxY,
                                       float maxZ) -> ssq::Table {
                    if (!value)
                        return eve::script::projectStatusResult(
                            vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                                  "Procgen module must not be null",
                                                                                  "procgen", {}, "procgen.squirrel"))
                                    .status());
                    return makeOwnedSpatialProxy(vm, value->boxVolumeHandle(minX, minY, minZ, maxX, maxY, maxZ));
                });
    cls.addFunc("sphereVolume", [vm = cls.getHandle()](Procgen* value, float x, float y, float z, float radius) {
        if (!value)
            return makeOwnedSpatialProxy(vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                                                 eve::DiagnosticCode::InvalidArgument, "sphereVolume requires Procgen",
                                                 "procgen", {}, "procgen.squirrel")));
        return makeOwnedSpatialProxy(vm, value->sphereVolumeHandle(x, y, z, radius));
    });
    cls.addFunc("polygonVolume", [vm = cls.getHandle()](Procgen* value, PointSet* points, float minY, float maxY) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(points);
        if (!value || !reference)
            return makeOwnedSpatialProxy(
                vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "polygonVolume requires owned control points", "points",
                        {}, "procgen.squirrel")));
        return makeOwnedSpatialProxy(vm, value->polygonVolumeHandle(*reference, minY, maxY));
    });
    cls.addFunc("pointData", [vm = cls.getHandle()](Procgen* value, PointSet* points) {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(points);
        if (!value || !reference)
            return makeOwnedSpatialProxy(vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                                                 eve::DiagnosticCode::InvalidArgument,
                                                 "pointData requires owned points", "points", {}, "procgen.squirrel")));
        return makeOwnedSpatialProxy(vm, value->pointDataHandle(*reference));
    });
    cls.addFunc("heightfieldData", [vm = cls.getHandle()](Procgen* value, Heightmap* heightmap, float originX,
                                                          float originZ, float cellSize, float heightScale) {
        const auto reference = nativeProxyReference<ProcgenHeightmapHandleRef>(heightmap);
        if (!value || !reference)
            return makeOwnedSpatialProxy(
                vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "heightfieldData requires an owned heightmap",
                        "heightmap", {}, "procgen.squirrel")));
        return makeOwnedSpatialProxy(vm,
                                     value->heightfieldDataHandle(*reference, originX, originZ, cellSize, heightScale));
    });
    cls.addFunc("textureMaskData", [vm = cls.getHandle()](Procgen* value, Heightmap* values, float originX,
                                                          float originZ, float cellSize, float minValue, float maxValue,
                                                          float minY, float maxY) {
        const auto reference = nativeProxyReference<ProcgenHeightmapHandleRef>(values);
        if (!value || !reference)
            return makeOwnedSpatialProxy(
                vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "textureMaskData requires an owned scalar map", "values",
                        {}, "procgen.squirrel")));
        return makeOwnedSpatialProxy(
            vm, value->textureMaskDataHandle(*reference, originX, originZ, cellSize, minValue, maxValue, minY, maxY));
    });
    cls.addFunc("meshSurfaceData", [vm = cls.getHandle()](Procgen* value, MeshBuild* mesh, float tolerance) {
        const auto reference = nativeProxyReference<ProcgenMeshBuildHandleRef>(mesh);
        if (!value || !reference)
            return makeOwnedSpatialProxy(
                vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "meshSurfaceData requires an owned mesh build", "mesh",
                        {}, "procgen.squirrel")));
        return makeOwnedSpatialProxy(vm, value->meshSurfaceDataHandle(*reference, tolerance));
    });
    cls.addFunc("unionSpatial", [vm = cls.getHandle()](Procgen* value, SpatialData* first, SpatialData* second) {
        const auto firstRef  = nativeProxyReference<ProcgenSpatialDataHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedSpatialProxy(
                vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "unionSpatial requires owned spatial data", "spatial", {},
                        "procgen.squirrel")));
        return makeOwnedSpatialProxy(vm, value->unionSpatialHandle(*firstRef, *secondRef));
    });
    cls.addFunc("intersectSpatial", [vm = cls.getHandle()](Procgen* value, SpatialData* first, SpatialData* second) {
        const auto firstRef  = nativeProxyReference<ProcgenSpatialDataHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedSpatialProxy(
                vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "intersectSpatial requires owned spatial data", "spatial",
                        {}, "procgen.squirrel")));
        return makeOwnedSpatialProxy(vm, value->intersectSpatialHandle(*firstRef, *secondRef));
    });
    cls.addFunc("differenceSpatial", [vm = cls.getHandle()](Procgen* value, SpatialData* first, SpatialData* second) {
        const auto firstRef  = nativeProxyReference<ProcgenSpatialDataHandleRef>(first);
        const auto secondRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(second);
        if (!value || !firstRef || !secondRef)
            return makeOwnedSpatialProxy(
                vm, eve::Result<ProcgenSpatialDataHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "differenceSpatial requires owned spatial data",
                        "spatial", {}, "procgen.squirrel")));
        return makeOwnedSpatialProxy(vm, value->differenceSpatialHandle(*firstRef, *secondRef));
    });
    cls.addFunc("sampleSpatial", [vm = cls.getHandle()](Procgen* value, SpatialData* spatial, float spacing,
                                                        uint32_t seed, float jitter) {
        const auto reference = nativeProxyReference<ProcgenSpatialDataHandleRef>(spatial);
        if (!value || !reference)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "sampleSpatial requires owned spatial data", "spatial",
                        {}, "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->sampleSpatialHandle(*reference, spacing, seed, jitter));
    });
    cls.addFunc(
        "filterSpatial", [vm = cls.getHandle()](Procgen* value, PointSet* points, SpatialData* spatial, bool invert) {
            const auto pointsRef  = nativeProxyReference<ProcgenPointSetHandleRef>(points);
            const auto spatialRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(spatial);
            if (!value || !pointsRef || !spatialRef)
                return makeOwnedPointSetProxy(
                    vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument, "filterSpatial requires owned inputs", "input", {},
                            "procgen.squirrel")));
            return makeOwnedPointSetProxy(vm, value->filterSpatialHandle(*pointsRef, *spatialRef, invert));
        });
    cls.addFunc("projectToSpatial", [vm = cls.getHandle()](Procgen* value, PointSet* points, SpatialData* spatial) {
        const auto pointsRef  = nativeProxyReference<ProcgenPointSetHandleRef>(points);
        const auto spatialRef = nativeProxyReference<ProcgenSpatialDataHandleRef>(spatial);
        if (!value || !pointsRef || !spatialRef)
            return makeOwnedPointSetProxy(
                vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "projectToSpatial requires owned inputs", "input", {},
                        "procgen.squirrel")));
        return makeOwnedPointSetProxy(vm, value->projectToSpatialHandle(*pointsRef, *spatialRef));
    });
    cls.addFunc("splineData", [vm = cls.getHandle()](Procgen* value, PointSet* points, float radius) -> ssq::Table {
        const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(points);
        if (!value || !reference)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "splineData requires an owned point set",
                                                                      "points", {}, "procgen.squirrel"))
                        .status());
        return makeOwnedSpatialProxy(vm, value->splineDataHandle(*reference, radius));
    });
    cls.addFunc("sampleSpline",
                [vm = cls.getHandle()](Procgen* value, PointSet* points, float spacing, uint32_t seed,
                                       float jitter) -> ssq::Table {
                    const auto reference = nativeProxyReference<ProcgenPointSetHandleRef>(points);
                    if (!value || !reference)
                        return eve::script::projectStatusResult(
                            vm, eve::Result<void>::failure(
                                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                           "sampleSpline requires an owned point set", "points", {},
                                                           "procgen.squirrel"))
                                    .status());
                    return makeOwnedPointSetProxy(vm, value->sampleSplineHandle(*reference, spacing, seed, jitter));
                });
    cls.addFunc("filterSplineDistance",
                [vm = cls.getHandle()](Procgen* value, PointSet* input, PointSet* spline, float minDistance,
                                       float maxDistance) -> ssq::Table {
                    const auto inputRef  = nativeProxyReference<ProcgenPointSetHandleRef>(input);
                    const auto splineRef = nativeProxyReference<ProcgenPointSetHandleRef>(spline);
                    if (!value || !inputRef || !splineRef)
                        return eve::script::projectStatusResult(
                            vm, eve::Result<void>::failure(
                                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                           "filterSplineDistance requires owned point sets", "points",
                                                           {}, "procgen.squirrel"))
                                    .status());
                    return makeOwnedPointSetProxy(
                        vm, value->filterSplineDistanceHandle(*inputRef, *splineRef, minDistance, maxDistance));
                });
    cls.addFunc("selfPrune", [vm = cls.getHandle()](Procgen* value, PointSet* input, float radius) -> ssq::Table {
        const auto inputRef = nativeProxyReference<ProcgenPointSetHandleRef>(input);
        if (!value || !inputRef)
            return eve::script::projectStatusResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "selfPrune requires owned points", "points", {},
                                                                      "procgen.squirrel"))
                        .status());
        return makeOwnedPointSetProxy(vm, value->selfPruneHandle(*inputRef, radius));
    });
    cls.addFunc(
        "publishCellInstances",
        [vm = cls.getHandle()](Procgen* value, const std::string& prefix, ProcgenCellRequest* request, PointSet* points,
                               const std::string& assetAttribute, const std::string& defaultAsset) {
            const auto pointsRef = nativeProxyReference<ProcgenPointSetHandleRef>(points);
            if (!value || !request || !pointsRef)
                return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                          eve::DiagnosticCode::InvalidArgument,
                                                          "publishCellInstances requires request and owned points",
                                                          "publishCellInstances", {}, "procgen.squirrel")));
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
                eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument,
                    "publishCellSnapshot requires request, owned points, and a non-zero decimal target revision",
                    "publishCellSnapshot", {}, "procgen.squirrel")),
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
                eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument,
                    "publishCellInstanceDelta requires request, delta, and a decimal target revision greater than one",
                    "publishCellInstanceDelta", {}, "procgen.squirrel")),
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
                            eve::Result<std::uint64_t>::failure(
                                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                       "synchronizeCellInstances requires runtime and request",
                                                       "synchronizeCellInstances", {}, "procgen.squirrel")),
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
                  : eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "synchronizeCellInstancesAtomic requires Procgen",
                        "procgen", {}, "procgen.squirrel")),
            [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
    });
    cls.addFunc("removeCellInstances",
                [vm = cls.getHandle()](Procgen* value, const std::string& prefix, ProcgenCellRequest* request) {
                    if (!value || !request)
                        return eve::script::projectResult(
                            vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "removeCellInstances requires a request",
                                    "request", {}, "procgen.squirrel")));
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
                  : eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "removeCellInstancesAtomic requires Procgen", "procgen",
                        {}, "procgen.squirrel")),
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
                  : eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "completeCellCleanupAtomic requires Procgen", "procgen",
                        {}, "procgen.squirrel")),
            [](std::uint64_t removed) { return eve::Value(std::to_string(removed)); });
    });
    cls.addFunc("removeInstances", [vm = cls.getHandle()](Procgen* value, const std::string& batchId) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "removeInstances requires Procgen", "procgen", {},
                                                                      "procgen.squirrel")));
        return eve::script::projectResult(vm, value->removeInstances(batchId));
    });
    cls.addFunc(
        "generateTexture",
        [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params,
                               graphics::Graphics* gfx) -> ssq::Table {
            if (!value || !params || !gfx)
                return eve::script::projectStatusResult(
                    vm,
                    eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "generateTexture requires params and Graphics",
                                                                      "generateTexture", {}, "procgen.squirrel"))
                        .status());
            return projectBorrowedResult(vm, value->generateTextureBorrowed(recipe, params->reference, gfx), "texture");
        });
    cls.addFunc("generateMesh",
                [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params,
                                       graphics::Graphics* gfx) -> ssq::Table {
                    if (!value || !params || !gfx)
                        return eve::script::projectStatusResult(
                            vm, eve::Result<void>::failure(
                                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                           "generateMesh requires params and Graphics", "generateMesh",
                                                           {}, "procgen.squirrel"))
                                    .status());
                    return projectBorrowedResult(vm, value->generateMeshBorrowed(recipe, params->reference, gfx),
                                                 "mesh");
                });
    cls.addFunc("uploadMesh",
                [vm = cls.getHandle()](Procgen* value, MeshBuild* mesh, graphics::Graphics* gfx) -> ssq::Table {
                    if (!value || !mesh || !gfx)
                        return eve::script::projectStatusResult(
                            vm,
                            eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                              "uploadMesh requires a mesh and Graphics",
                                                                              "uploadMesh", {}, "procgen.squirrel"))
                                .status());
                    return projectBorrowedResult(vm, value->uploadMeshBorrowed(*mesh, *gfx), "mesh");
                });
    cls.addFunc("configureGtsTerrainTileLods",
                [vm = cls.getHandle()](Procgen* value, const GtsTerrainLodSet* lods, int tileIndex,
                                       graphics::Renderable3D* renderable, graphics::Graphics* gfx, float worldDiameter,
                                       float verticalFovDegrees, float originX, float originY, float originZ) {
                    if (!value || !lods || !renderable || !gfx)
                        return eve::script::projectResult(
                            vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "configureGtsTerrainTileLods requires LODs, Renderable3D and Graphics", "mesh", {},
                                    "procgen.squirrel")));
                    return eve::script::projectResult(vm,value->configureGtsTerrainTileLods(
                        *lods,tileIndex,*renderable,*gfx,worldDiameter,verticalFovDegrees,originX,originY,originZ));
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
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "commitSystem requires a context proxy",
                                                                      "context", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, value->commitSystem(context->reference));
    });
    cls.addFunc("abortSystem", [vm = cls.getHandle()](Procgen* value, ScriptProcgenContext* context) {
        if (!value || !context)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "abortSystem requires a context proxy", "context",
                                                                      {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, value->abortSystem(context->reference));
    });
    cls.addFunc("getSystemOutput",
                [vm = cls.getHandle()](Procgen* value, const std::string& system, const std::string& output) {
                    if (!value)
                        return makeOwnedPointSetProxy(
                            vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "Procgen module must not be null", "procgen",
                                    {}, "procgen.squirrel")));
                    return makeOwnedPointSetProxy(vm, value->getSystemOutputHandle(system, output));
                });
    cls.addFunc("getSystemDebugStage",
                [vm = cls.getHandle()](Procgen* value, const std::string& system, const std::string& stage) {
                    if (!value)
                        return makeOwnedPointSetProxy(
                            vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "Procgen module must not be null", "procgen",
                                    {}, "procgen.squirrel")));
                    return makeOwnedPointSetProxy(vm, value->getSystemDebugStageHandle(system, stage));
                });
    cls.addFunc("getPreviousSystemDebugStage",
                [vm = cls.getHandle()](Procgen* value, const std::string& system, const std::string& stage) {
                    if (!value)
                        return makeOwnedPointSetProxy(
                            vm, eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "Procgen module must not be null", "procgen",
                                    {}, "procgen.squirrel")));
                    return makeOwnedPointSetProxy(vm, value->getPreviousSystemDebugStageHandle(system, stage));
                });
    cls.addFunc("removeSystem", [vm = cls.getHandle()](Procgen* value, const std::string& name) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "removeSystem requires a Procgen module",
                                                                      "procgen", {}, "procgen.squirrel")));
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
                vm, eve::Result<RecipeDescriptor>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                               "algorithm schema requires a Procgen module", "procgen", {},
                                               "procgen.squirrel"))
                        .status());
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
                            vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "algorithm defaults require a params proxy",
                                    "params", {}, "procgen.squirrel")));
                    return eve::script::projectResult(vm, value->applyAlgorithmDefaults(algorithm, params->reference));
                });
    cls.addFunc("generateTo", [vm = cls.getHandle()](Procgen* value, const std::string& algorithm,
                                                     ScriptProcgenParams* params, OutputSpec* output) {
        const auto outputRef = nativeProxyReference<ProcgenOutputHandleRef>(output);
        if (!value || !params || !outputRef)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "generateTo requires params and owned output",
                                                                      "generateTo", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, value->generateTo(algorithm, params->reference, *outputRef));
    });
    cls.addFunc("autotileGrid", [vm = cls.getHandle()](Procgen* value, Grid2D* grid) {
        const auto gridRef = nativeProxyReference<ProcgenGridHandleRef>(grid);
        if (!value || !gridRef)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "autotileGrid requires an owned grid proxy",
                                                                      "grid", {}, "procgen.squirrel")));
        return eve::script::projectResult(vm, value->autotileGrid(*gridRef));
    });
    cls.addFunc("randomSeed", &Procgen::randomSeed);
    cls.addFunc("gridToJson", [vm = cls.getHandle()](Procgen* value, Grid2D* grid) {
        const auto gridRef = nativeProxyReference<ProcgenGridHandleRef>(grid);
        if (!value || !gridRef)
            return eve::script::projectResult(
                vm,
                eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                         "gridToJson requires an owned grid proxy",
                                                                         "grid", {}, "procgen.squirrel")),
                [](std::string&& json) { return eve::Value(std::move(json)); });
        return eve::script::projectResult(vm, value->gridToJson(*gridRef),
                                          [](std::string&& json) { return eve::Value(std::move(json)); });
    });
    cls.addFunc("getTextureRecipeCount", &Procgen::getTextureRecipeCount);
    cls.addFunc("getTextureRecipeId", &Procgen::getTextureRecipeId);
    cls.addFunc("hasTextureRecipe", &Procgen::hasTextureRecipe);
    cls.addFunc("getTextureRecipeSchema", [vm = cls.getHandle()](Procgen* value, const std::string& recipe) {
        if (!value)
            return eve::script::projectStatusResult(
                vm, eve::Result<RecipeDescriptor>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                               "texture schema requires a Procgen module", "procgen", {},
                                               "procgen.squirrel"))
                        .status());
        return projectRecipeDescriptorResult(vm, value->getTextureRecipeSchema(recipe));
    });
    cls.addFunc("applyTextureRecipeDefaults",
                [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params) {
                    if (!value || !params)
                        return eve::script::projectResult(
                            vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "texture defaults require a params proxy",
                                    "params", {}, "procgen.squirrel")));
                    return eve::script::projectResult(vm, value->applyTextureRecipeDefaults(recipe, params->reference));
                });
    cls.addFunc("getPbrRecipeCount", &Procgen::getPbrRecipeCount);
    cls.addFunc("getPbrRecipeId", &Procgen::getPbrRecipeId);
    cls.addFunc("hasPbrRecipe", &Procgen::hasPbrRecipe);
    cls.addFunc("getPbrRecipeSchema", [vm = cls.getHandle()](Procgen* value, const std::string& recipe) {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                eve::Result<RecipeDescriptor>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                              "PBR schema requires a Procgen module",
                                                                              "procgen", {}, "procgen.squirrel"))
                    .status());
        return projectRecipeDescriptorResult(vm, value->getPbrRecipeSchema(recipe));
    });
    cls.addFunc("applyPbrRecipeDefaults",
                [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params) {
                    if (!value || !params)
                        return eve::script::projectResult(
                            vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                                  "PBR defaults require a params proxy",
                                                                                  "params", {}, "procgen.squirrel")));
                    return eve::script::projectResult(vm, value->applyPbrRecipeDefaults(recipe, params->reference));
                });
    cls.addFunc(
        "buildArtifact", [vm = cls.getHandle()](Procgen* value, const std::string& recipeId,
                                                ScriptProcgenParams* params, const std::string& artifactIdentity) {
            if (!value)
                return eve::script::projectResult(
                    vm,
                    eve::Result<GeneratedArtifact>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                                   "procgen module must not be null",
                                                                                   "procgen", {}, "procgen.squirrel")),
                    [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
            const auto parsed = ArtifactId::parse(artifactIdentity);
            if (!parsed || parsed->isNil())
                return eve::script::projectResult(
                    vm,
                    eve::Result<GeneratedArtifact>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "artifact identity must be a non-nil canonical UUID",
                        "artifactIdentity", {}, "procgen.squirrel")),
                    [](GeneratedArtifact&& artifact) { return artifactProjection(std::move(artifact)); });
            if (!params)
                return eve::script::projectResult(
                    vm,
                    eve::Result<GeneratedArtifact>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "buildArtifact requires a params proxy", "params", {},
                        "procgen.squirrel")),
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
                    eve::Result<ArtifactPublishReceipt>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "procgen module must not be null",
                                               "procgen", {}, "procgen.squirrel")),
                    [](ArtifactPublishReceipt&& receipt) { return publishReceiptProjection(std::move(receipt)); });
            const auto parsed = ArtifactId::parse(artifactIdentity);
            if (!parsed || parsed->isNil())
                return eve::script::projectResult(
                    vm,
                    eve::Result<ArtifactPublishReceipt>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "artifact identity must be a non-nil canonical UUID",
                        "artifactIdentity", {}, "procgen.squirrel")),
                    [](ArtifactPublishReceipt&& receipt) { return publishReceiptProjection(std::move(receipt)); });
            ArtifactPublishOptions options;
            options.scene    = scene;
            options.graphics = graphics;
            options.physics  = physics;
            options.map      = map;
            if (!params)
                return eve::script::projectResult(
                    vm,
                    eve::Result<ArtifactPublishReceipt>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "publishArtifact requires a params proxy", "params", {},
                        "procgen.squirrel")),
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
                eve::Result<RecipeDescriptor>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                              "mesh schema requires a Procgen module",
                                                                              "procgen", {}, "procgen.squirrel"))
                    .status());
        return projectRecipeDescriptorResult(vm, value->getMeshRecipeSchema(recipe));
    });
    cls.addFunc("applyMeshRecipeDefaults",
                [vm = cls.getHandle()](Procgen* value, const std::string& recipe, ScriptProcgenParams* params) {
                    if (!value || !params)
                        return eve::script::projectResult(
                            vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "mesh defaults require a params proxy",
                                    "params", {}, "procgen.squirrel")));
                    return eve::script::projectResult(vm, value->applyMeshRecipeDefaults(recipe, params->reference));
                });
}


}  // namespace eve::procgen
