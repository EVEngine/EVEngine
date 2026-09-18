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
            case HexEdgeType::Slope: appendEdgeTerraces(near, nearWeights, far, farWeights, cellData, neighbour); break;
            case HexEdgeType::Cliff:
                // The blend strip would already be a vertical wall here, but its winding flips with the
                // sign of the elevation step; the vertical winding is correct in both directions.
                emitQuadVertical(near.v1, near.v5, far.v1, far.v5, nearWeights, nearWeights, farWeights, farWeights,
                                 cellData, neighbour, cellData);
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
            const HexVec3           boundaryLeft  = HexMetrics::terraceLerp(bottom, left, step);
            const HexVec3           boundaryRight = HexMetrics::terraceLerp(bottom, right, step);

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
        HexVec3 leftSide  = HexMetrics::terraceLerp(bottom, left, 1);
        HexVec3 rightSide = HexMetrics::terraceLerp(bottom, right, 1);
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const float             t  = static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize;
            const HexTerrainWeights wl = HexTerrainWeights::lerp(bottomWeights, leftWeights, t);
            const HexTerrainWeights wr = HexTerrainWeights::lerp(bottomWeights, rightWeights, t);

            emitTriangle(leftSide, rightSide, bottom, wl, wr, bottomWeights, bottomCell, leftCell, rightCell);

            leftSide  = rightSide;
            rightSide = HexMetrics::terraceLerp(bottom, right, step + 1);
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

    /** @brief Emits a vertical quad in the bottom/top alternating order used by cliff strips. */
    void emitQuadVertical(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexVec3& p3,
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
            // and the far pair carries w1. Swapping them makes every quad blend along the
            // wrong axis and turns the band into a sawtooth.
            emitQuadForward(nearPositions[i], nearPositions[i + 1], farPositions[i], farPositions[i + 1], w0, w0, w1,
                            w1, nearCell, farCell, nearCell);
        }
    }

    /** @brief Terraces a slope between two edges; one quad per terrace step plus the closing quad. */
    void appendEdgeTerraces(EdgeVertices near, const HexTerrainWeights& nearWeights, EdgeVertices far,
                            const HexTerrainWeights& farWeights, const HexCellData* nearCell,
                            const HexCellData* farCell) {
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const HexTerrainWeights w1 = HexTerrainWeights::lerp(
                nearWeights, farWeights, static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize);
            const EdgeVertices middle = EdgeVertices::terraceLerp(near, far, step);
            emitQuadForward(near.v1, near.v5, middle.v1, middle.v5, nearWeights, nearWeights, w1, w1, nearCell, farCell,
                            nearCell);
            near = middle;
        }
        emitQuadForward(near.v1, near.v5, far.v1, far.v5, nearWeights, nearWeights, farWeights, farWeights, nearCell,
                        farCell, nearCell);
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
