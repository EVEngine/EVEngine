#include "hexmap/HexMapMesh.h"

#include "hexmap/HexCoordinates.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace eve::hexmap {

namespace {

/**
 * @brief Perturbation contract of this translation unit.
 *
 * `HexMesher::vertex` is the single place a position is displaced, so no position is ever perturbed
 * twice. Every helper below therefore works on *unperturbed* positions. `HexMap::cellPosition()`
 * already carries the vertical noise term, so cell centres (and the edge frames built from them) take
 * only the horizontal part of the perturbation - the same split the map itself uses between
 * `HexNoise::perturb` (XZ) and `HexNoise::elevationPerturb` (Y). Vertices that are pure interpolations
 * between two such positions inherit their Y exactly, which is what keeps a chunk's seam with its
 * neighbour closed.
 */

/** @brief Terrain palette index of a cell, or `0` when the cell is absent. */
[[nodiscard]] std::int32_t terrainOf(const HexCellData* cell) noexcept {
    return cell == nullptr ? 0 : cell->values.terrainType();
}

/** @brief Elevation of a cell, or `0` when the cell is absent. */
[[nodiscard]] std::int32_t elevationOf(const HexCellData* cell) noexcept {
    return cell == nullptr ? 0 : cell->values.elevation();
}

/** @brief Horizontal-only perturbation, `±kCellPerturbStrength` in XZ with Y preserved. */
[[nodiscard]] HexVec3 horizontalPerturb(const HexNoise& noise, HexVec3 position) noexcept {
    const HexVec4 sample = noise.sample(position.x, position.z);
    position.x += (sample.x * 2.f - 1.f) * HexMetrics::kCellPerturbStrength;
    position.z += (sample.z * 2.f - 1.f) * HexMetrics::kCellPerturbStrength;
    return position;
}

/** @brief Sample `index` of an edge frame, `0` being `v1` and `4` being `v5`. */
[[nodiscard]] HexVec3 edgeSample(const EdgeVertices& edge, std::int32_t index) noexcept {
    switch (index) {
        case 0: return edge.v1;
        case 1: return edge.v2;
        case 2: return edge.v3;
        case 3: return edge.v4;
        default: return edge.v5;
    }
}

/**
 * @brief Whether the triangle `a`,`b`,`c` is wound so that its normal points downwards.
 *
 * The corner branches permute their three points by elevation, and whether that permutation
 * is even or odd depends on which of the three cells happened to emit the corner - so the
 * very same junction can come out wound either way, which is how two of a raised cell's six
 * corners ended up inverted. This engine's front face is the one that makes `(bottom, high,
 * low)` point up (see the flat corner in `appendCorner`); this predicate is how the terraced
 * branches check themselves against it. Only XZ is needed: a terrain corner is never vertical.
 */
[[nodiscard]] bool facesDown(HexVec3 a, HexVec3 b, HexVec3 c) noexcept {
    const float abx = b.x - a.x;
    const float abz = b.z - a.z;
    const float acx = c.x - a.x;
    const float acz = c.z - a.z;
    return (abz * acx - abx * acz) < 0.f;
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

/** @brief Emits the ground fans, blend strips, terraces and cliffs of one chunk. */
class HexMesher {
public:
    HexMesher(const HexMap& map, HexMeshData& out) noexcept : map_(map), out_(out) {}

    /** @brief Builds every cell of `chunkIndex`. */
    void run(std::int32_t chunkIndex) {
        for (std::int32_t row = 0; row < HexMetrics::kChunkSizeZ; ++row) {
            for (std::int32_t column = 0; column < HexMetrics::kChunkSizeX; ++column) {
                buildCell(map_.chunkCell(chunkIndex, column, row));
            }
        }
    }

private:
    // --- per-cell entry point -----------------------------------------------

    /** @brief Emits the six fans of one cell plus the connection strips it owns. */
    void buildCell(HexCoordinates coordinates) {
        if (!map_.contains(coordinates)) return;
        const HexCellData* cellData = map_.cell(coordinates);
        if (cellData == nullptr) return;

        const HexVec3 center = map_.cellPosition(coordinates);
        for (std::int32_t index = 0; index < kHexDirectionCount; ++index) {
            const auto direction = static_cast<HexDirection>(index);

            EdgeVertices near = solidEdge(center, direction);
            if (riverThrough(coordinates, direction, cellData)) {
                // Carve a shallow channel along the river centreline (the middle sample only).
                near.v3.y = HexMetrics::streamBedY(cellData->values.elevation());
            }
            appendEdgeFan(center, near, cellData);

            HexCoordinates neighbourCoordinates{};
            if (!map_.getNeighbor(coordinates, direction, neighbourCoordinates)) continue;
            const HexCellData* neighbour = map_.cell(neighbourCoordinates);
            if (neighbour == nullptr) continue;
            appendConnection(coordinates, center, near, direction, cellData, neighbour, neighbourCoordinates);
        }
    }

    /** @brief Whether a river crosses the edge shared with `direction`'s neighbour. */
    [[nodiscard]] bool riverThrough(HexCoordinates coordinates, HexDirection direction,
                                    const HexCellData* cellData) const noexcept {
        if (cellData != nullptr && cellData->flags.hasRiverThrough(direction)) return true;
        HexCoordinates neighbour{};
        if (!map_.getNeighbor(coordinates, direction, neighbour)) return false;
        const HexCellData* other = map_.cell(neighbour);
        return other != nullptr && other->flags.hasRiverThrough(opposite(direction));
    }

    /** @brief Emits the strip/terrace/cliff towards one neighbour and the corner it closes. */
    void appendConnection(HexCoordinates coordinates, HexVec3 center, const EdgeVertices& near, HexDirection direction,
                          const HexCellData* cellData, const HexCellData* neighbour,
                          HexCoordinates neighbourCoordinates) {
        // Chunk ownership rule: only the NE, E and SE edges of a cell belong to the
        // owning chunk, so every shared boundary is emitted exactly once - by the cell
        // that owns that edge, never by both. The reference gates its whole
        // `TriangulateConnection` call the same way. Emitting the strip from the other
        // side too puts two coplanar (or, on a slope, two interpenetrating) copies on
        // every boundary, which is what makes the terrain look like it has seams.
        //
        // An edge is shared by two cells, so half of the six directions is the right
        // fraction. A corner below is shared by *three* cells and therefore needs its
        // own, narrower gate; reusing this one emitted two of the three cells at half
        // of all junctions, i.e. two coincident corner patches.
        if (static_cast<std::int32_t>(direction) > static_cast<std::int32_t>(HexDirection::SE)) return;

        const EdgeVertices far = shiftedEdge(center, direction, map_.cellPosition(neighbourCoordinates).y);

        const HexTerrainWeights nearWeights = HexTerrainWeights::primary();
        const HexTerrainWeights farWeights  = HexTerrainWeights::blend(1.f);

        switch (map_.edgeTypeTo(coordinates, neighbourCoordinates)) {
            case HexEdgeType::Flat: appendBlendStrip(near, far, nearWeights, farWeights, cellData, neighbour); break;
            case HexEdgeType::Slope:
                appendSlopeTerraces(near, nearWeights, far, farWeights, cellData, neighbour,
                                    elevationOf(cellData) <= elevationOf(neighbour));
                break;
            case HexEdgeType::Cliff:
                // A wall is a steep ramp, not a true vertical: `far` is the neighbour's solid edge
                // bridged horizontally, raised to its elevation. Its winding therefore depends on
                // which of the two cells owns the edge, exactly like a terrace ladder, so it goes
                // through the same orientation check. Split into four sub-quads as well: a single
                // `v1 -> v5` chord would not follow the fan's four-segment border, because
                // `vertex()` perturbs every sample independently, and that mismatch is an open
                // seam along the wall's foot.
                for (std::int32_t i = 0; i < 4; ++i) {
                    emitQuadUpward(edgeSample(near, i), edgeSample(near, i + 1), edgeSample(far, i),
                                   edgeSample(far, i + 1), nearWeights, nearWeights, farWeights, farWeights, cellData,
                                   neighbour, cellData);
                }
                break;
        }

        // Corner gate, two directions out of six: each junction is shared by three
        // cells and each of them reaches it from one of its own six corners, so one
        // third of the (cell, corner) pairs must emit it. `NE`/`E` select exactly one
        // per junction; adding `SE` selected two at half of them.
        if (static_cast<std::int32_t>(direction) > static_cast<std::int32_t>(HexDirection::E)) return;

        HexCoordinates nextCoordinates{};
        if (!map_.getNeighbor(coordinates, next(direction), nextCoordinates)) return;
        const HexCellData* nextCell = map_.cell(nextCoordinates);

        CornerCells corner{};
        corner.up = near.v5;
        // Third corner vertex: this cell's corner carried across by the bridge of the
        // *next* edge, exactly as the reference builds `e1.v5 + GetBridge(d.Next())`.
        // Its Y comes from the next cell, so the point belongs to `nextCell`.
        corner.left      = near.v5 + HexMetrics::bridge(next(direction));
        corner.left.y    = map_.cellPosition(nextCoordinates).y;
        corner.right     = far.v5;
        // `appendCorner` sorts the three points by the elevation of the cell they belong
        // to, so each point must be paired with its *own* cell: `left` is the next
        // cell's corner, `right` is the neighbour's (`far` is the neighbour's edge).
        // Swapping the two picked the wrong terrace/cliff branch at every junction whose
        // three cells differ in elevation. The sphere backend, which cannot share this
        // code, pairs them this way.
        corner.upCell    = cellData;
        corner.leftCell  = nextCell;
        corner.rightCell = neighbour;
        appendCorner(corner);
    }

    // --- corner matrix ------------------------------------------------------

    /** @brief Sorts the three corner cells by elevation and dispatches on the two edge types. */
    void appendCorner(const CornerCells& corner) {
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
            // `low` is the odd cell out and `high` sits level with `bottom`, so the fan has to
            // climb from that flat pair to `low` rather than descend from it.
            cornerTerracesToApex(high, highCell, bottom, bottomCell, low, lowCell);
        } else if (lowEdge == HexEdgeType::Flat && highEdge == HexEdgeType::Slope) {
            // Mirror of the above: `bottom` and `low` are level and `high` is the apex.
            cornerTerracesToApex(bottom, bottomCell, low, lowCell, high, highCell);
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
            // handedness, so the flat corner fan is emitted high-before-low. "Flat" only
            // describes the two edges though - with cliffs on both of them these three points
            // still differ in height, and the elevation sort decides which of them is named
            // `low`, so the winding is taken from the points.
            emitCornerTriangle(bottom, high, low, HexTerrainWeights::primary(), HexTerrainWeights::primary(),
                               HexTerrainWeights::primary(), bottomCell, lowCell, highCell);
        }
    }

    /** @brief Five-step terraced fan across a corner whose both sides of `bottom` are slopes. */
    void cornerTerraces(const HexVec3& bottom, const HexVec3& left, const HexVec3& right, const HexCellData* bottomCell,
                        const HexCellData* leftCell, const HexCellData* rightCell) {
        const HexTerrainWeights bottomWeights = HexTerrainWeights::primary();
        const HexTerrainWeights leftWeights   = HexTerrainWeights::primary();
        const HexTerrainWeights rightWeights  = HexTerrainWeights::primary();

        // Same reason as the apex fan: the elevation sort permutes these points, so the fan
        // cannot pick a winding by construction.

        HexVec3           lastLeft         = bottom;
        HexVec3           lastRight        = bottom;
        HexTerrainWeights lastLeftWeights  = bottomWeights;
        HexTerrainWeights lastRightWeights = bottomWeights;
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const float             t             = static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize;
            const HexTerrainWeights wl            = HexTerrainWeights::lerp(bottomWeights, leftWeights, t);
            const HexTerrainWeights wr            = HexTerrainWeights::lerp(bottomWeights, rightWeights, t);
            const HexVec3           boundaryLeft  = HexMetrics::terraceLerp(bottom, left, step);
            const HexVec3           boundaryRight = HexMetrics::terraceLerp(bottom, right, step);

            // Both sides start at `bottom`, so the first band is a triangle. Emitting it as a
            // quad would add a zero-area one whose self-edge the welded census reports as an
            // open seam and whose doubled edge makes its neighbours look non-manifold.
            if (step == 1) {
                emitCornerTriangle(boundaryLeft, boundaryRight, bottom, wl, wr, bottomWeights, bottomCell, leftCell,
                                   rightCell);
            } else {
                emitQuadUpward(boundaryLeft, boundaryRight, lastLeft, lastRight, wl, wr, lastLeftWeights,
                               lastRightWeights, bottomCell, leftCell, rightCell);
            }
            lastLeft         = boundaryLeft;
            lastRight        = boundaryRight;
            lastLeftWeights  = wl;
            lastRightWeights = wr;
        }
        emitQuadUpward(lastLeft, lastRight, left, right, lastLeftWeights, lastRightWeights, leftWeights, rightWeights,
                       bottomCell, leftCell, rightCell);
    }

    /**
     * @brief Terraced corner fan whose ladder climbs from the two lower cells to the apex.
     *
     * This is `cornerTerraces` walked the other way, and the two directions are not
     * interchangeable: `HexMetrics::terraceLerp` is direction-asymmetric, so a corner side
     * only meets the edge band it borders when both are parameterized from the same end -
     * and every edge band starts at the lower cell of its edge.
     *
     * `appendCorner` reaches this shape whenever the corner's odd cell out is the *highest*
     * one, which is exactly what a single raised cell produces: the ladder descends from the
     * apex there, while the bands along its two edges climb to it.
     *
     * Quads are emitted high-side-first, this engine's front-face convention (the flat corner
     * in `appendCorner` documents it). The closing band degenerates at the apex, so it is
     * emitted as a triangle - a zero-area quad would only add a self-edge that the welded
     * census reports as an open seam.
     *
     * @param left Lower corner point of the left side.
     * @param leftCell Cell owning `left`.
     * @param right Lower corner point of the right side.
     * @param rightCell Cell owning `right`.
     * @param apex Higher corner point both sides climb to.
     * @param apexCell Cell owning `apex`.
     */
    void cornerTerracesToApex(const HexVec3& left, const HexCellData* leftCell, const HexVec3& right,
                              const HexCellData* rightCell, const HexVec3& apex, const HexCellData* apexCell) {
        const HexTerrainWeights leftWeights  = HexTerrainWeights::primary();
        const HexTerrainWeights rightWeights = HexTerrainWeights::primary();
        const HexTerrainWeights apexWeights  = HexTerrainWeights::primary();

        HexVec3           lastLeft         = left;
        HexVec3           lastRight        = right;
        HexTerrainWeights lastLeftWeights  = leftWeights;
        HexTerrainWeights lastRightWeights = rightWeights;
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const float             t  = static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize;
            const HexTerrainWeights wl = HexTerrainWeights::lerp(leftWeights, apexWeights, t);
            const HexTerrainWeights wr = HexTerrainWeights::lerp(rightWeights, apexWeights, t);
            const HexVec3           bl = HexMetrics::terraceLerp(left, apex, step);
            const HexVec3           br = HexMetrics::terraceLerp(right, apex, step);

            emitQuadUpward(lastLeft, lastRight, bl, br, lastLeftWeights, lastRightWeights, wl, wr, apexCell, leftCell,
                           rightCell);
            lastLeft         = bl;
            lastRight        = br;
            lastLeftWeights  = wl;
            lastRightWeights = wr;
        }
        emitCornerTriangle(lastLeft, apex, lastRight, lastLeftWeights, apexWeights, lastRightWeights, apexCell,
                           leftCell, rightCell);
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
        HexVec3 leftSide  = HexMetrics::terraceLerp(bottom, left, 1);
        HexVec3 rightSide = HexMetrics::terraceLerp(bottom, right, 1);
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const float             t  = static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize;
            const HexTerrainWeights wl = HexTerrainWeights::lerp(bottomWeights, leftWeights, t);
            const HexTerrainWeights wr = HexTerrainWeights::lerp(bottomWeights, rightWeights, t);

            emitCornerTriangle(leftSide, rightSide, bottom, wl, wr, bottomWeights, bottomCell, leftCell, rightCell);

            leftSide  = rightSide;
            rightSide = HexMetrics::terraceLerp(bottom, right, step + 1);
        }
        emitCornerTriangle(leftSide, rightSide, bottom, leftWeights, rightWeights, bottomWeights, bottomCell, leftCell,
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
        emitCornerTriangleFrom(anchor, boundary, low, lowWeights, bottom, bottomWeights, bottomCell, lowCell, highCell);
        if (lowEdge == HexEdgeType::Slope) {
            appendBoundaryTriangle(boundary, boundaryWeights, bottom, bottomWeights, high, highWeights, bottomCell,
                                   lowCell, highCell);
        } else {
            emitCornerTriangle(high, bottom, low, highWeights, bottomWeights, lowWeights, bottomCell, lowCell,
                               highCell);
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
        emitCornerTriangleFrom(anchor, boundary, high, highWeights, bottom, bottomWeights, bottomCell, highCell,
                               lowCell);
        if (highEdge == HexEdgeType::Slope) {
            appendBoundaryTriangle(boundary, boundaryWeights, bottom, bottomWeights, low, lowWeights, bottomCell,
                                   highCell, lowCell);
        } else {
            emitCornerTriangle(low, bottom, high, lowWeights, bottomWeights, highWeights, bottomCell, highCell,
                               lowCell);
        }
    }

    // --- vertex and triangle primitives -------------------------------------

    /** @brief Emits one vertex; the single perturbation point of this file. */
    void vertex(HexVec3 position, const HexTerrainWeights& weights, const HexCellData* t0, const HexCellData* t1,
                const HexCellData* t2) {
        const float u = HexTerrainVertexCode::encodeIndices(terrainOf(t0), terrainOf(t1), terrainOf(t2));
        const float v = HexTerrainVertexCode::encodeWeights(weights.b, weights.c);
        out_.addVertex(horizontalPerturb(map_.noise(), position), u, v);
    }

    /** @brief Emits a triangle whose three vertices share one weight set and one cell triple. */
    void emitTriangle(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w,
                      const HexCellData* t0, const HexCellData* t1, const HexCellData* t2) {
        const std::uint32_t i0 = static_cast<std::uint32_t>(out_.vertexCount());
        vertex(p0, w, t0, t1, t2);
        vertex(p1, w, t0, t1, t2);
        vertex(p2, w, t0, t1, t2);
        out_.addTriangle(i0, i0 + 1u, i0 + 2u);
    }

    /** @brief Emits a triangle with one weight set per vertex. */
    void emitTriangle(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w0,
                      const HexTerrainWeights& w1, const HexTerrainWeights& w2, const HexCellData* t0,
                      const HexCellData* t1, const HexCellData* t2) {
        const std::uint32_t i0 = static_cast<std::uint32_t>(out_.vertexCount());
        vertex(p0, w0, t0, t1, t2);
        vertex(p1, w1, t0, t1, t2);
        vertex(p2, w2, t0, t1, t2);
        out_.addTriangle(i0, i0 + 1u, i0 + 2u);
    }

    /**
     * @brief Emits a horizontal quad `(a, b, c, d)` as `(a, c, b)` and `(b, c, d)`.
     *
     * The argument order is the `a -> b -> d -> c` ring used by `HexMeshData::addQuad`.
     * Splitting along the other diagonal instead (`(a,b,c),(b,c,d)`) gives the two
     * triangles opposite face normals, which the renderer turns into a sawtooth band
     * along every hex edge.
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
        out_.addTriangle(i0, i0 + 2u, i0 + 1u);
        out_.addTriangle(i0 + 1u, i0 + 2u, i0 + 3u);
    }

    /** @brief Emits a triangle that reuses an already emitted anchor vertex as its first corner. */
    void emitTriangleFrom(std::uint32_t anchor, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w1,
                          const HexTerrainWeights& w2, const HexCellData* t0, const HexCellData* t1,
                          const HexCellData* t2) {
        const std::uint32_t i1 = static_cast<std::uint32_t>(out_.vertexCount());
        vertex(p1, w1, t0, t1, t2);
        vertex(p2, w2, t0, t1, t2);
        out_.addTriangle(anchor, i1, i1 + 1u);
    }

    /**
     * @brief Emits a horizontal quad, mirrored when it would otherwise face downwards.
     *
     * A corner fan lists its two sides in an order that depends on which of the three cells
     * emitted the corner and on how the elevation sort permuted them, so no caller can pick
     * the winding by construction. Reading it off the emitted triangle keeps every fan
     * consistent without depending on that order. Cliff walls are steep ramps rather than true
     * verticals - their far edge is the neighbour's solid edge, bridged horizontally - so they
     * are part of the heightfield and take the same treatment.
     */
    void emitQuadUpward(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexVec3& p3,
                        const HexTerrainWeights& w0, const HexTerrainWeights& w1, const HexTerrainWeights& w2,
                        const HexTerrainWeights& w3, const HexCellData* t0, const HexCellData* t1,
                        const HexCellData* t2) {
        // Each triangle is oriented on its own: a quad spanning a terrace or a corner is not
        // planar, so its two halves can disagree and orienting the first one alone leaves the
        // other facing down.
        const std::uint32_t i0 = static_cast<std::uint32_t>(out_.vertexCount());
        vertex(p0, w0, t0, t1, t2);
        vertex(p1, w1, t0, t1, t2);
        vertex(p2, w2, t0, t1, t2);
        vertex(p3, w3, t0, t1, t2);
        if (facesDown(p0, p2, p1))
            out_.addTriangle(i0, i0 + 1u, i0 + 2u);
        else
            out_.addTriangle(i0, i0 + 2u, i0 + 1u);
        if (facesDown(p1, p2, p3))
            out_.addTriangle(i0 + 1u, i0 + 3u, i0 + 2u);
        else
            out_.addTriangle(i0 + 1u, i0 + 2u, i0 + 3u);
    }

    /**
     * @brief Emits a corner triangle, choosing the winding that makes it face upwards.
     *
     * The cliff corner branches permute their three points by elevation just like the flat
     * ones, so the same junction can be listed in either cyclic order depending on which
     * cell emitted it.
     */
    void emitCornerTriangle(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w0,
                            const HexTerrainWeights& w1, const HexTerrainWeights& w2, const HexCellData* t0,
                            const HexCellData* t1, const HexCellData* t2) {
        if (facesDown(p0, p1, p2))
            emitTriangle(p0, p2, p1, w0, w2, w1, t0, t1, t2);
        else
            emitTriangle(p0, p1, p2, w0, w1, w2, t0, t1, t2);
    }

    /** @brief `emitTriangleFrom` with the winding chosen so the face points upwards. */
    void emitCornerTriangleFrom(std::uint32_t anchor, const HexVec3& anchorPoint, const HexVec3& p1,
                                const HexTerrainWeights& w1, const HexVec3& p2, const HexTerrainWeights& w2,
                                const HexCellData* t0, const HexCellData* t1, const HexCellData* t2) {
        const std::uint32_t i1 = static_cast<std::uint32_t>(out_.vertexCount());
        vertex(p1, w1, t0, t1, t2);
        vertex(p2, w2, t0, t1, t2);
        if (facesDown(anchorPoint, p1, p2))
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
        for (std::int32_t i = 0; i < 4; ++i) {
            const float             t0 = static_cast<float>(i) * 0.25f;
            const float             t1 = static_cast<float>(i + 1) * 0.25f;
            const HexTerrainWeights w0 = HexTerrainWeights::lerp(nearWeights, farWeights, t0);
            const HexTerrainWeights w1 = HexTerrainWeights::lerp(nearWeights, farWeights, t1);
            // Both vertices of an edge share their parameter: the near pair carries w0
            // and the far pair carries w1. Swapping them makes every quad blend along the
            // wrong axis and turns the band into a sawtooth.
            emitQuadForward(edgeSample(near, i), edgeSample(near, i + 1), edgeSample(far, i), edgeSample(far, i + 1),
                            w0, w0, w1, w1, nearCell, farCell, nearCell);
        }
    }

    /**
     * @brief Bridges two edges that share their five samples, one quad per sample pair.
     *
     * `from`/`to` are used in the order given and carry their own constant weight set;
     * `fromCell`/`toCell` follow the same order. Emission is always near-side-first, so
     * callers pass the spatially-near edge first regardless of which end the ladder was
     * parameterized from.
     */
    void appendEdgeBand(const EdgeVertices& from, const HexTerrainWeights& fromWeights, const EdgeVertices& to,
                        const HexTerrainWeights& toWeights, const HexCellData* fromCell, const HexCellData* toCell) {
        for (std::int32_t i = 0; i < 4; ++i) {
            emitQuadForward(edgeSample(from, i), edgeSample(from, i + 1), edgeSample(to, i), edgeSample(to, i + 1),
                            fromWeights, fromWeights, toWeights, toWeights, fromCell, toCell, fromCell);
        }
    }

    /**
     * @brief Terraces a slope between two edges, always walking the ladder from the lower cell.
     *
     * Two things have to line up along a terraced edge, and both are direction sensitive
     * because `HexMetrics::terraceLerp` is asymmetric - the vertical offset uses
     * `((step + 1) / 2)`, measured from its first argument:
     *
     *  1. The band's border has to be the same four-segment polyline as the cell fan's.
     *     Spanning a band with the single `v1 -> v5` chord does not do that: the samples are
     *     collinear before perturbation, but `vertex()` displaces each of them
     *     independently, so the chord and the polyline diverge and every terraced edge
     *     opens a seam.
     *  2. The ladder has to start at the same end as the corner patches, which always
     *     interpolate from the lowest of their three cells (`appendCorner`). Walking from
     *     whichever cell owns the edge put the two opposite ends together wherever the
     *     owner was the *higher* cell.
     *
     * The bands are still emitted near-side-first, because the quad winding depends on it;
     * only the parameterization origin changes.
     *
     * @param near Edge of the cell emitting this connection.
     * @param far Edge of the neighbour.
     * @param nearIsLow Whether `near` is the lower of the two cells.
     */
    void appendSlopeTerraces(const EdgeVertices& near, const HexTerrainWeights& nearWeights, const EdgeVertices& far,
                             const HexTerrainWeights& farWeights, const HexCellData* nearCell,
                             const HexCellData* farCell, bool nearIsLow) {
        const EdgeVertices&      low   = nearIsLow ? near : far;
        const EdgeVertices&      high  = nearIsLow ? far : near;
        const HexTerrainWeights& lowW  = nearIsLow ? nearWeights : farWeights;
        const HexTerrainWeights& highW = nearIsLow ? farWeights : nearWeights;

        EdgeVertices      previous  = low;
        HexTerrainWeights previousW = lowW;
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const EdgeVertices      boundary = EdgeVertices::terraceLerp(low, high, step);
            const HexTerrainWeights weights =
                HexTerrainWeights::lerp(lowW, highW, static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize);
            if (nearIsLow)
                appendEdgeBand(previous, previousW, boundary, weights, nearCell, farCell);
            else
                appendEdgeBand(boundary, weights, previous, previousW, nearCell, farCell);
            previous  = boundary;
            previousW = weights;
        }
        if (nearIsLow)
            appendEdgeBand(previous, previousW, high, highW, nearCell, farCell);
        else
            appendEdgeBand(high, highW, previous, previousW, nearCell, farCell);
    }

    // --- edge frames --------------------------------------------------------

    /** @brief Edge frame of the solid border in `direction`, centred on `center` at its own elevation. */
    [[nodiscard]] static EdgeVertices solidEdge(HexVec3 center, HexDirection direction) noexcept {
        return EdgeVertices{center + HexMetrics::firstSolidCorner(direction),
                            center + HexMetrics::secondSolidCorner(direction)};
    }

    /** @brief Solid border of `direction`, bridged to the neighbour and raised to `targetY`. */
    [[nodiscard]] static EdgeVertices shiftedEdge(HexVec3 center, HexDirection direction, float targetY) noexcept {
        const HexVec3 bridge = HexMetrics::bridge(direction);
        EdgeVertices  edge   = solidEdge(center, direction);
        edge.v1              = edge.v1 + bridge;
        edge.v2              = edge.v2 + bridge;
        edge.v3              = edge.v3 + bridge;
        edge.v4              = edge.v4 + bridge;
        edge.v5              = edge.v5 + bridge;
        edge.v1.y            = targetY;
        edge.v2.y            = targetY;
        edge.v3.y            = targetY;
        edge.v4.y            = targetY;
        edge.v5.y            = targetY;
        return edge;
    }

    const HexMap& map_;
    HexMeshData&  out_;
};

}  // namespace

void buildTerrainMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out) {
    out.clear();
    if (chunkIndex < 0 || chunkIndex >= map.chunkCount()) {
        out.finalize();
        return;
    }

    HexMesher mesher(map, out);
    mesher.run(chunkIndex);
    out.finalize();
}

}  // namespace eve::hexmap
