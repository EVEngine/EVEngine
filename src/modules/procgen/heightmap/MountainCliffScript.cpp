#include "procgen/heightmap/CliffBandMesh.h"
#include "procgen/heightmap/CliffDressing.h"
#include "procgen/heightmap/MountainRecipe.h"
#include "procgen/heightmap/TerrainStampScript.h"

#include "procgen/MeshBuild.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace eve::procgen {
namespace {

void exposeMountainCliffBindings(ssq::Table& table) {
    auto mountain = table.addClass("MountainRecipeSettings", ssq::Class::Ctor<MountainRecipeSettings()>());
    mountain.addFunc("setPreset", [](MountainRecipeSettings* s, int preset) {
        s->preset = static_cast<MountainPreset>(std::clamp(preset, 0, 2));
    });
    mountain.addFunc("getPreset", [](const MountainRecipeSettings* s) { return static_cast<int>(s->preset); });
    mountain.addVar("width", &MountainRecipeSettings::width);
    mountain.addVar("height", &MountainRecipeSettings::height);
    mountain.addFunc("setSeed",
                     [](MountainRecipeSettings* s, int seed) { s->seed = static_cast<std::uint32_t>(seed); });
    mountain.addFunc("getSeed", [](const MountainRecipeSettings* s) { return static_cast<int>(s->seed); });
    mountain.addVar("spacingX", &MountainRecipeSettings::spacingX);
    mountain.addVar("spacingZ", &MountainRecipeSettings::spacingZ);
    mountain.addVar("heightScale", &MountainRecipeSettings::heightScale);
    mountain.addVar("cliffSlopeMin", &MountainRecipeSettings::cliffSlopeMin);
    mountain.addVar("cliffSlopeMax", &MountainRecipeSettings::cliffSlopeMax);
    mountain.addVar("frequencyOverride", &MountainRecipeSettings::frequencyOverride);
    mountain.addVar("ridgeOverride", &MountainRecipeSettings::ridgeOverride);
    mountain.addVar("warpOverride", &MountainRecipeSettings::warpOverride);
    mountain.addVar("amplitudeOverride", &MountainRecipeSettings::amplitudeOverride);
    mountain.addVar("thermalIterations", &MountainRecipeSettings::thermalIterations);
    mountain.addVar("hydraulicIterations", &MountainRecipeSettings::hydraulicIterations);
    mountain.addVar("fluvialIterations", &MountainRecipeSettings::fluvialIterations);
    mountain.addFunc("setSize", [](MountainRecipeSettings* s, int width, int height) {
        s->width  = width;
        s->height = height;
    });

    auto cliff = table.addClass("CliffBandMeshSettings", ssq::Class::Ctor<CliffBandMeshSettings()>());
    cliff.addVar("originX", &CliffBandMeshSettings::originX);
    cliff.addVar("originZ", &CliffBandMeshSettings::originZ);
    cliff.addVar("spacingX", &CliffBandMeshSettings::spacingX);
    cliff.addVar("spacingZ", &CliffBandMeshSettings::spacingZ);
    cliff.addVar("heightScale", &CliffBandMeshSettings::heightScale);
    cliff.addVar("slopeMin", &CliffBandMeshSettings::slopeMin);
    cliff.addVar("slopeMax", &CliffBandMeshSettings::slopeMax);
    cliff.addVar("subdivisions", &CliffBandMeshSettings::subdivisions);
    cliff.addVar("strataAmplitude", &CliffBandMeshSettings::strataAmplitude);
    cliff.addVar("strataScale", &CliffBandMeshSettings::strataScale);
    cliff.addVar("strataVertical", &CliffBandMeshSettings::strataVertical);
    cliff.addVar("ledgeHeight", &CliffBandMeshSettings::ledgeHeight);
    cliff.addVar("outwardBias", &CliffBandMeshSettings::outwardBias);
    cliff.addFunc("setSeed", [](CliffBandMeshSettings* s, int seed) { s->seed = static_cast<std::uint32_t>(seed); });
    cliff.addFunc("getSeed", [](const CliffBandMeshSettings* s) { return static_cast<int>(s->seed); });
    cliff.addVar("maxTriangles", &CliffBandMeshSettings::maxTriangles);
    cliff.addFunc("setOrigin", [](CliffBandMeshSettings* s, float x, float z) {
        s->originX = x;
        s->originZ = z;
    });
    cliff.addFunc("setSpacing", [](CliffBandMeshSettings* s, float sx, float sz) {
        s->spacingX = sx;
        s->spacingZ = sz;
    });
}

}  // namespace

void exposeMountainCliffApis(ssq::Table& table) {
    exposeMountainCliffBindings(table);

    // Heightmap methods are registered in exposeHeightmap; extend via class lookup is awkward,
    // so free functions live on the eve root table alongside settings classes.
    table.addFunc("bakeMountainRecipe", [vm = table.getHandle()](Heightmap*                    target,
                                                                 const MountainRecipeSettings* settings,
                                                                 Heightmap* slopeMask, Heightmap* cliffFitness) {
        MountainRecipeMasks masks;
        masks.slopeMask    = slopeMask;
        masks.cliffFitness = cliffFitness;
        auto result = target && settings
                          ? bakeMountainRecipe(*target, *settings, masks)
                          : Result<int>::failure(Diagnostic::error(
                                DiagnosticCode::InvalidArgument, "bakeMountainRecipe requires heightmap and settings"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });

    table.addFunc("generateCliffFitnessMask", [vm = table.getHandle()](
                                                  Heightmap* target, const Heightmap* heights, float spacingX,
                                                  float spacingZ, float heightScale, float slopeMin, float slopeMax) {
        auto result =
            target && heights
                ? generateCliffFitnessMask(*target, *heights, spacingX, spacingZ, heightScale, slopeMin, slopeMax)
                : Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "generateCliffFitnessMask requires target and heights"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });

    table.addFunc(
        "generateCliffBandMesh",
        [vm = table.getHandle()](const Heightmap* heights, const CliffBandMeshSettings* settings,
                                 Heightmap* fitness) -> ssq::Table {
            if (!heights || !settings) {
                auto failed = Result<MeshBuild>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "generateCliffBandMesh requires heightmap and settings"));
                return eve::script::projectStatusResult(vm, failed.status());
            }
            MeshBuild mesh;
            auto      built = generateCliffBandMesh(mesh, *heights, *settings, fitness);
            if (!built.ok()) return eve::script::projectStatusResult(vm, built.status());
            built.ignore("triangle count returned via mesh meta");
            auto instance =
                eve::script::makeOwnedSquirrelInstance<MeshBuild>(vm, std::make_unique<MeshBuild>(std::move(mesh)));
            if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status());
            return eve::script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
        });

    table.addFunc("configureCliffRockScatterDefaults",
                  [vm = table.getHandle()](TerrainObjectPlacementSettings* settings, float originX, float originZ,
                                           float width, float depth, float heightScale, int seed, float spacing,
                                           float minimumFitness, const std::string& asset) {
                      auto result = settings ? configureCliffRockScatterDefaults(
                                                   *settings, originX, originZ, width, depth, heightScale,
                                                   static_cast<std::uint32_t>(seed), spacing, minimumFitness, asset)
                                             : Result<int>::failure(Diagnostic::error(
                                                   DiagnosticCode::InvalidArgument,
                                                   "configureCliffRockScatterDefaults requires placement settings"));
                      return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
                  });
}

}  // namespace eve::procgen
