#include "hexmap/HexSphereMesh.h"

#include "hexmap/HexMapMesh.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace eve::hexmap {

namespace {

/**
 * @brief Perturbation contract of this translation unit.
 *
 * `SphereMesher::vertex` is the single place a position is displaced, so no position
 * is displaced twice. Unlike the planar mesher, whose vertical noise term is folded
 * into `HexMap::cellPosition`, every position here is built from a topological
 * direction and a radius only, and the perturbation is purely tangential. It is a
 * pure function of the unperturbed position, which is what keeps the shared corners
 * of a cell's two adjacent edges - and of the three cells meeting at a corner - in
 * exact agreement, so the surface stays closed.
 */

[[nodiscard]] float dotOf(HexVec3 a, HexVec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

[[nodiscard]] float lengthOf(HexVec3 v) noexcept { return std::sqrt(dotOf(v, v)); }

[[nodiscard]] HexVec3 crossOf(HexVec3 a, HexVec3 b) noexcept {
    return HexVec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] HexVec3 normalize(HexVec3 v) noexcept {
    const float len = lengthOf(v);
    if (!(len > 1e-9f)) return HexVec3{0.f, 1.f, 0.f};
    const float inverse = 1.f / len;
    return HexVec3{v.x * inverse, v.y * inverse, v.z * inverse};
}

/** @brief Interpolates two **unit** directions along their great circle. */
[[nodiscard]] HexVec3 slerpDirection(HexVec3 a, HexVec3 b, float t) noexcept {
    const float cosine = std::clamp(dotOf(a, b), -1.f, 1.f);
    const float angle  = std::acos(cosine);
    if (angle < 1e-5f) {
        return normalize(HexVec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
    }
    const float sine = std::sin(angle);
    const float wa   = std::sin((1.f - t) * angle) / sine;
    const float wb   = std::sin(t * angle) / sine;
    return HexVec3{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb};
}

/**
 * @brief Five samples along the arc from `a` to `b` with the radius interpolated linearly.
 *
 * The spherical replacement for `EdgeVertices(c1, c2)`, which interpolates in a
 * straight line in XZ. On a sphere the surface path between two points of an edge is
 * an arc, so the direction is slerped while the radius stays linear.
 */
[[nodiscard]] EdgeVertices sphereEdge(HexVec3 a, HexVec3 b) noexcept {
    const HexVec3 dirA = normalize(a);
    const HexVec3 dirB = normalize(b);
    const float   ra   = lengthOf(a);
    const float   rb   = lengthOf(b);

    EdgeVertices edge;
    HexVec3*     samples[5] = {&edge.v1, &edge.v2, &edge.v3, &edge.v4, &edge.v5};
    for (std::int32_t i = 0; i < 5; ++i) {
        const float t      = static_cast<float>(i) * 0.25f;
        const float radius = ra + (rb - ra) * t;
        *samples[i]        = slerpDirection(dirA, dirB, t) * radius;
    }
    return edge;
}

/**
 * @brief Terraced interpolation of two positions.
 *
 * The planar `HexMetrics::terraceLerp` moves horizontally in XZ by the terrace step
 * and vertically in Y by its paired step. On a sphere "horizontal" is the surface
 * arc and "vertical" is the radius, so the direction is slerped by the horizontal
 * step and the radius is blended by the vertical one.
 */
[[nodiscard]] HexVec3 sphereTerraceLerp(HexVec3 a, HexVec3 b, std::int32_t step) noexcept {
    const float   horizontal = static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize;
    const float   vertical   = static_cast<float>((step + 1) / 2) * HexMetrics::kVerticalTerraceStepSize;
    const HexVec3 direction  = slerpDirection(normalize(a), normalize(b), horizontal);
    const float   ra         = lengthOf(a);
    const float   rb         = lengthOf(b);
    return direction * (ra + (rb - ra) * vertical);
}

/** @brief `sphereTerraceLerp` across all five samples of an edge. */
[[nodiscard]] EdgeVertices sphereEdgeTerraceLerp(const EdgeVertices& a, const EdgeVertices& b,
                                                 std::int32_t step) noexcept {
    EdgeVertices result;
    result.v1 = sphereTerraceLerp(a.v1, b.v1, step);
    result.v2 = sphereTerraceLerp(a.v2, b.v2, step);
    result.v3 = sphereTerraceLerp(a.v3, b.v3, step);
    result.v4 = sphereTerraceLerp(a.v4, b.v4, step);
    result.v5 = sphereTerraceLerp(a.v5, b.v5, step);
    return result;
}

/**
 * @brief Tangential-only perturbation, `±strength` along two tangent axes.
 *
 * The planar builder displaces XZ and leaves Y alone; on a sphere the corresponding
 * "do not move in or out" rule means displacing inside the tangent plane at the
 * vertex. Both tangent axes are driven by the noise field, so the wobble stays
 * isotropic instead of collapsing at the poles.
 *
 * The displaced point is projected back onto its own radius. A raw tangent offset
 * would change the radius by `O(d^2 / r)`, which is not negligible here: at planet
 * scale the perturbation is a sizeable fraction of a cell, and a radius that drifts
 * would lift and lower the relief independently of the cell's elevation.
 */
[[nodiscard]] HexVec3 tangentPerturb(const HexNoise& noise, HexVec3 position, float strength) noexcept {
    const float radius = lengthOf(position);
    if (!(radius > 1e-6f)) return position;
    const HexVec3 up       = position * (1.f / radius);
    // A tangent frame that stays well conditioned over the whole sphere: the world axis
    // least aligned with `up` is never parallel to it.
    const HexVec3 axis     = std::fabs(up.y) < 0.9f ? HexVec3{0.f, 1.f, 0.f} : HexVec3{1.f, 0.f, 0.f};
    const HexVec3 tangentX = normalize(crossOf(axis, up));
    const HexVec3 tangentY = crossOf(up, tangentX);

    const HexVec4 first  = noise.sample(position.x, position.z);
    const HexVec4 second = noise.sample(position.y + 37.f, position.x - 11.f);
    const float   alongX = (first.x * 2.f - 1.f) * strength;
    const float   alongY = (second.z * 2.f - 1.f) * strength;
    return normalize(position + tangentX * alongX + tangentY * alongY) * radius;
}

/** @brief Terrain palette index of a cell, or `0` when the cell is absent. */
[[nodiscard]] std::int32_t terrainOf(const HexCellData* cell) noexcept {
    return cell == nullptr ? 0 : cell->values.terrainType();
}

/** @brief One of the five samples of an edge. */
[[nodiscard]] HexVec3 sampleAt(const EdgeVertices& edge, std::int32_t index) noexcept {
    switch (index) {
        case 0: return edge.v1;
        case 1: return edge.v2;
        case 2: return edge.v3;
        case 3: return edge.v4;
        default: return edge.v5;
    }
}

/** @brief Elevation of a cell, or `0` when the cell is absent. */
[[nodiscard]] std::int32_t elevationOf(const HexCellData* cell) noexcept {
    return cell == nullptr ? 0 : cell->values.elevation();
}

/**
 * @brief Whether `(a, b, c)` is wound so that its normal points towards the sphere centre.
 *
 * Every surface these meshers build lies on a shell around the origin - the terrain is a closed
 * one, the water is a set of caps on another - so "faces outward" is the winding rule for all of
 * it. The corner branches permute their three points by elevation, and whether that permutation
 * is even or odd depends on which of the three cells emitted the corner, so the same junction
 * came out wound either way; reading the orientation off the emitted triangle is what makes
 * every branch agree. The water mesher is a separate emitter and needs the same treatment: its
 * caps are wound from a centre and corner ring, which is inward for this engine's front face.
 *
 * The vertex normals are flat geometric normals (see `HexMeshData::finalize`), which is why
 * `hexmap.sphereMesh.facesPointAwayFromTheCentre` and its water counterpart are the checks that
 * measure this.
 */
[[nodiscard]] bool facesInward(HexVec3 a, HexVec3 b, HexVec3 c) noexcept {
    const HexVec3 ab = b - a;
    const HexVec3 ac = c - a;
    const HexVec3 normal{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
    // The centroid's direction from the origin is the outward direction; leaving both vectors
    // unnormalised is fine because only the sign matters.
    return normal.x * (a.x + b.x + c.x) + normal.y * (a.y + b.y + c.y) + normal.z * (a.z + b.z + c.z) < 0.f;
}

/** @brief The three cells meeting at a corner, kept together while they are sorted by elevation. */
struct CornerCells {
    HexVec3            up{};
    HexVec3            left{};
    HexVec3            right{};
    const HexCellData* upCell    = nullptr;
    const HexCellData* leftCell  = nullptr;
    const HexCellData* rightCell = nullptr;
};

/** @brief Emits the ground fans, blend strips, terraces and cliffs of one sphere. */
class SphereMesher {
public:
    SphereMesher(const HexSphereMap& map, HexMeshData& out) noexcept
        : map_(map), out_(out),
          // The planar mesher perturbs by a fixed 40% of a cell's outer radius. The
          // same fraction of the mean cell spacing keeps the wobble scale-invariant
          // across subdivision levels and radii, which a fixed world-unit figure
          // would not be.
          perturbStrength_((HexMetrics::kCellPerturbStrength / HexMetrics::kOuterRadius) * map.cellSpacing()),
          // River beds drop by the planar stream-bed offset expressed in elevation
          // steps, so the channel keeps the same depth relative to the relief.
          streamBedDrop_((HexMetrics::kStreamBedElevationOffset / HexMetrics::kElevationStep) * map.elevationStep()) {}

    /** @brief Builds every cell of the sphere. */
    void run() {
        for (HexSphereCell cell = 0; cell < map_.cellCount(); ++cell) buildCell(cell);
    }

private:
    // --- edge frames --------------------------------------------------------

    /** @brief Solid corner `corner` of `cell`: its topological corner pulled towards the centre. */
    [[nodiscard]] HexVec3 solidCornerOf(HexSphereCell cell, std::int32_t corner) const noexcept {
        const HexVec3 mixed =
            slerpDirection(map_.direction(cell), map_.cornerDirection(cell, corner), HexMetrics::kSolidFactor);
        return mixed * map_.surfaceRadius(cell);
    }

    /** @brief Solid border of edge `d` of `cell`, from its corner `d` to its corner `d + 1`. */
    [[nodiscard]] EdgeVertices solidEdgeOf(HexSphereCell cell, std::int32_t d) const noexcept {
        const std::int32_t edges = map_.neighborCount(cell);
        const std::int32_t nextD = (d + 1) % edges;
        return sphereEdge(solidCornerOf(cell, d), solidCornerOf(cell, nextD));
    }

    /**
     * @brief Solid border of the same shared edge seen from `neighbour`, wound to match.
     *
     * The neighbour traverses the shared edge in the opposite corner order, so its v1
     * would sit at this cell's corner `d + 1`; the two ends are swapped back so that
     * `near.v1` pairs with `far.v1` across the strip and the quad does not twist.
     */
    [[nodiscard]] EdgeVertices farEdgeOf(HexSphereCell cell, std::int32_t d, HexSphereCell neighbour) const noexcept {
        const std::int32_t back   = map_.oppositeDirection(cell, d);
        const std::int32_t nEdges = map_.neighborCount(neighbour);
        if (back < 0 || nEdges <= 0) return EdgeVertices{};
        return sphereEdge(solidCornerOf(neighbour, (back + 1) % nEdges), solidCornerOf(neighbour, back));
    }

    /** @brief Whether a river crosses the edge shared with `d`'s neighbour. */
    [[nodiscard]] bool riverThrough(HexSphereCell cell, std::int32_t d, const HexCellData* cellData) const noexcept {
        if (cellData != nullptr && cellData->flags.hasRiverThrough(static_cast<HexDirection>(d))) return true;
        const HexSphereCell neighbour = map_.neighbor(cell, d);
        if (neighbour == kNoHexSphereCell) return false;
        const HexCellData* other = map_.cellAt(neighbour);
        const std::int32_t back  = map_.oppositeDirection(cell, d);
        return other != nullptr && back >= 0 && other->flags.hasRiverThrough(static_cast<HexDirection>(back));
    }

    // --- per-cell entry point -----------------------------------------------

    /** @brief Emits the fans of one cell plus the connection strips it owns. */
    void buildCell(HexSphereCell cell) {
        const HexCellData* cellData = map_.cellAt(cell);
        if (cellData == nullptr) return;

        const HexVec3      center = map_.cellPosition(cell);
        const std::int32_t edges  = map_.neighborCount(cell);
        for (std::int32_t d = 0; d < edges; ++d) {
            EdgeVertices near = solidEdgeOf(cell, d);
            if (riverThrough(cell, d, cellData)) {
                // Carve a shallow channel along the river centreline (the middle sample only).
                near.v3 = normalize(near.v3) * (lengthOf(near.v3) + streamBedDrop_);
            }
            appendEdgeFan(center, near, cellData);

            const HexSphereCell neighbour = map_.neighbor(cell, d);
            if (neighbour == kNoHexSphereCell) continue;
            const HexCellData* neighbourData = map_.cellAt(neighbour);
            if (neighbourData == nullptr) continue;
            appendConnection(cell, d, near, cellData, neighbour, neighbourData);
        }
    }

    /** @brief Emits the strip/terrace/cliff towards one neighbour and the corner it closes. */
    void appendConnection(HexSphereCell cell, std::int32_t d, const EdgeVertices& near, const HexCellData* cellData,
                          HexSphereCell neighbour, const HexCellData* neighbourData) {
        // Edge ownership rule. The planar mesher leans on the chunk grid here; a sphere
        // has none, so the lower cell id owns the edge instead. That is a total order
        // over the same set of shared edges, so every boundary is still emitted exactly
        // once, and never by both of its cells.
        if (cell > neighbour) return;

        const EdgeVertices far = farEdgeOf(cell, d, neighbour);

        const HexTerrainWeights nearWeights = HexTerrainWeights::primary();
        const HexTerrainWeights farWeights  = HexTerrainWeights::blend(1.f);

        switch (map_.edgeTypeTo(cell, neighbour)) {
            case HexEdgeType::Flat:
                appendBlendStrip(near, far, nearWeights, farWeights, cellData, neighbourData);
                break;
            case HexEdgeType::Slope:
                appendEdgeTerraces(near, nearWeights, far, farWeights, cellData, neighbourData);
                break;
            case HexEdgeType::Cliff:
                // The blend strip would already be a radial wall here, but its winding
                // flips with the sign of the elevation step; the vertical winding is
                // correct in both directions. Split into the same four sub-quads as a
                // flat strip so the wall does not leave a T-junction against the fan.
                for (std::int32_t i = 0; i < 4; ++i) {
                    emitQuadVertical(sampleAt(near, i), sampleAt(near, i + 1), sampleAt(far, i), sampleAt(far, i + 1),
                                     nearWeights, nearWeights, farWeights, farWeights, cellData, neighbourData,
                                     cellData);
                }
                break;
        }

        appendCorner(cell, d, cellData, neighbour, neighbourData);
    }

    // --- corner -------------------------------------------------------------

    /**
     * @brief Emits the triangle closing the gap between three cells sharing a corner.
     *
     * The three cells meet at one topological corner, so each of their solid corners
     * lies on the same unit direction and differs only by the cell's own surface
     * radius. That is what makes the patch exact: the three solid corners are pulled
     * towards three different cell centres, and the shared corner direction is the one
     * thing they have in common.
     */
    void appendCorner(HexSphereCell cell, std::int32_t d, const HexCellData* cellData, HexSphereCell neighbour,
                      const HexCellData* neighbourData) {
        const std::int32_t  edges    = map_.neighborCount(cell);
        const std::int32_t  nextD    = (d + 1) % edges;
        const HexSphereCell nextCell = map_.neighbor(cell, nextD);
        if (nextCell == kNoHexSphereCell) return;
        const HexCellData* nextData = map_.cellAt(nextCell);
        if (nextData == nullptr) return;

        // Corner ownership. `appendConnection` already returned unless `cell < neighbour`,
        // which is the right total order for an *edge* - two cells share it. A corner is
        // shared by three, so reusing that gate emits the patch for one or two of them and
        // half of all corners end up with two coincident patches (non-manifold edges and a
        // broken Euler characteristic). The lowest id of the three cells owns the corner,
        // which is exactly one emitter and is always a cell that owns its preceding edge.
        if (nextCell < cell) return;

        // The corner between edge `d` and edge `d + 1` of `cell` is corner `d + 1`. Each
        // neighbour reaches that same corner from the far end of its own shared edge,
        // which the topology stores in the opposite order.
        const std::int32_t back       = map_.oppositeDirection(cell, d);
        const std::int32_t back2      = map_.oppositeDirection(cell, nextD);
        const std::int32_t nextEdges  = map_.neighborCount(nextCell);
        if (back < 0 || back2 < 0 || nextEdges <= 0) return;

        CornerCells corner{};
        corner.up        = solidCornerOf(cell, nextD);
        corner.right     = solidCornerOf(neighbour, back);
        corner.left      = solidCornerOf(nextCell, (back2 + 1) % nextEdges);
        corner.upCell    = cellData;
        corner.rightCell = neighbourData;
        corner.leftCell  = nextData;
        appendCornerTriangles(corner);
    }

    /** @brief Sorts the three corner cells by elevation and dispatches on the two edge types. */
    void appendCornerTriangles(const CornerCells& corner) {
        const std::array<HexVec3, 3>            positions{corner.up, corner.left, corner.right};
        const std::array<const HexCellData*, 3> cells{corner.upCell, corner.leftCell, corner.rightCell};
        std::array<std::int32_t, 3>             order{0, 1, 2};
        for (std::int32_t i = 0; i < 2; ++i) {
            for (std::int32_t j = i + 1; j < 3; ++j) {
                if (elevationOf(cells[static_cast<std::size_t>(order[static_cast<std::size_t>(j)])]) <
                    elevationOf(cells[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])])) {
                    std::swap(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(j)]);
                }
            }
        }

        const HexVec3      bottom     = positions[static_cast<std::size_t>(order[0])];
        const HexVec3      low        = positions[static_cast<std::size_t>(order[1])];
        const HexVec3      high       = positions[static_cast<std::size_t>(order[2])];
        const HexCellData* bottomCell = cells[static_cast<std::size_t>(order[0])];
        const HexCellData* lowCell    = cells[static_cast<std::size_t>(order[1])];
        const HexCellData* highCell   = cells[static_cast<std::size_t>(order[2])];

        const HexEdgeType lowEdge  = edgeType(elevationOf(bottomCell), elevationOf(lowCell));
        const HexEdgeType highEdge = edgeType(elevationOf(bottomCell), elevationOf(highCell));

        if (lowEdge == HexEdgeType::Slope && highEdge == HexEdgeType::Slope) {
            cornerTerraces(bottom, low, high, bottomCell, lowCell, highCell);
        } else if (lowEdge == HexEdgeType::Slope && highEdge == HexEdgeType::Flat) {
            cornerTerraces(low, high, bottom, lowCell, highCell, bottomCell);
        } else if (lowEdge == HexEdgeType::Flat && highEdge == HexEdgeType::Slope) {
            cornerTerraces(high, bottom, low, highCell, bottomCell, lowCell);
        } else if (lowEdge == HexEdgeType::Slope && highEdge == HexEdgeType::Cliff) {
            cornerTerracesCliff(bottom, low, high, bottomCell, lowCell, highCell, lowEdge);
        } else if (lowEdge == HexEdgeType::Cliff && highEdge == HexEdgeType::Slope) {
            cornerCliffTerraces(bottom, low, high, bottomCell, lowCell, highCell, highEdge);
        } else if (edgeType(elevationOf(lowCell), elevationOf(highCell)) == HexEdgeType::Slope) {
            if (elevationOf(lowCell) < elevationOf(highCell)) {
                cornerCliffTerraces(high, bottom, low, highCell, bottomCell, lowCell, highEdge);
            } else {
                cornerTerracesCliff(low, high, bottom, lowCell, highCell, bottomCell, lowEdge);
            }
        } else {
            // Mirror of the reference winding: this engine's front face is the opposite
            // handedness, so the flat corner fan is emitted high-before-low.
            emitTriangle(bottom, high, low, HexTerrainWeights::primary(), bottomCell, lowCell, highCell);
        }
    }

    /** @brief Five-step terraced fan across a corner whose both sides of `bottom` are slopes. */
    void cornerTerraces(const HexVec3& bottom, const HexVec3& left, const HexVec3& right, const HexCellData* bottomCell,
                        const HexCellData* leftCell, const HexCellData* rightCell) {
        const HexTerrainWeights bottomWeights = HexTerrainWeights::primary();
        const HexTerrainWeights leftWeights   = HexTerrainWeights::primary();
        const HexTerrainWeights rightWeights  = HexTerrainWeights::primary();

        HexVec3           lastLeft         = bottom;
        HexVec3           lastRight        = bottom;
        HexTerrainWeights lastLeftWeights  = bottomWeights;
        HexTerrainWeights lastRightWeights = bottomWeights;
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const float             t             = static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize;
            const HexTerrainWeights wl            = HexTerrainWeights::lerp(bottomWeights, leftWeights, t);
            const HexTerrainWeights wr            = HexTerrainWeights::lerp(bottomWeights, rightWeights, t);
            const HexVec3           boundaryLeft  = sphereTerraceLerp(bottom, left, step);
            const HexVec3           boundaryRight = sphereTerraceLerp(bottom, right, step);

            emitQuadForward(boundaryLeft, boundaryRight, lastLeft, lastRight, wl, wr, lastLeftWeights, lastRightWeights,
                            bottomCell, leftCell, rightCell);
            lastLeft         = boundaryLeft;
            lastRight        = boundaryRight;
            lastLeftWeights  = wl;
            lastRightWeights = wr;
        }
        emitQuadForward(lastLeft, lastRight, left, right, lastLeftWeights, lastRightWeights, leftWeights, rightWeights,
                        bottomCell, leftCell, rightCell);
    }

    /**
     * @brief Fills the sliver whose `left`/`right` sides are constant-weight edges.
     *
     * One triangle per terrace step walks from `bottom` to `right`; the corner's third cell vanishes at
     * `bottom`, so the band ends flush with the cliff foot.
     */
    void appendBoundaryTriangle(HexVec3 bottom, const HexTerrainWeights& bottomWeights, HexVec3 left,
                                const HexTerrainWeights& leftWeights, HexVec3 right,
                                const HexTerrainWeights& rightWeights, const HexCellData* bottomCell,
                                const HexCellData* leftCell, const HexCellData* rightCell) {
        HexVec3 leftSide  = sphereTerraceLerp(bottom, left, 1);
        HexVec3 rightSide = sphereTerraceLerp(bottom, right, 1);
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const float             t  = static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize;
            const HexTerrainWeights wl = HexTerrainWeights::lerp(bottomWeights, leftWeights, t);
            const HexTerrainWeights wr = HexTerrainWeights::lerp(bottomWeights, rightWeights, t);

            emitTriangle(leftSide, rightSide, bottom, wl, wr, bottomWeights, bottomCell, leftCell, rightCell);

            leftSide  = rightSide;
            rightSide = sphereTerraceLerp(bottom, right, step + 1);
        }
        emitTriangle(leftSide, rightSide, bottom, leftWeights, rightWeights, bottomWeights, bottomCell, leftCell,
                     rightCell);
    }

    /** @brief Terraced corner where the `high` side rises to a cliff foot. */
    void cornerTerracesCliff(const HexVec3& bottom, const HexVec3& low, const HexVec3& high,
                             const HexCellData* bottomCell, const HexCellData* lowCell, const HexCellData* highCell,
                             HexEdgeType lowEdge) {
        const HexTerrainWeights bottomWeights = HexTerrainWeights::primary();
        const HexTerrainWeights lowWeights    = HexTerrainWeights::primary();
        const HexTerrainWeights highWeights   = HexTerrainWeights::primary();

        const float             span            = static_cast<float>(elevationOf(highCell) - elevationOf(bottomCell));
        const float             blend           = span > 1e-6f ? 1.f / span : 1.f;
        const HexVec3           boundary        = lerp(bottom, high, blend);
        const HexTerrainWeights boundaryWeights = HexTerrainWeights::lerp(bottomWeights, highWeights, blend);

        const std::uint32_t anchor = static_cast<std::uint32_t>(out_.vertexCount());
        vertex(boundary, boundaryWeights, bottomCell, lowCell, highCell);
        appendBoundaryTriangle(bottom, bottomWeights, low, lowWeights, boundary, boundaryWeights, bottomCell, lowCell,
                               highCell);
        emitTriangleFrom(anchor, low, bottom, lowWeights, bottomWeights, bottomCell, lowCell, highCell);
        if (lowEdge == HexEdgeType::Slope) {
            appendBoundaryTriangle(boundary, boundaryWeights, bottom, bottomWeights, high, highWeights, bottomCell,
                                   lowCell, highCell);
        } else {
            emitTriangle(high, bottom, low, highWeights, bottomWeights, lowWeights, bottomCell, lowCell, highCell);
        }
    }

    /** @brief Terraced corner where the `low` side rises to a cliff foot (mirror of the above). */
    void cornerCliffTerraces(const HexVec3& bottom, const HexVec3& low, const HexVec3& high,
                             const HexCellData* bottomCell, const HexCellData* lowCell, const HexCellData* highCell,
                             HexEdgeType highEdge) {
        const HexTerrainWeights bottomWeights = HexTerrainWeights::primary();
        const HexTerrainWeights lowWeights    = HexTerrainWeights::primary();
        const HexTerrainWeights highWeights   = HexTerrainWeights::primary();

        const float             span            = static_cast<float>(elevationOf(lowCell) - elevationOf(bottomCell));
        const float             blend           = span > 1e-6f ? 1.f / span : 1.f;
        const HexVec3           boundary        = lerp(bottom, low, blend);
        const HexTerrainWeights boundaryWeights = HexTerrainWeights::lerp(bottomWeights, lowWeights, blend);

        const std::uint32_t anchor = static_cast<std::uint32_t>(out_.vertexCount());
        vertex(boundary, boundaryWeights, bottomCell, lowCell, highCell);
        appendBoundaryTriangle(bottom, bottomWeights, high, highWeights, boundary, boundaryWeights, bottomCell,
                               highCell, lowCell);
        emitTriangleFrom(anchor, high, bottom, highWeights, bottomWeights, bottomCell, highCell, lowCell);
        if (highEdge == HexEdgeType::Slope) {
            appendBoundaryTriangle(boundary, boundaryWeights, bottom, bottomWeights, low, lowWeights, bottomCell,
                                   highCell, lowCell);
        } else {
            emitTriangle(low, bottom, high, lowWeights, bottomWeights, highWeights, bottomCell, highCell, lowCell);
        }
    }

    // --- vertex and triangle primitives -------------------------------------

    /** @brief Emits one vertex; the single perturbation point of this file. */
    void vertex(HexVec3 position, const HexTerrainWeights& weights, const HexCellData* t0, const HexCellData* t1,
                const HexCellData* t2) {
        const float u = HexTerrainVertexCode::encodeIndices(terrainOf(t0), terrainOf(t1), terrainOf(t2));
        const float v = HexTerrainVertexCode::encodeWeights(weights.b, weights.c);
        out_.addVertex(tangentPerturb(map_.noise(), position, perturbStrength_), u, v);
    }

    /** @brief The position `vertex` will actually emit for `position`. */
    [[nodiscard]] HexVec3 perturbedPosition(HexVec3 position) const noexcept {
        return tangentPerturb(map_.noise(), position, perturbStrength_);
    }

    /** @brief Position of an already emitted vertex, for the anchored-triangle orientation. */
    [[nodiscard]] HexVec3 positionOf(std::uint32_t vertexIndex) const noexcept {
        const std::size_t base = static_cast<std::size_t>(vertexIndex) * 3u;
        return HexVec3{out_.positions()[base], out_.positions()[base + 1u], out_.positions()[base + 2u]};
    }

    /** @brief Emits a triangle whose three vertices share one weight set and one cell triple. */
    void emitTriangle(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w,
                      const HexCellData* t0, const HexCellData* t1, const HexCellData* t2) {
        const std::uint32_t i0 = static_cast<std::uint32_t>(out_.vertexCount());
        // The winding is decided on the *emitted* positions: the perturbation is tangential and
        // can flip the sign of a sliver, so testing the nominal points would leave those wound
        // the wrong way however carefully the emitter ordered them.
        const bool inward = facesInward(perturbedPosition(p0), perturbedPosition(p1), perturbedPosition(p2));
        vertex(p0, w, t0, t1, t2);
        vertex(p1, w, t0, t1, t2);
        vertex(p2, w, t0, t1, t2);
        if (inward)
            out_.addTriangle(i0, i0 + 2u, i0 + 1u);
        else
            out_.addTriangle(i0, i0 + 1u, i0 + 2u);
    }

    /** @brief Emits a triangle with one weight set per vertex. */
    void emitTriangle(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w0,
                      const HexTerrainWeights& w1, const HexTerrainWeights& w2, const HexCellData* t0,
                      const HexCellData* t1, const HexCellData* t2) {
        const std::uint32_t i0 = static_cast<std::uint32_t>(out_.vertexCount());
        const bool          inward = facesInward(perturbedPosition(p0), perturbedPosition(p1), perturbedPosition(p2));
        vertex(p0, w0, t0, t1, t2);
        vertex(p1, w1, t0, t1, t2);
        vertex(p2, w2, t0, t1, t2);
        if (inward)
            out_.addTriangle(i0, i0 + 2u, i0 + 1u);
        else
            out_.addTriangle(i0, i0 + 1u, i0 + 2u);
    }

    /**
     * @brief Emits a surface quad `(a, b, c, d)` as `(a, c, b)` and `(b, c, d)`.
     *
     * The argument order is the `a -> b -> d -> c` ring used by `HexMeshData::addQuad`. The
     * first emitted triangle decides the winding; mirroring swaps the 2nd and 3rd arguments,
     * which reverses both.
     */
    void emitQuadForward(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexVec3& p3,
                         const HexTerrainWeights& w0, const HexTerrainWeights& w1, const HexTerrainWeights& w2,
                         const HexTerrainWeights& w3, const HexCellData* t0, const HexCellData* t1,
                         const HexCellData* t2) {
        const std::uint32_t i0 = static_cast<std::uint32_t>(out_.vertexCount());
        vertex(p0, w0, t0, t1, t2);
        vertex(p1, w1, t0, t1, t2);
        vertex(p2, w2, t0, t1, t2);
        vertex(p3, w3, t0, t1, t2);
        // Each triangle is oriented on its own. A quad spanning a terrace or a corner is not
        // planar, so its two halves can disagree and orienting the first one alone leaves the
        // other inside-out.
        const HexVec3 a0 = perturbedPosition(p0);
        const HexVec3 a1 = perturbedPosition(p1);
        const HexVec3 a2 = perturbedPosition(p2);
        const HexVec3 a3 = perturbedPosition(p3);
        if (facesInward(a0, a2, a1))
            out_.addTriangle(i0, i0 + 1u, i0 + 2u);
        else
            out_.addTriangle(i0, i0 + 2u, i0 + 1u);
        if (facesInward(a1, a2, a3))
            out_.addTriangle(i0 + 1u, i0 + 3u, i0 + 2u);
        else
            out_.addTriangle(i0 + 1u, i0 + 2u, i0 + 3u);
    }

    /** @brief Emits a radial quad in the bottom/top alternating order used by cliff strips. */
    void emitQuadVertical(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexVec3& p3,
                          const HexTerrainWeights& w0, const HexTerrainWeights& w1, const HexTerrainWeights& w2,
                          const HexTerrainWeights& w3, const HexCellData* t0, const HexCellData* t1,
                          const HexCellData* t2) {
        emitQuadForward(p0, p1, p2, p3, w0, w1, w2, w3, t0, t1, t2);
    }

    /** @brief Emits a triangle that reuses an already emitted anchor vertex as its first corner. */
    void emitTriangleFrom(std::uint32_t anchor, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w1,
                          const HexTerrainWeights& w2, const HexCellData* t0, const HexCellData* t1,
                          const HexCellData* t2) {
        const std::uint32_t i1 = static_cast<std::uint32_t>(out_.vertexCount());
        // The anchor is already emitted, so its stored (perturbed) position is used as is.
        vertex(p1, w1, t0, t1, t2);
        vertex(p2, w2, t0, t1, t2);
        if (facesInward(positionOf(anchor), perturbedPosition(p1), perturbedPosition(p2)))
            out_.addTriangle(anchor, i1 + 1u, i1);
        else
            out_.addTriangle(anchor, i1, i1 + 1u);
    }

    /** @brief Four triangles fanning from `center` along one solid edge. */
    void appendEdgeFan(HexVec3 center, const EdgeVertices& edge, const HexCellData* cellData) {
        const HexTerrainWeights w          = HexTerrainWeights::primary();
        const HexVec3           samples[5] = {edge.v1, edge.v2, edge.v3, edge.v4, edge.v5};
        for (std::int32_t i = 0; i < 4; ++i) {
            emitTriangle(center, samples[i], samples[i + 1], w, cellData, cellData, cellData);
        }
    }

    /** @brief Four quads bridging `near` to `far`, blending the two cells' terrain types. */
    void appendBlendStrip(const EdgeVertices& near, const EdgeVertices& far, const HexTerrainWeights& nearWeights,
                          const HexTerrainWeights& farWeights, const HexCellData* nearCell,
                          const HexCellData* farCell) {
        const HexVec3 nearPositions[5] = {near.v1, near.v2, near.v3, near.v4, near.v5};
        const HexVec3 farPositions[5]  = {far.v1, far.v2, far.v3, far.v4, far.v5};
        for (std::int32_t i = 0; i < 4; ++i) {
            const float             t0 = static_cast<float>(i) * 0.25f;
            const float             t1 = static_cast<float>(i + 1) * 0.25f;
            const HexTerrainWeights w0 = HexTerrainWeights::lerp(nearWeights, farWeights, t0);
            const HexTerrainWeights w1 = HexTerrainWeights::lerp(nearWeights, farWeights, t1);
            // Both vertices of an edge share their parameter: the near pair carries w0
            // and the far pair carries w1.
            emitQuadForward(nearPositions[i], nearPositions[i + 1], farPositions[i], farPositions[i + 1], w0, w0, w1,
                            w1, nearCell, farCell, nearCell);
        }
    }

    /** @brief Terraces a slope between two edges; one band per terrace step plus the closing band. */
    void appendEdgeTerraces(EdgeVertices near, const HexTerrainWeights& nearWeights, EdgeVertices far,
                            const HexTerrainWeights& farWeights, const HexCellData* nearCell,
                            const HexCellData* farCell) {
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const HexTerrainWeights w1 = HexTerrainWeights::lerp(
                nearWeights, farWeights, static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize);
            const EdgeVertices middle = sphereEdgeTerraceLerp(near, far, step);
            appendEdgeBand(near, middle, nearWeights, w1, nearCell, farCell);
            near = middle;
        }
        appendEdgeBand(near, far, nearWeights, farWeights, nearCell, farCell);
    }

    /**
     * @brief Bridges two parallel edges that share their five samples.
     *
     * The planar builder spans a terrace band with the single chord `v1 -> v5`, which is
     * safe there only because its five samples are collinear. On a sphere the samples sit
     * on an arc, so a chord would leave a T-junction against the four-segment boundary of
     * the cell fan and open a crack along every terraced or vertical edge. The band is
     * therefore split exactly the way the flat blend strip and the fan are.
     */
    void appendEdgeBand(const EdgeVertices& from, const EdgeVertices& to, const HexTerrainWeights& fromWeights,
                        const HexTerrainWeights& toWeights, const HexCellData* nearCell, const HexCellData* farCell) {
        for (std::int32_t i = 0; i < 4; ++i) {
            emitQuadForward(sampleAt(from, i), sampleAt(from, i + 1), sampleAt(to, i), sampleAt(to, i + 1),
                            fromWeights, fromWeights, toWeights, toWeights, nearCell, farCell, nearCell);
        }
    }

    const HexSphereMap& map_;
    HexMeshData&        out_;
    float               perturbStrength_;
    float               streamBedDrop_;
};

/** @brief Emits one capped hexagonal sheet per flooded cell. */
class SphereWaterMesher {
public:
    SphereWaterMesher(const HexSphereMap& map, HexMeshData& out) noexcept : map_(map), out_(out) {}

    /** @brief Builds every flooded cell of the sphere. */
    void run() {
        for (HexSphereCell cell = 0; cell < map_.cellCount(); ++cell) buildCell(cell);
    }

private:
    /**
     * @brief Radius of the water surface over a cell.
     *
     * The planar `kWaterElevationOffset` is expressed in world units; on a sphere it
     * is converted to the same fraction of one elevation step the planar constant
     * already has, so the surface sits just under its own level at every radius.
     */
    [[nodiscard]] float waterRadius(std::int32_t waterLevel) const noexcept {
        const float offset = HexMetrics::kWaterElevationOffset / HexMetrics::kElevationStep;
        return map_.sphereRadius() + (static_cast<float>(waterLevel) + offset) * map_.elevationStep();
    }

    void buildCell(HexSphereCell cell) {
        const HexCellData* data = map_.cellAt(cell);
        if (data == nullptr || !data->values.isUnderwater()) return;

        // The shore parameter the water shader expects: 1 where the water ends against
        // land, 0 in open ocean.
        float              shore = 0.f;
        const std::int32_t edges = map_.neighborCount(cell);
        for (std::int32_t d = 0; d < edges; ++d) {
            const HexCellData* neighbour = map_.cellAt(map_.neighbor(cell, d));
            if (neighbour == nullptr || !neighbour->values.isUnderwater()) {
                shore = 1.f;
                break;
            }
        }

        const float        radius  = waterRadius(data->values.waterLevel());
        const HexVec3      center  = map_.direction(cell) * radius;
        const std::int32_t corners = map_.cornerCountOf(cell);
        for (std::int32_t k = 0; k < corners; ++k) {
            const HexVec3       a  = map_.cornerDirection(cell, k) * radius;
            const HexVec3       b  = map_.cornerDirection(cell, (k + 1) % corners) * radius;
            const std::uint32_t i0 = static_cast<std::uint32_t>(out_.vertexCount());
            out_.addVertex(center, shore, 0.f);
            out_.addVertex(a, shore, 0.f);
            out_.addVertex(b, shore, 0.f);
            // A cap wound inward is culled, which reads as a hole in the sea, so the orientation
            // is taken from the vertices rather than from the corner ordering.
            if (facesInward(center, a, b))
                out_.addTriangle(i0, i0 + 2u, i0 + 1u);
            else
                out_.addTriangle(i0, i0 + 1u, i0 + 2u);
        }
    }

    const HexSphereMap& map_;
    HexMeshData&        out_;
};

}  // namespace

void buildSphereTerrainMesh(const HexSphereMap& map, HexMeshData& out) {
    out.clear();
    if (map.empty()) {
        out.finalize();
        return;
    }

    SphereMesher mesher(map, out);
    mesher.run();
    out.finalize();
}

void buildSphereWaterMesh(const HexSphereMap& map, HexMeshData& out) {
    out.clear();
    if (map.empty()) {
        out.finalize();
        return;
    }

    SphereWaterMesher mesher(map, out);
    mesher.run();
    out.finalize();
}

}  // namespace eve::hexmap
