#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

/**
 * @brief Build a deterministic, flat-top hexagonal continent mesh.
 *
 * The generator derives elevation, temperature, moisture, drainage and biome
 * from the seed, including river confluences and elevated basin lakes. UV.x
 * stores the primary biome id plus a fractional transition
 * weight; UV.y stores the secondary biome id plus river coverage. This compact
 * encoding keeps the mesh compatible with the engine's existing MeshVertex.
 * The output metadata includes `cells.<biome>`, `cells.river`, `cells.lake`
 * and `edges.cliff` counts for validation and tooling.
 *
 * @param params width, height, radius, seed, seaLevel, heightScale and riverCount.
 * @param out Destination CPU mesh.
 * @param error Failure description.
 * @return True when a non-empty mesh was generated.
 */
bool generateHexTerrainMesh(const Params& params, MeshBuild& out, std::string& error);

}  // namespace eve::procgen
