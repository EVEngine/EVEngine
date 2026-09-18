#include "hexmap/HexMapMesh.h"

#include "hexmap/HexCell.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"

#include <cmath>
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
 * `a -> b -> c` must resolve to a `+Y` normal (counter-clockwise seen from above,
 * using up = `+Z`), otherwise the face is culled.
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
 */
void emitQuad(HexMeshData& out, const SurfaceVertex& r0, const SurfaceVertex& r1, const SurfaceVertex& r2,
              const SurfaceVertex& r3, const HexNoise& noise) {
    emitTriangle(out, r0, r3, r1, noise);
    emitTriangle(out, r1, r3, r2, noise);
}

/** @brief Unit horizontal normal of `forward`, turned a quarter circle around `+Y`. */
[[nodiscard]] HexVec3 acrossDirection(HexVec3 forward) noexcept {
    const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (length <= 1e-6f) return HexVec3{0.f, 0.f, 0.f};
    return HexVec3{forward.z / length, 0.f, -forward.x / length};
}

/** @brief One cross-section of a strip: a centreline point, its across direction and its width. */
struct StripStation {
    HexVec3 position;
    HexVec3 across;
    float   halfWidth = 0.f;
    float   u         = 0.f;
};

/**
 * @brief Appends the ribbon segment between two cross-sections.
 *
 * The outline runs `minus0 -> plus0 -> plus1 -> minus1`, the ring order these helpers
 * resolve to an upward normal. Both across directions must belong to the same
 * direction of travel; a zero half-width folds one pair of corners together and
 * leaves only the tapering triangle.
 *
 * @param out Destination mesh.
 * @param from Upstream cross-section.
 * @param to Downstream cross-section.
 * @param noise Map noise, applied once per emitted position.
 */
void emitSegment(HexMeshData& out, const StripStation& from, const StripStation& to, const HexNoise& noise) {
    const SurfaceVertex minus0{from.position - from.across * from.halfWidth, from.u, 0.f};
    const SurfaceVertex plus0{from.position + from.across * from.halfWidth, from.u, 1.f};
    const SurfaceVertex plus1{to.position + to.across * to.halfWidth, to.u, 1.f};
    const SurfaceVertex minus1{to.position - to.across * to.halfWidth, to.u, 0.f};
    emitQuad(out, minus0, plus0, plus1, minus1, noise);
}

}  // namespace

void buildRoadMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out) {
    out.clear();
    if (chunkIndex < 0 || chunkIndex >= map.chunkCount()) {
        out.finalize();
        return;
    }

    const HexNoise& noise     = map.noise();
    const float     halfWidth = HexMetrics::innerRadius() * HexMetrics::kSolidFactor * 0.375f;
    // Lift the surface off the terrain so the two coplanar layers cannot z-fight.
    constexpr float kRoadLift = 0.05f;

    for (std::int32_t row = 0; row < HexMetrics::kChunkSizeZ; ++row) {
        for (std::int32_t column = 0; column < HexMetrics::kChunkSizeX; ++column) {
            const HexCoordinates coordinates = map.chunkCell(chunkIndex, column, row);
            const HexCellData*   data        = map.cell(coordinates);
            if (data == nullptr || !map.hasRoad(coordinates)) continue;

            const HexVec3 start  = map.cellPosition(coordinates);
            const HexVec3 ground = map.cellGroundPosition(coordinates);

            // Emit each edge once, from the chunk that owns that border.
            for (std::int32_t i = 0; i <= static_cast<std::int32_t>(HexDirection::SE); ++i) {
                const HexDirection direction = static_cast<HexDirection>(i);
                if (!data->flags.hasRoad(direction)) continue;
                HexCoordinates neighbour{};
                if (!map.getNeighbor(coordinates, direction, neighbour)) continue;

                const HexVec3 neighbourCentre = map.cellPosition(neighbour);
                const HexVec3 edgeMiddle      = HexMetrics::solidEdgeMiddle(direction);
                // The crossing sits halfway between the two surfaces so the ribbon bends with
                // the slope instead of stepping at the border.
                const HexVec3 crossing{ground.x + edgeMiddle.x, (start.y + neighbourCentre.y) * 0.5f,
                                       ground.z + edgeMiddle.z};

                const StripStation from{
                    {start.x, start.y + kRoadLift, start.z}, acrossDirection(crossing - start), halfWidth, 0.f};
                const StripStation middle{{crossing.x, crossing.y + kRoadLift, crossing.z},
                                          acrossDirection(neighbourCentre - start),
                                          halfWidth,
                                          0.5f};
                const StripStation finish{{neighbourCentre.x, neighbourCentre.y + kRoadLift, neighbourCentre.z},
                                          acrossDirection(neighbourCentre - crossing),
                                          halfWidth,
                                          1.f};
                emitSegment(out, from, middle, noise);
                emitSegment(out, middle, finish, noise);
            }
        }
    }

    out.finalize();
}

}  // namespace eve::hexmap
