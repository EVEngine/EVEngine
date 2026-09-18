#include "hexmap/HexMapMesh.h"

#include "hexmap/HexCell.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"

#include <array>
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

void buildRiverMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out) {
    out.clear();
    if (chunkIndex < 0 || chunkIndex >= map.chunkCount()) {
        out.finalize();
        return;
    }

    const HexNoise& noise     = map.noise();
    const float     halfWidth = HexMetrics::innerRadius() * HexMetrics::kRiverSurfaceScale * 0.5f;

    for (std::int32_t row = 0; row < HexMetrics::kChunkSizeZ; ++row) {
        for (std::int32_t column = 0; column < HexMetrics::kChunkSizeX; ++column) {
            const HexCoordinates coordinates = map.chunkCell(chunkIndex, column, row);
            const HexCellData*   data        = map.cell(coordinates);
            if (data == nullptr || !map.hasRiver(coordinates)) continue;

            const HexVec3 ground = map.cellGroundPosition(coordinates);
            const HexVec3 centre{ground.x, HexMetrics::streamBedY(map.elevation(coordinates)), ground.z};

            // Per-edge nodes of the channel, keyed by HexDirection index.
            std::array<bool, 6>    connected{};
            std::array<bool, 6>    incoming{};
            std::array<bool, 6>    outgoing{};
            std::array<HexVec3, 6> edgePosition{};
            std::int32_t           connectionCount = 0;
            for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
                const HexDirection direction = static_cast<HexDirection>(i);
                if (!map.hasRiverThrough(coordinates, direction)) continue;
                connected[i] = true;
                incoming[i]  = data->flags.hasRiverIn(direction);
                outgoing[i]  = data->flags.hasRiverOut(direction);
                ++connectionCount;
                // The edge crossing sits on the solid edge midpoint, the same point the
                // terrain mesh owns on a shared border.
                edgePosition[i] = centre + HexMetrics::solidEdgeMiddle(direction);
            }
            if (connectionCount == 0) continue;

            std::int32_t emitted = 0;

            // Every incoming/outgoing pair bends through the cell centre.
            for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
                if (!incoming[i]) continue;
                for (std::int32_t j = 0; j < kHexDirectionCount; ++j) {
                    if (!outgoing[j] || j == i) continue;
                    const StripStation start{edgePosition[i], acrossDirection(centre - edgePosition[i]), halfWidth,
                                             0.f};
                    const StripStation middle{centre, acrossDirection(edgePosition[j] - edgePosition[i]), halfWidth,
                                              0.5f};
                    const StripStation finish{edgePosition[j], acrossDirection(edgePosition[j] - centre), halfWidth,
                                              1.f};
                    emitSegment(out, start, middle, noise);
                    emitSegment(out, middle, finish, noise);
                    ++emitted;
                }
            }

            // A begin/end cell (HexFlags::hasRiverBeginOrEnd) has exactly one connection, so
            // there is no pair: the channel tapers from the edge to the cell centre instead.
            if (emitted == 0 && connectionCount == 1) {
                for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
                    if (!connected[i]) continue;
                    if (outgoing[i]) {
                        const StripStation start{centre, acrossDirection(edgePosition[i] - centre), 0.f, 0.5f};
                        const StripStation finish{edgePosition[i], acrossDirection(edgePosition[i] - centre), halfWidth,
                                                  1.f};
                        emitSegment(out, start, finish, noise);
                    } else {
                        const StripStation start{edgePosition[i], acrossDirection(centre - edgePosition[i]), halfWidth,
                                                 0.f};
                        const StripStation finish{centre, acrossDirection(centre - edgePosition[i]), 0.f, 0.5f};
                        emitSegment(out, start, finish, noise);
                    }
                    ++emitted;
                }
            }

            // Degenerate topologies (for example a sink fed by two rivers) carry no direction
            // pair at all; join the collected edges in direction order so the cell is not blank.
            if (emitted == 0) {
                std::int32_t previous = -1;
                for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
                    if (!connected[i]) continue;
                    if (previous >= 0) {
                        const StripStation start{edgePosition[previous],
                                                 acrossDirection(centre - edgePosition[previous]), halfWidth, 0.f};
                        const StripStation middle{centre, acrossDirection(edgePosition[i] - edgePosition[previous]),
                                                  halfWidth, 0.5f};
                        const StripStation finish{edgePosition[i], acrossDirection(edgePosition[i] - centre), halfWidth,
                                                  1.f};
                        emitSegment(out, start, middle, noise);
                        emitSegment(out, middle, finish, noise);
                        ++emitted;
                    }
                    previous = i;
                }
            }
        }
    }

    out.finalize();
}

}  // namespace eve::hexmap
