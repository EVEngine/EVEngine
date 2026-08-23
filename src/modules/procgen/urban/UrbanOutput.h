#pragma once

#include <string>

namespace eve::procgen {
class Grid2D;
class MeshBuild;
class Params;
class GeneratorRegistry;
class MeshRecipeRegistry;

namespace urban {

struct UrbanOptions;

/**
 * @brief Parse Procgen Params into urban generator options.
 * Params: land/landPoints, minParcelArea, targetParcels, maxLevels, lambdaSize,
 * lambdaRegu, lambdaAcce, lambdaOrient, gammaAngle, gammaSide, accessThreshold,
 * shortEdgeFactor, streetWidth, streetPattern, culDeSacAfterLevel, orientation,
 * boundaryStreet, boundaryStreetFraction, dijkstraJunctionWeight, optimize,
 * optimizeIterations.
 */
bool parseUrbanOptions(const Params& params, UrbanOptions& opts, std::string& error);

/** @brief Register "urban.parcels" (Grid2D) and "mesh.urban" (MeshBuild) builtins. */
void registerUrbanGenerators(GeneratorRegistry& registry);
void registerUrbanMeshRecipes(MeshRecipeRegistry& registry);

/** @brief Grid2D rasterization of the urban layout (Semantic::Road/Floor/Wall + parcel detail). */
bool generateUrbanGrid(const Params& params, Grid2D& out, std::string& error);
/** @brief MeshBuild of parcel blocks + street ribbons (flat or extruded, Y-up). */
bool generateUrbanMesh(const Params& params, MeshBuild& out, std::string& error);

}  // namespace urban
}  // namespace eve::procgen
