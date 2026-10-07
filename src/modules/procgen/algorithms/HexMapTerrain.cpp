/**
 * @file HexMapTerrain.cpp
 * @brief Procgen wrappers that bake hexmap terrain without owning the live map.
 *
 * Distinct from HexTerrain.cpp (`generateHexTerrainMesh`), which builds a
 * flat-top continent mesh and is unrelated to the hexmap grid.
 */

#include "procgen/algorithms/HexTerrainBindings.h"

#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMapGenerator.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexSphereGenerator.h"
#include "hexmap/HexSphereMap.h"
#include "hexmap/HexSphereTopology.h"
#include "hexmap/HexTerrainBake.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Procgen.h"
#include "procgen/ProcgenScriptObjects.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

namespace eve::procgen {
namespace {

[[nodiscard]] hexmap::HexMapGeneratorSettings planarSettings(const Params& params) {
    hexmap::HexMapGeneratorSettings settings;
    settings.seed                 = params.getSeed();
    settings.highRiseProbability  = params.getFloat("highRiseProbability", settings.highRiseProbability);
    settings.sinkProbability      = params.getFloat("sinkProbability", settings.sinkProbability);
    settings.jitterProbability    = params.getFloat("jitterProbability", settings.jitterProbability);
    settings.chunkSizeMin         = params.getInt("chunkSizeMin", settings.chunkSizeMin);
    settings.chunkSizeMax         = params.getInt("chunkSizeMax", settings.chunkSizeMax);
    settings.landPercentage       = params.getInt("landPercentage", settings.landPercentage);
    settings.waterLevel           = params.getInt("waterLevel", settings.waterLevel);
    settings.elevationMinimum     = params.getInt("elevationMinimum", settings.elevationMinimum);
    settings.elevationMaximum     = params.getInt("elevationMaximum", settings.elevationMaximum);
    settings.mapBorderX           = params.getInt("mapBorderX", settings.mapBorderX);
    settings.mapBorderZ           = params.getInt("mapBorderZ", settings.mapBorderZ);
    settings.regionBorder         = params.getInt("regionBorder", settings.regionBorder);
    settings.regionCount          = params.getInt("regionCount", settings.regionCount);
    settings.erosionPercentage    = params.getInt("erosionPercentage", settings.erosionPercentage);
    settings.startingMoisture     = params.getFloat("startingMoisture", settings.startingMoisture);
    settings.evaporationFactor    = params.getFloat("evaporationFactor", settings.evaporationFactor);
    settings.precipitationFactor  = params.getFloat("precipitationFactor", settings.precipitationFactor);
    settings.runoffFactor         = params.getFloat("runoffFactor", settings.runoffFactor);
    settings.seepageFactor        = params.getFloat("seepageFactor", settings.seepageFactor);
    settings.windStrength         = params.getFloat("windStrength", settings.windStrength);
    settings.riverPercentage      = params.getInt("riverPercentage", settings.riverPercentage);
    settings.extraLakeProbability = params.getFloat("extraLakeProbability", settings.extraLakeProbability);
    settings.lowTemperature       = params.getFloat("lowTemperature", settings.lowTemperature);
    settings.highTemperature      = params.getFloat("highTemperature", settings.highTemperature);
    settings.temperatureJitter    = params.getFloat("temperatureJitter", settings.temperatureJitter);
    const int wind                = params.getInt("windDirection", static_cast<int>(settings.windDirection));
    if (wind >= 0 && wind < hexmap::kHexDirectionCount)
        settings.windDirection = static_cast<hexmap::HexDirection>(wind);
    return settings;
}

[[nodiscard]] hexmap::HexSphereGeneratorSettings sphereSettings(const Params& params) {
    hexmap::HexSphereGeneratorSettings settings;
    settings.seed           = params.getSeed();
    settings.landPercentage = params.getInt("landPercentage", settings.landPercentage);
    settings.waterLevel     = params.getInt("waterLevel", settings.waterLevel);
    settings.octaves        = params.getInt("octaves", settings.octaves);
    settings.frequency      = params.getFloat("frequency", settings.frequency);
    return settings;
}

bool refuseBakeGrid(const char* name, const char* entry, std::string& error) {
    error = std::string(name) + " produces a hexmap bake; call procgen." + entry;
    return false;
}

[[nodiscard]] ssq::Table projectBake(HSQUIRRELVM vm, ScriptProcgenParams*                  params,
                                     Result<Value> (*generate)(const Params&), const char* entry) {
    if (params == nullptr)
        return script::projectStatusResult(
            vm, Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         std::string(entry) + " params proxy must not be null",
                                                         "params", {}, "procgen.squirrel"))
                    .status());
    auto view = Procgen::resolve(params->reference);
    if (!view.isBound())
        return script::projectStatusResult(
            vm, Result<Value>::failure(Diagnostic::error(DiagnosticCode::StaleHandle,
                                                         std::string(entry) + " parameters handle is stale", "params",
                                                         {}, "procgen.squirrel"))
                    .status());
    return script::projectResult(vm, generate(*view), [](Value value) { return value; });
}

}  // namespace

void registerHexTerrainAlgorithms(GeneratorRegistry& registry) {
    RecipeDescriptor terrain;
    terrain.id          = "hex.terrain";
    terrain.displayName = "Hex Terrain";
    terrain.category    = "Hex Map";
    terrain.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
    terrain.params.push_back(ParamDescriptor::integer("width", "Width", 20, hexmap::HexMetrics::kChunkSizeX,
                                                      hexmap::kMaxHexGridDimension, hexmap::HexMetrics::kChunkSizeX));
    terrain.params.push_back(ParamDescriptor::integer("height", "Height", 15, hexmap::HexMetrics::kChunkSizeZ,
                                                      hexmap::kMaxHexGridDimension, hexmap::HexMetrics::kChunkSizeZ));
    terrain.params.push_back(ParamDescriptor::integer("landPercentage", "Land Percentage", 50, 1, 99));
    terrain.params.push_back(ParamDescriptor::integer("waterLevel", "Water Level", 3, 0, 8));
    terrain.params.push_back(ParamDescriptor::integer("riverPercentage", "River Percentage", 10, 0, 50));
    terrain.params.push_back(ParamDescriptor::integer("regionCount", "Region Count", 1, 1, 4));
    terrain.params.push_back(ParamDescriptor::integer("mapBorderX", "Map Border X", 5, 0, 64));
    terrain.params.push_back(ParamDescriptor::integer("mapBorderZ", "Map Border Z", 5, 0, 64));
    terrain.params.push_back(ParamDescriptor::integer("regionBorder", "Region Border", 5, 0, 64));
    terrain.params.push_back(ParamDescriptor::integer("erosionPercentage", "Erosion Percentage", 50, 0, 100));
    terrain.params.push_back(
        ParamDescriptor::floating("highRiseProbability", "High Rise Probability", 0.25f, 0.f, 1.f, 0.01f));
    terrain.params.push_back(ParamDescriptor::floating("sinkProbability", "Sink Probability", 0.2f, 0.f, 1.f, 0.01f));
    terrain.params.push_back(
        ParamDescriptor::floating("jitterProbability", "Jitter Probability", 0.25f, 0.f, 1.f, 0.01f));
    terrain.params.push_back(ParamDescriptor::integer("windDirection", "Wind Direction", 5, 0, 5));
    terrain.params.push_back(ParamDescriptor::floating("windStrength", "Wind Strength", 4.f, 0.f, 16.f, 0.1f));
    registry.registerAlgorithm(std::move(terrain), [](const Params&, Grid2D&, std::string& error) {
        return refuseBakeGrid("hex.terrain", "generateHexTerrain", error);
    });

    RecipeDescriptor sphere;
    sphere.id          = "hex.sphere";
    sphere.displayName = "Hex Sphere";
    sphere.category    = "Hex Map";
    sphere.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
    sphere.params.push_back(
        ParamDescriptor::integer("subdivision", "Subdivision", 4, 0, hexmap::kMaxHexSphereSubdivision));
    sphere.params.push_back(ParamDescriptor::floating("radius", "Radius", 100.f, 1.f, 10000.f, 1.f));
    sphere.params.push_back(ParamDescriptor::integer("landPercentage", "Land Percentage", 45, 1, 99));
    sphere.params.push_back(ParamDescriptor::integer("waterLevel", "Water Level", 0, -4, 8));
    sphere.params.push_back(ParamDescriptor::integer("octaves", "Octaves", 5, 1, 16));
    sphere.params.push_back(ParamDescriptor::floating("frequency", "Frequency", 1.55f, 0.1f, 16.f, 0.05f));
    registry.registerAlgorithm(std::move(sphere), [](const Params&, Grid2D&, std::string& error) {
        return refuseBakeGrid("hex.sphere", "generateHexSphere", error);
    });
}

Result<Value> generateHexTerrain(const Params& params) {
    hexmap::HexMap map;
    auto           reset = map.reset(params.getWidth(), params.getHeight(), params.getSeed());
    if (!reset) return Result<Value>::failure(reset.status());
    auto generated = hexmap::generateHexMap(map, planarSettings(params));
    if (!generated) return Result<Value>::failure(generated.status());
    return Result<Value>::success(hexmap::hexTerrainBakeToValue(hexmap::snapshotHexTerrain(map)));
}

Result<Value> generateHexSphere(const Params& params) {
    const std::int32_t   subdivision = params.getInt("subdivision", 4);
    const float          radius      = params.getFloat("radius", 100.f);
    hexmap::HexSphereMap map;
    auto                 reset = map.reset(subdivision, radius, params.getSeed());
    if (!reset) return Result<Value>::failure(reset.status());
    auto generated = hexmap::generateSphereMap(map, sphereSettings(params));
    if (!generated) return Result<Value>::failure(generated.status());
    return Result<Value>::success(hexmap::hexTerrainBakeToValue(hexmap::snapshotHexSphereTerrain(map)));
}

void exposeHexTerrain(ssq::Class& cls) {
    cls.addFunc("generateHexTerrain", [vm = cls.getHandle()](Procgen*, ScriptProcgenParams* params) -> ssq::Table {
        return projectBake(vm, params, generateHexTerrain, "generateHexTerrain");
    });
    cls.addFunc("generateHexSphere", [vm = cls.getHandle()](Procgen*, ScriptProcgenParams* params) -> ssq::Table {
        return projectBake(vm, params, generateHexSphere, "generateHexSphere");
    });
}

}  // namespace eve::procgen
