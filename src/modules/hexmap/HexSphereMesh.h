#pragma once
#include "common/Export.h"


/** @file HexSphereMesh.h @brief Mesh generation for the spherical hex map surface. */

#include "hexmap/HexMeshData.h"
#include "hexmap/HexSphereMap.h"

namespace eve::hexmap {

/**
 * @brief Builds the ground, terrace and cliff surface of a spherical hex map.
 *
 * This is the spherical counterpart of `buildTerrainMesh`. The whole sphere is one
 * mesh, because the topology gives every cell a global id and the surface has no
 * chunk borders to hide a seam in.
 *
 * The construction is the same corner-matrix scheme the planar builder uses, with
 * the three planar assumptions replaced:
 *
 * - **Edge count.** A cell has five edges if it is pentagonal and six otherwise, so
 *   nothing iterates a fixed six directions.
 * - **Edge ownership.** The planar builder leans on the chunk grid (`direction <= SE`)
 *   to emit every shared boundary exactly once. A sphere has no chunk grid, so the
 *   *lower cell id* owns the edge instead: the strip across `cell -> neighbour` is
 *   built only while `cell < neighbour`. The rule is a total order on the same set of
 *   edges, so it still emits each shared boundary exactly once.
 * - **"Vertical" is radial.** Positions are built as a unit direction times a radius,
 *   and the terrace interpolation of a slope moves *along the surface* by the
 *   horizontal step and *outward* by the vertical step, instead of moving in XZ and Y.
 *
 * All three terrain layers are encoded exactly as the planar builder encodes them
 * (see `HexTerrainVertexCode`), so `mesh3d`'s terrain shader consumes this mesh
 * unchanged.
 *
 * @param map Source map. An empty map produces an empty mesh.
 * @param out Destination mesh; cleared and finalized by this call.
 */
EVENGINE_API_WORLD void buildSphereTerrainMesh(const HexSphereMap& map, HexMeshData& out);

/**
 * @brief Builds the ocean surface of a spherical hex map.
 *
 * Only flooded cells contribute. Unlike the planar water stream there is no shore
 * band: a flooded cell draws its whole hexagonal cap at its water radius, so two
 * neighbouring flooded cells share their corners exactly and the ocean is one
 * closed sheet with a clean edge where it meets land.
 *
 * The texture coordinates follow the same contract as the planar water mesh: `u` is
 * the shore parameter (`0` in open ocean, `1` where the water ends against land)
 * and `v` is reserved.
 *
 * @param map Source map. An empty map, or one with no flooded cell, produces an
 *            empty mesh.
 * @param out Destination mesh; cleared and finalized by this call.
 */
void buildSphereWaterMesh(const HexSphereMap& map, HexMeshData& out);

}  // namespace eve::hexmap
