#include "hexmap/HexMapMesh.h"

#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"

#include <cstdint>

namespace eve::hexmap {
namespace {

/** @brief One pending surface vertex: a world position plus its texture coordinates. */
struct SurfaceVertex {
    HexVec3 position;
    float   u = 0.f;
    float   v = 0.f;
};

/**
 * @brief Whether three positions span no area, so the face would be invisible.
 *
 * The test reuses the right-handed cross product `HexMeshData::finalize()` derives
 * its face normals from.
 */
[[nodiscard]] bool isDegenerate(HexVec3 a, HexVec3 b, HexVec3 c) noexcept {
    const HexVec3 ab = b - a;
    const HexVec3 ac = c - a;
    const HexVec3 normal{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
    return (normal.x * normal.x + normal.y * normal.y + normal.z * normal.z) <= 1e-8f;
}

/**
 * @brief Appends one triangle, perturbing every position exactly once.
 *
 * `a -> b -> c` must resolve to a `+Y` normal, otherwise the face is culled. The
 * argument order is the reference project's, so a ported `AddTriangle(a, b, c)`
 * becomes `emitTriangle(out, a, b, c)` unchanged.
 *
 * @param out Destination mesh.
 * @param noise Map noise, applied here so no call site can displace a vertex twice.
 */
void emitTriangle(HexMeshData& out, const SurfaceVertex& a, const SurfaceVertex& b, const SurfaceVertex& c,
                  const HexNoise& noise) {
    const HexVec3 pa = noise.perturb(a.position);
    const HexVec3 pb = noise.perturb(b.position);
    const HexVec3 pc = noise.perturb(c.position);
    if (isDegenerate(pa, pb, pc)) return;
    out.addTriangle(out.addVertex(pa, a.u, a.v), out.addVertex(pb, b.u, b.v), out.addVertex(pc, c.u, c.v));
}

/**
 * @brief Appends the quad whose four corners run `r0 -> r1 -> r2 -> r3` around its outline.
 *
 * `HexMeshData::addQuad(a, b, c, d)` splits the ring `a -> b -> d -> c` along `b -> c`,
 * so the corners are re-ordered here and every call site keeps readable outline order.
 * The reference project's `AddQuad(a, b, c, d)` is therefore
 * `emitQuad(out, a, b, d, c, noise)`.
 */
void emitQuad(HexMeshData& out, const SurfaceVertex& r0, const SurfaceVertex& r1, const SurfaceVertex& r2,
              const SurfaceVertex& r3, const HexNoise& noise) {
    emitTriangle(out, r0, r3, r1, noise);
    emitTriangle(out, r1, r3, r2, noise);
}

/** @brief The water surface height of a cell, or `offGridHeight` when it is off-grid. */
[[nodiscard]] float waterHeightOf(const HexMap& map, HexCoordinates coordinates, float offGridHeight) noexcept {
    return map.contains(coordinates) ? HexMetrics::waterSurfaceY(map.waterLevel(coordinates)) : offGridHeight;
}

/**
 * @brief Whether a cell exists and is flooded.
 *
 * Off-grid neighbours are *not* underwater: the reference routes them through the
 * open-water branch so the map border still gets a water triangle.
 */
[[nodiscard]] bool isWater(const HexMap& map, HexCoordinates coordinates) noexcept {
    return map.contains(coordinates) && map.isUnderwater(coordinates);
}

/**
 * @brief Emits the open-water wedge and, where the neighbour is water, its bridge.
 *
 * This is the reference project's `TriangulateOpenWater`: **one** centre triangle for
 * this direction (not a whole-cell fan), a bridge quad towards a water neighbour that
 * the owning chunk owns, and the corner triangle that closes the third cell of the
 * junction.
 *
 * @param map Source map.
 * @param coordinates Cell the wedge belongs to.
 * @param direction Direction of the wedge.
 * @param centre Water-surface centre of `coordinates`.
 * @param noise Map noise.
 * @param out Destination mesh.
 */
void appendOpenWater(const HexMap& map, HexCoordinates coordinates, HexDirection direction, HexVec3 centre,
                     const HexNoise& noise, HexMeshData& out) {
    const SurfaceVertex c1{centre + HexMetrics::firstWaterCorner(direction), 0.f, 0.f};
    const SurfaceVertex c2{centre + HexMetrics::secondWaterCorner(direction), 0.f, 0.f};
    emitTriangle(out, SurfaceVertex{centre, 0.f, 0.f}, c1, c2, noise);

    HexCoordinates neighbour{};
    if (static_cast<std::int32_t>(direction) > static_cast<std::int32_t>(HexDirection::SE) ||
        !map.getNeighbor(coordinates, direction, neighbour)) {
        // Only the NE, E and SE edges bridge outward: for a water/water boundary the
        // other side would emit the same quad, so exactly one chunk owns it. An
        // off-grid neighbour has nothing to bridge to.
        return;
    }

    const HexVec3   offset = HexMetrics::waterBridge(direction);
    const float     farY   = waterHeightOf(map, neighbour, centre.y);
    const SurfaceVertex e1{{c1.position.x + offset.x, farY, c1.position.z + offset.z}, 0.f, 0.f};
    const SurfaceVertex e2{{c2.position.x + offset.x, farY, c2.position.z + offset.z}, 0.f, 0.f};
    emitQuad(out, c1, c2, e2, e1, noise);

    if (static_cast<std::int32_t>(direction) > static_cast<std::int32_t>(HexDirection::E)) return;
    HexCoordinates nextNeighbour{};
    if (!map.getNeighbor(coordinates, next(direction), nextNeighbour) || !isWater(map, nextNeighbour)) return;

    // The third cell of the junction is water too, so its own wedge stops at the
    // water corner and this triangle fills the gap between the two bridges.
    HexVec3 corner = map.cellGroundPosition(nextNeighbour) + HexMetrics::firstWaterCorner(previous(direction));
    corner.y       = farY;
    emitTriangle(out, c2, e2, SurfaceVertex{corner, 0.f, 0.f}, noise);
}

/**
 * @brief Emits the shoreline wedge and the bank strip of one direction.
 *
 * This is the reference project's `TriangulateWaterShore`. Two details matter for the
 * result:
 *
 * - the wedge is **subdivided into four triangles** along a five-sample water edge, so
 *   the water edge tessellates like the bank it meets; and
 * - the strip is **flat at the water height** - the far edge only carries the
 *   neighbour's solid corner *positions*, never the neighbour's elevation. A strip
 *   that tilts up to the bank instead reads as a bright band tracing every hexagon
 *   edge, which is exactly the seam this port exists to avoid. The bank terrain
 *   simply rises through the flat water, and the far edge's foam parameter marks where.
 *
 * @param map Source map.
 * @param coordinates Flooded cell the strip belongs to.
 * @param direction Direction towards the land neighbour.
 * @param centre Water-surface centre of `coordinates`.
 * @param noise Map noise.
 * @param out Destination mesh.
 */
void appendWaterShore(const HexMap& map, HexCoordinates coordinates, HexDirection direction, HexVec3 centre,
                      const HexNoise& noise, HexMeshData& out) {
    const EdgeVertices near{centre + HexMetrics::firstWaterCorner(direction),
                            centre + HexMetrics::secondWaterCorner(direction)};

    const SurfaceVertex nearSamples[5] = {SurfaceVertex{near.v1, 0.f, 0.f}, SurfaceVertex{near.v2, 0.f, 0.f},
                                          SurfaceVertex{near.v3, 0.f, 0.f}, SurfaceVertex{near.v4, 0.f, 0.f},
                                          SurfaceVertex{near.v5, 0.f, 0.f}};
    const SurfaceVertex wedgeCentre{centre, 0.f, 0.f};
    for (std::int32_t i = 0; i < 4; ++i) {
        emitTriangle(out, wedgeCentre, nearSamples[i], nearSamples[i + 1], noise);
    }

    const HexCoordinates neighbour = coordinates.step(direction);
    const HexVec3        neighbourGround = map.cellGroundPosition(neighbour);
    const HexVec3        bank{neighbourGround.x, centre.y, neighbourGround.z};
    // Second corner first, so sample `i` of the far edge lines up with sample `i` of
    // the near edge across the strip - the reference orders them the same way.
    const EdgeVertices far{bank + HexMetrics::secondSolidCorner(opposite(direction)),
                           bank + HexMetrics::firstSolidCorner(opposite(direction))};

    const SurfaceVertex farSamples[5] = {SurfaceVertex{far.v1, 1.f, 0.f}, SurfaceVertex{far.v2, 1.f, 0.f},
                                         SurfaceVertex{far.v3, 1.f, 0.f}, SurfaceVertex{far.v4, 1.f, 0.f},
                                         SurfaceVertex{far.v5, 1.f, 0.f}};
    for (std::int32_t i = 0; i < 4; ++i) {
        emitQuad(out, nearSamples[i], nearSamples[i + 1], farSamples[i + 1], farSamples[i], noise);
    }

    // Close the junction with the next cell around: its water edge when it is flooded,
    // its bank corner when it is land. Either way the vertex stays at the water height.
    HexCoordinates nextNeighbour{};
    if (!map.getNeighbor(coordinates, next(direction), nextNeighbour)) return;
    const HexVec3 corner =
        isWater(map, nextNeighbour)
            ? map.cellGroundPosition(nextNeighbour) + HexMetrics::firstWaterCorner(previous(direction))
            : map.cellGroundPosition(nextNeighbour) + HexMetrics::firstSolidCorner(previous(direction));
    HexVec3 capped = corner;
    capped.y       = centre.y;
    emitTriangle(out, nearSamples[4], farSamples[4], SurfaceVertex{capped, 0.f, 0.f}, noise);
}

}  // namespace

void buildWaterMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out) {
    out.clear();
    if (chunkIndex < 0 || chunkIndex >= map.chunkCount()) {
        out.finalize();
        return;
    }

    const HexNoise& noise = map.noise();

    for (std::int32_t row = 0; row < HexMetrics::kChunkSizeZ; ++row) {
        for (std::int32_t column = 0; column < HexMetrics::kChunkSizeX; ++column) {
            const HexCoordinates coordinates = map.chunkCell(chunkIndex, column, row);
            if (map.cell(coordinates) == nullptr || !map.isUnderwater(coordinates)) continue;

            const HexVec3 ground = map.cellGroundPosition(coordinates);
            const HexVec3 centre{ground.x, HexMetrics::waterSurfaceY(map.waterLevel(coordinates)), ground.z};

            // Dispatch per direction, exactly like the reference: a flooded direction
            // gets the open-water wedge, a land direction gets the shore wedge and its
            // strip. Emitting a full-cell fan up front instead would double the centre
            // triangles of every shoreline direction and leave the strip to fight it.
            for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
                const auto direction = static_cast<HexDirection>(i);
                HexCoordinates neighbour{};
                const bool     hasNeighbour   = map.getNeighbor(coordinates, direction, neighbour);
                const bool     neighbourWater = hasNeighbour && isWater(map, neighbour);
                if (hasNeighbour && !neighbourWater) {
                    appendWaterShore(map, coordinates, direction, centre, noise, out);
                } else {
                    appendOpenWater(map, coordinates, direction, centre, noise, out);
                }
            }
        }
    }

    out.finalize();
}

}  // namespace eve::hexmap
