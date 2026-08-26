#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

class MeshRecipeRegistry;

/**
 * @brief Build a complete, walkable, multi-ring medieval castle.
 *
 * The recipe creates concentric crenellated walls, corner and interval towers,
 * gatehouses, an elevated central keep, wall walks and physical stair flights.
 * Geometry is deterministic for a given seed.
 *
 * Important parameters: width/depth, rings, wallHeight, wallThickness,
 * towerRadius, towerSides, towerHeight, towerSpacing, ringInset,
 * ringHeightStep, keepWidth, keepDepth, keepFloors, floorHeight, stairWidth,
 * stepHeight, merlonWidth, gateWidth, courtyardBuildings, detail (0..2), scale.
 */
bool generateCastleMesh(const Params &params, MeshBuild &out, std::string &error);

/** @brief Register mesh.castle in the built-in mesh recipe registry. */
void registerCastleMeshRecipe(MeshRecipeRegistry &registry);

}  // namespace eve::procgen
