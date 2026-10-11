#include "hexmap/HexSphereMesh.h"

#include "hexmap/HexMapMesh.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace eve::hexmap {

namespace {

/**
 * @brief Perturbation contract of this translation unit.
 *
 * `SphereMesher::perturbedPosition` is the single place a position is displaced, so no position
 * is displaced twice - the emitters take the displaced corners once, decide on them, and store
 * those same values. Unlike the planar mesher, whose vertical noise term is folded
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
 * @brief Whether `EVP_NO_PERTURB` is set, read once and never in a release build.
 *
 * Turning the wobble off everywhere at once separates a property that only fails because a thin
 * sliver was pushed through its neighbour from one that is wrong in the emitted connectivity, which
 * is how the sphere test's folded-face bound was measured. Read once, not per vertex: this sits in
 * the innermost emitter, and `getenv` walks the environment block on every call.
 */
[[nodiscard]] bool perturbationDisabled() noexcept {
#ifndef NDEBUG
    static const bool disabled = std::getenv("EVP_NO_PERTURB") != nullptr;
    return disabled;
#else
    return false;
#endif
}

/**
 * @brief Tangential-only perturbation, `strength` along two tangent axes.
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
    if (perturbationDisabled()) return position;
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

/** @brief Whether two emitted positions weld to the same vertex. */
[[nodiscard]] bool samePoint(HexVec3 a, HexVec3 b) noexcept {
    // Matches the 1e-3 quantum the weld census uses, so "the same point" means the same thing
    // here and there. The shortest legitimate edge on the sphere is a couple of units, so this
    // cannot swallow real geometry.
    constexpr float kEpsilon = 1e-3f;
    return std::abs(a.x - b.x) < kEpsilon && std::abs(a.y - b.y) < kEpsilon && std::abs(a.z - b.z) < kEpsilon;
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
    /**
     * @brief `EVP_PERTURB_SCALE`, read once, defaulting to the design amplitude.
     *
     * The sphere test's folded-face bound is derived from sweeping this, so it has to stay
     * re-measurable; it does nothing in a release build, where the amplitude is always 1.
     */
    [[nodiscard]] static float perturbScaleFromEnvironment() noexcept {
#ifndef NDEBUG
        static const float scale = [] {
            if (const char* value = std::getenv("EVP_PERTURB_SCALE")) return static_cast<float>(std::strtod(value, nullptr));
            return 1.f;
        }();
        return scale;
#else
        return 1.f;
#endif
    }

    SphereMesher(const HexSphereMap& map, HexMeshData& out) noexcept
        : map_(map), out_(out),
          // The planar mesher perturbs by a fixed 40% of a cell's outer radius. `cellSpacing()` is
          // the centre-to-centre distance, which is `sqrt(3)` times that radius rather than equal to
          // it, so the fraction has to be divided by `sqrt(3)` to keep the same relative wobble.
          perturbStrength_((HexMetrics::kCellPerturbStrength / (HexMetrics::kOuterRadius * 1.7320508075688772f)) *
                           map.cellSpacing() * perturbScaleFromEnvironment()),          // River beds drop by the planar stream-bed offset expressed in elevation
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
                                 nearWeights, nearWeights, farWeights, farWeights, cellData, neighbourData, cellData);
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

        // Sorting by elevation is what lets the seven cases below be written against
        // "bottom/low/high", but it is a permutation of the canonical {up, left, right} order and
        // the parity of that permutation is *lost* when the three roles are renamed. An odd
        // permutation reverses the cyclic sense of the corner, so every triangle emitted for it
        // has to be wound the other way. The dispatch below only ever rotates the triple, which
        // is even, so this sort is the single source of parity for the whole corner.
        std::int32_t swaps = 0;
        for (std::int32_t i = 0; i < 2; ++i) {
            for (std::int32_t j = i + 1; j < 3; ++j) {
                if (elevationOf(cells[static_cast<std::size_t>(order[static_cast<std::size_t>(j)])]) <
                    elevationOf(cells[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])])) {
                    std::swap(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(j)]);
                    ++swaps;
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

        // Three cells at one elevation put all three solid corners on the same point, so the
        // patch has no area to fill. Emitting it anyway adds a triangle whose every edge is a
        // self-loop, and the census counts those as an edge used twice.
        if (samePoint(bottom, low) && samePoint(bottom, high)) return;

        const bool previousMirror = mirrorCorner_;
        const bool previousIn     = inCorner_;
        cornerParity_             = (swaps % 2) != 0;
        mirrorCorner_             = cornerParity_;
        inCorner_                 = true;
        if (lowEdge == HexEdgeType::Slope && highEdge == HexEdgeType::Slope) {
            mirrorCorner_ = cornerParity_;
            cornerTerraces(bottom, low, high, bottomCell, lowCell, highCell);
        } else if (lowEdge == HexEdgeType::Slope && highEdge == HexEdgeType::Flat) {
            // `low` is the odd cell out and `high` sits level with `bottom`, so the fan climbs
            // from that flat pair to `low` rather than descending from it.
            mirrorCorner_ = cornerParity_;
            cornerTerracesToApex(high, highCell, bottom, bottomCell, low, lowCell);
        } else if (lowEdge == HexEdgeType::Flat && highEdge == HexEdgeType::Slope) {
            // Mirror of the above: `bottom` and `low` are level and `high` is the apex.
            mirrorCorner_ = cornerParity_;
            cornerTerracesToApex(bottom, bottomCell, low, lowCell, high, highCell);
        } else if (lowEdge == HexEdgeType::Slope && highEdge == HexEdgeType::Cliff) {
            mirrorCorner_ = cornerParity_;
            cornerTerracesCliff(bottom, low, high, bottomCell, lowCell, highCell);
        } else if (lowEdge == HexEdgeType::Cliff && highEdge == HexEdgeType::Slope) {
            mirrorCorner_ = cornerParity_;
            cornerCliffTerraces(bottom, low, high, bottomCell, lowCell, highCell);
        } else if (edgeType(elevationOf(lowCell), elevationOf(highCell)) == HexEdgeType::Slope) {
            // Neither side out of `bottom` is a slope: both are cliffs, so both of the corner's
            // sides there are those walls' single straight end edges, and the only side carrying
            // rungs is `low -> high`. Fanning from `bottom` puts the two straight chords along the
            // walls and lays the ladder on the third side.
            //
            // The reference rotates its three cells here and hands them to a cliff-corner builder
            // in that rotated order; those builders read their arguments as bottom/low/high, so a
            // corner whose three cells all sit at different heights came out with the wrong side
            // terraced. It takes all three differing, which is why the sparse elevation patterns
            // never reached it and a generated planet does.
            mirrorCorner_ = cornerParity_;
            const HexTerrainWeights w = HexTerrainWeights::primary();
            appendBoundaryTriangle(bottom, w, low, w, high, w, bottomCell, lowCell, highCell);
        } else {
            mirrorCorner_ = cornerParity_;
            // Mirror of the reference winding: this engine's front face is the opposite
            // handedness, so the flat corner fan is emitted high-before-low.
            {
                emitTriangle(bottom, high, low, HexTerrainWeights::primary(), bottomCell, lowCell, highCell);
            }
        }
        mirrorCorner_ = previousMirror;
        inCorner_     = previousIn;
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

            if (step == 1) {
                // The first band is a triangle, not a quad. Both trailing points are `bottom`,
                // so `emitQuadForward` would add the degenerate `(boundaryRight, bottom, bottom)`
                // alongside the real triangle, and both of them traverse `bottom -> boundaryRight`
                // the same way - one directed edge used twice, i.e. a face wound against its
                // neighbour. The planar builder emits a triangle here for the same reason.
                emitTriangle(boundaryLeft, bottom, boundaryRight, wl, lastLeftWeights, wr, bottomCell, leftCell,
                             rightCell);
            } else if (!samePoint(boundaryLeft, lastLeft) || !samePoint(boundaryRight, lastRight)) {
                // The ladder always runs `kTerracesPerSlope * 2` steps, so a slope shallower than
                // that lands the later steps on the same point: the quad has no width and its two
                // triangles traverse the shared edge the same way. Skip it rather than emit it.
                emitQuadForward(boundaryLeft, boundaryRight, lastLeft, lastRight, wl, wr, lastLeftWeights,
                                lastRightWeights, bottomCell, leftCell, rightCell);
            }
            lastLeft         = boundaryLeft;
            lastRight        = boundaryRight;
            lastLeftWeights  = wl;
            lastRightWeights = wr;
        }
        if (!samePoint(lastLeft, left) || !samePoint(lastRight, right)) {
            // The closing band leads with the destination and trails with the last rung, exactly
            // like every band in the loop above. Passing `(lastLeft, lastRight, left, right)`
            // instead puts the last rung at the leading end of both this band and the one emitted
            // for the final step, so the two traverse their shared edge the same way.
            emitQuadForward(left, right, lastLeft, lastRight, leftWeights, rightWeights, lastLeftWeights,
                            lastRightWeights, bottomCell, leftCell, rightCell);
        }
    }

    /**
     * @brief Terraced corner fan whose ladder climbs from the two lower cells to the apex.
     *
     * This is `cornerTerraces` walked the other way, and the two directions are not
     * interchangeable: `sphereTerraceLerp` measures its vertical term from its first argument, so
     * a corner side only meets the edge band it borders when both are parameterized from the same
     * end - and every edge band starts at the lower cell of its edge. Without this the two
     * builders produce different rungs between the same endpoints, which leaves a crack the width
     * of one terrace step.
     *
     * The closing band degenerates at the apex, so it is emitted as a triangle.
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
            const HexVec3           bl = sphereTerraceLerp(left, apex, step);
            const HexVec3           br = sphereTerraceLerp(right, apex, step);

            emitQuadForward(lastLeft, lastRight, bl, br, lastLeftWeights, lastRightWeights, wl, wr, apexCell,
                            leftCell, rightCell);
            lastLeft         = bl;
            lastRight        = br;
            lastLeftWeights  = wl;
            lastRightWeights = wr;
        }
        emitTriangle(lastLeft, apex, lastRight, lastLeftWeights, apexWeights, lastRightWeights, apexCell, leftCell,
                     rightCell);
    }

    /**
     * @brief Fills a corner half that has exactly one terraced side: a fan from `apex` over it.
     *
     * The half is the triangle `apex`, `from`, `to`. Only the `from -> to` side is a border that
     * carries rungs - an edge band's end ladder - and the other two are chords: `to -> apex` runs
     * along a cliff wall's end edge and `apex -> from` is interior to the corner. Fanning from
     * `apex` is what keeps those two chords single segments, so the wall's end edge, which the wall
     * emits as one straight segment because a cliff is one ramp rather than a ladder, is met edge
     * for edge. The planar builder derives and verifies the same shape; a two-sided strip instead
     * drops rungs into the middle of the wall's edge and detaches the half entirely.
     */
    void appendBoundaryTriangle(HexVec3 apex, const HexTerrainWeights& apexWeights, HexVec3 from,
                                const HexTerrainWeights& fromWeights, HexVec3 to,
                                const HexTerrainWeights& toWeights, const HexCellData* t0, const HexCellData* t1,
                                const HexCellData* t2) {
        // When the ladder starts at the apex the first band has two coincident corners and covers
        // nothing, but it would still contribute three directed edges.
        const bool ladderStartsAtApex = samePoint(apex, from);
        HexVec3    last               = from;
        HexTerrainWeights lastWeights = fromWeights;
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const float             t    = static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize;
            const HexTerrainWeights w    = HexTerrainWeights::lerp(fromWeights, toWeights, t);
            const HexVec3           rung = sphereTerraceLerp(from, to, step);
            if (step > 1 || !ladderStartsAtApex) {
                emitTriangle(rung, last, apex, w, lastWeights, apexWeights, t0, t1, t2);
            }
            last        = rung;
            lastWeights = w;
        }
        emitTriangle(to, last, apex, toWeights, lastWeights, apexWeights, t0, t1, t2);
    }

    /**
     * @brief Terraced corner where the `high` side rises to a cliff.
     *
     * The corner's three sides are not alike, and each has to be built the way the patch opposite it
     * builds the same line. `bottom -> low` is a slope, so the band along that edge ends in a ladder
     * and this side must carry the same rungs. `bottom -> high` is the cliff: the wall bridges the
     * two solid edges as one ramp, so its end edge is a *single* segment and this side has to stay
     * straight. `low -> high` is whichever of the two the cells make it.
     *
     * This used to split the corner at a cliff-foot vertex `lerp(bottom, high, 1 / span)` placed on
     * the `bottom -> high` side, which is a vertex in the middle of the wall's end edge; the census
     * owned 84 of the sphere's 114 missing faces to this branch, six corners at fourteen edges each.
     * Neither half needs the foot. The planar builder carries the same derivation and is closed on
     * all nine of its elevation patterns with it.
     */
    void cornerTerracesCliff(const HexVec3& bottom, const HexVec3& low, const HexVec3& high,
                             const HexCellData* bottomCell, const HexCellData* lowCell, const HexCellData* highCell) {
        const HexTerrainWeights w = HexTerrainWeights::primary();
        if (edgeType(elevationOf(lowCell), elevationOf(highCell)) == HexEdgeType::Slope) {
            // Both `bottom -> low` and `low -> high` carry rungs, and each is measured from its own
            // lower end, which is the direction the bands along those edges use. The corner is
            // therefore a fan from `bottom` over the two ladders laid end to end, closed by the
            // straight `high -> bottom` chord, with the two calls meeting along the interior
            // `bottom -> low` chord.
            appendBoundaryTriangle(bottom, w, bottom, w, low, w, bottomCell, lowCell, highCell);
            appendBoundaryTriangle(bottom, w, low, w, high, w, bottomCell, lowCell, highCell);
        } else {
            // Only `bottom -> low` carries rungs. Fanning from `high` leaves `low -> high` and
            // `high -> bottom` as single segments, so the wall's end edge is met edge for edge.
            appendBoundaryTriangle(high, w, bottom, w, low, w, bottomCell, lowCell, highCell);
        }
    }

    /**
     * @brief Terraced corner where the `low` side rises to a cliff: the mirror of the above.
     *
     * Unreachable as the dispatch stands: the elevation sort guarantees `bottom <= low <= high`, so
     * `edgeType(bottom, low) == Cliff` and `edgeType(bottom, high) == Slope` cannot both hold, and
     * the branch census measures this arm at zero corners. It is kept as the mirror of
     * `cornerTerracesCliff` for the branch that would need it.
     */
    void cornerCliffTerraces(const HexVec3& bottom, const HexVec3& low, const HexVec3& high,
                             const HexCellData* bottomCell, const HexCellData* lowCell, const HexCellData* highCell) {
        const HexTerrainWeights w = HexTerrainWeights::primary();
        if (edgeType(elevationOf(highCell), elevationOf(lowCell)) == HexEdgeType::Slope) {
            appendBoundaryTriangle(bottom, w, bottom, w, high, w, bottomCell, lowCell, highCell);
            appendBoundaryTriangle(bottom, w, high, w, low, w, bottomCell, lowCell, highCell);
        } else {
            appendBoundaryTriangle(low, w, bottom, w, high, w, bottomCell, lowCell, highCell);
        }
    }

    // --- vertex and triangle primitives -------------------------------------

    /**
     * @brief Emits one vertex at a position that has already been perturbed.
     *
     * The emitters below decide on the perturbed positions - a degenerate guard and the sphere's
     * winding both read them - and those same positions are what gets stored. Perturbing a second
     * time inside `vertex` cost one extra noise sample per vertex in the innermost emitter.
     */
    void vertexAt(HexVec3 perturbed, const HexTerrainWeights& weights, const HexCellData* t0, const HexCellData* t1,
                  const HexCellData* t2) {
        const float u = HexTerrainVertexCode::encodeIndices(terrainOf(t0), terrainOf(t1), terrainOf(t2));
        const float v = HexTerrainVertexCode::encodeWeights(weights.b, weights.c);
        out_.addVertex(perturbed, u, v);
    }

    /** @brief The position that will actually be emitted for `position`. */
    [[nodiscard]] HexVec3 perturbedPosition(HexVec3 position) const noexcept {
        return tangentPerturb(map_.noise(), position, perturbStrength_);
    }

    /**
     * @brief Whether the triangle about to be emitted must be reversed to face outward.
     *
     * One convention for the whole mesh: emitters lay their vertices out in a fixed order, and the
     * only thing that ever reverses it is the parity of the corner sort that built the patch. No
     * geometric test is consulted, because none of them can read a cliff wall - so this takes no
     * positions. It used to be handed the perturbed corners, which cost three noise samples per
     * triangle in the innermost emitter and then ignored every one of them.
     */
    [[nodiscard]] bool reverseWinding() const noexcept { return !mirrorCorner_; }

    /** @brief Whether a triangle has two corners close enough to weld into a single vertex. */
    [[nodiscard]] static bool degenerate(HexVec3 a, HexVec3 b, HexVec3 c) noexcept {
        return samePoint(a, b) || samePoint(b, c) || samePoint(a, c);
    }

    /** @brief Emits a triangle whose three vertices share one weight set and one cell triple. */
    void emitTriangle(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w,
                      const HexCellData* t0, const HexCellData* t1, const HexCellData* t2) {
        // A triangle whose corners weld together covers no area, but its edges are real directed
        // edges: leaving it in splits one vertex into two and reports a boundary loop that is not
        // a hole. Dropping it cannot open one, because it covers nothing.
        const HexVec3 a0 = perturbedPosition(p0);
        const HexVec3 a1 = perturbedPosition(p1);
        const HexVec3 a2 = perturbedPosition(p2);
        if (degenerate(a0, a1, a2)) return;
        const std::uint32_t i0      = static_cast<std::uint32_t>(out_.vertexCount());
        const bool          reverse = reverseWinding();
        vertexAt(a0, w, t0, t1, t2);
        vertexAt(a1, w, t0, t1, t2);
        vertexAt(a2, w, t0, t1, t2);
        if (reverse)
            out_.addTriangle(i0, i0 + 2u, i0 + 1u);
        else
            out_.addTriangle(i0, i0 + 1u, i0 + 2u);
    }

    /** @brief Emits a triangle with one weight set per vertex. */
    void emitTriangle(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexTerrainWeights& w0,
                      const HexTerrainWeights& w1, const HexTerrainWeights& w2, const HexCellData* t0,
                      const HexCellData* t1, const HexCellData* t2) {
        const HexVec3 a0 = perturbedPosition(p0);
        const HexVec3 a1 = perturbedPosition(p1);
        const HexVec3 a2 = perturbedPosition(p2);
        if (degenerate(a0, a1, a2)) return;
        const std::uint32_t i0      = static_cast<std::uint32_t>(out_.vertexCount());
        const bool          reverse = reverseWinding();
        vertexAt(a0, w0, t0, t1, t2);
        vertexAt(a1, w1, t0, t1, t2);
        vertexAt(a2, w2, t0, t1, t2);
        if (reverse)
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
        const HexVec3       a0 = perturbedPosition(p0);
        const HexVec3       a1 = perturbedPosition(p1);
        const HexVec3       a2 = perturbedPosition(p2);
        const HexVec3       a3 = perturbedPosition(p3);
        // Each half is dropped on its own: a quad spanning a step can have one half collapse while
        // the other still covers area.
        const bool firstOk  = !degenerate(a0, a2, a1);
        const bool secondOk = !degenerate(a1, a2, a3);
        if (!firstOk && !secondOk) return;
        vertexAt(a0, w0, t0, t1, t2);
        vertexAt(a1, w1, t0, t1, t2);
        vertexAt(a2, w2, t0, t1, t2);
        vertexAt(a3, w3, t0, t1, t2);
        // Oriented as a unit: deciding each half on its own leaves the shared diagonal traversed
        // the same way twice whenever the two halves disagree, and one of them is then culled.
        const bool reverse = reverseWinding();
        if (firstOk) {
            if (reverse)
                out_.addTriangle(i0, i0 + 1u, i0 + 2u);
            else
                out_.addTriangle(i0, i0 + 2u, i0 + 1u);
        }
        if (secondOk) {
            if (reverse)
                out_.addTriangle(i0 + 2u, i0 + 1u, i0 + 3u);
            else
                out_.addTriangle(i0 + 1u, i0 + 2u, i0 + 3u);
        }
    }

    /** @brief Emits a radial quad in the bottom/top alternating order used by cliff strips. */
    void emitQuadVertical(const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, const HexVec3& p3,
                          const HexTerrainWeights& w0, const HexTerrainWeights& w1, const HexTerrainWeights& w2,
                          const HexTerrainWeights& w3, const HexCellData* t0, const HexCellData* t1,
                          const HexCellData* t2) {
        emitQuadForward(p0, p1, p2, p3, w0, w1, w2, w3, t0, t1, t2);
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
        // The ladder always climbs from the low side to the high side, whichever cell is emitting.
        // `sphereTerraceLerp` measures its vertical term from its *first* argument, so a ladder
        // built high-to-low lands on a different set of rungs than the neighbouring patches use;
        // the emitting side therefore only decides the band's argument order, which is winding.
        // The planar builder splits the same two cases (`appendSlopeTerraces`).
        const bool               nearIsLow = elevationOf(nearCell) <= elevationOf(farCell);
        const EdgeVertices&      low       = nearIsLow ? near : far;
        const EdgeVertices&      high      = nearIsLow ? far : near;
        const HexTerrainWeights& lowW      = nearIsLow ? nearWeights : farWeights;
        const HexTerrainWeights& highW     = nearIsLow ? farWeights : nearWeights;

        EdgeVertices      previous  = low;
        HexTerrainWeights previousW = lowW;
        for (std::int32_t step = 1; step < HexMetrics::kTerracesPerSlope * 2; ++step) {
            const HexTerrainWeights w = HexTerrainWeights::lerp(
                lowW, highW, static_cast<float>(step) * HexMetrics::kHorizontalTerraceStepSize);
            const EdgeVertices boundary = sphereEdgeTerraceLerp(low, high, step);
            if (nearIsLow)
                appendEdgeBand(previous, boundary, previousW, w, nearCell, farCell);
            else
                appendEdgeBand(boundary, previous, w, previousW, nearCell, farCell);
            previous  = boundary;
            previousW = w;
        }
        if (nearIsLow)
            appendEdgeBand(previous, high, previousW, highW, nearCell, farCell);
        else
            appendEdgeBand(high, previous, highW, previousW, nearCell, farCell);
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

    /**
     * @brief Whether the corner patch being emitted must be wound the other way round.
     *
     * Set by `appendCornerTriangles` from the parity of its elevation sort; false everywhere
     * else. This is what replaces guessing the orientation from the geometry: the sign of a
     * cliff wall tells you nothing, but the permutation that produced the patch tells you
     * exactly which way round it was built.
     */
    bool mirrorCorner_ = false;
    /** @brief Whether the emitter is currently inside a corner patch; see `mirrorCorner_`. */
    bool inCorner_ = false;
    /** @brief Parity of the corner sort, before any per-branch diagnostic override. */
    bool cornerParity_ = false;
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
