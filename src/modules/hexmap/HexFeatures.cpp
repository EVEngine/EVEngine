/**
 * @file HexFeatures.cpp
 * @brief Walls, towers, bridges and decorations of one hex chunk.
 */

#include "hexmap/HexFeatures.h"

#include "hexmap/HexCell.h"
#include "hexmap/HexCoordinates.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"

#include <cmath>
#include <cstdint>

namespace eve::hexmap {

namespace {

/**
 * @brief Perturbation contract of this translation unit.
 *
 * `HexMap::cellPosition()` already carries the vertical noise term, so every
 * position derived from a cell centre takes only the horizontal part of the
 * perturbation, the same split the terrain builder uses. Feature positions are
 * perturbed exactly once, inside the geometry emitters, so no helper here
 * displaces a position on its own.
 */
[[nodiscard]] HexVec3 horizontalPerturb(const HexNoise& noise, HexVec3 position) noexcept {
    const HexVec4 sample = noise.sample(position.x, position.z);
    position.x += (sample.x * 2.f - 1.f) * HexMetrics::kCellPerturbStrength;
    position.z += (sample.z * 2.f - 1.f) * HexMetrics::kCellPerturbStrength;
    return position;
}

/** @brief Dot product of two hex vectors. */
[[nodiscard]] float dot(HexVec3 a, HexVec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

/** @brief Cross product of two hex vectors. */
[[nodiscard]] HexVec3 cross(HexVec3 a, HexVec3 b) noexcept {
    return HexVec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/** @brief Unit-length `v`, or the zero vector when `v` is degenerate. */
[[nodiscard]] HexVec3 normalize(HexVec3 v) noexcept {
    const float length = std::sqrt(dot(v, v));
    if (length <= 1e-6f) return HexVec3{};
    return HexVec3{v.x / length, v.y / length, v.z / length};
}

/** @brief Whether a vector is the zero vector. */
[[nodiscard]] bool isZero(HexVec3 v) noexcept { return v.x == 0.f && v.y == 0.f && v.z == 0.f; }

/**
 * @brief Rotates a horizontal vector about the world up axis.
 *
 * @param orientation Hash component in `[0, 1)`; the full turn is applied here,
 *                    matching the reference's `360 degrees * hash`.
 * @param v Vector to rotate; its Y component is preserved.
 */
[[nodiscard]] HexVec3 yaw(float orientation, HexVec3 v) noexcept {
    const float angle    = orientation * 6.28318530718f;
    const float cosine   = std::cos(angle);
    const float sine     = std::sin(angle);
    return HexVec3{v.x * cosine - v.z * sine, v.y, v.x * sine + v.z * cosine};
}

/** @brief Texture code of a plain wall segment. */
constexpr float kWallCode = 0.f;
/** @brief Texture code of a wall tower. */
constexpr float kTowerCode = 1.f;
/** @brief Texture code of a bridge. */
constexpr float kBridgeCode = 2.f;
/** @brief Texture code of an urban decoration. */
constexpr float kUrbanCode = 3.f;
/** @brief Texture code of a farm decoration. */
constexpr float kFarmCode = 4.f;
/** @brief Texture code of a plant decoration. */
constexpr float kPlantCode = 5.f;
/** @brief Texture code of a special decoration. */
constexpr float kSpecialCode = 6.f;

/** @brief Half-width of a bridge slab relative to a cell's inner radius. */
constexpr float kBridgeHalfWidth = 0.25f;
/** @brief Half-thickness of a bridge slab relative to the wall height. */
constexpr float kBridgeHalfThickness = 0.15f;
/** @brief Half-extent of a wall tower relative to the wall thickness. */
constexpr float kTowerHalfExtent = 1.5f;
/** @brief Height of a wall tower relative to the wall thickness. */
constexpr float kTowerHeight = 2.f;

/** @brief Emits a triangle whose three vertices share one texture code. */
void emitTriangle(HexMeshData& out, const HexNoise& noise, const HexVec3& p0, const HexVec3& p1, const HexVec3& p2,
                  float code) {
    const auto i0 = static_cast<std::uint32_t>(out.vertexCount());
    out.addVertex(horizontalPerturb(noise, p0), code, 0.f);
    out.addVertex(horizontalPerturb(noise, p1), code, 0.f);
    out.addVertex(horizontalPerturb(noise, p2), code, 0.f);
    out.addTriangle(i0, i0 + 1u, i0 + 2u);
}

/**
 * @brief Emits a quad from an edge grid: corner, then the two edge vectors.
 *
 * The emitted ring is `c`, `c + e1`, `c + e1 + e2`, `c + e2`. Both emitted
 * triangles then face the direction of `cross(e1, e2)`, so callers order the
 * two edge vectors such that their cross product points outwards. Passing the
 * corner as the far end of the edges instead flips both triangles, and swapping
 * the two edges flips them the other way.
 */
void emitEdgeQuad(HexMeshData& out, const HexNoise& noise, const HexVec3& corner, const HexVec3& e1, const HexVec3& e2,
                  float code) {
    const auto i0 = static_cast<std::uint32_t>(out.vertexCount());
    out.addVertex(horizontalPerturb(noise, corner), code, 0.f);
    out.addVertex(horizontalPerturb(noise, corner + e1), code, 0.f);
    out.addVertex(horizontalPerturb(noise, corner + e1 + e2), code, 0.f);
    out.addVertex(horizontalPerturb(noise, corner + e2), code, 0.f);
    out.addTriangle(i0 + 2u, i0 + 1u, i0);
    out.addTriangle(i0 + 3u, i0 + 2u, i0);
}

/** @brief Emits one face of an axis-aligned box centred on `centre`. */
void emitBoxFace(HexMeshData& out, const HexNoise& noise, HexVec3 centre, HexVec3 e1, HexVec3 e2, float code) {
    const HexVec3 corner = centre - e1 * 0.5f - e2 * 0.5f;
    emitEdgeQuad(out, noise, corner, e1, e2, code);
}

/**
 * @brief Emits a closed box centred on `origin`.
 *
 * @param right First unit axis.
 * @param up Second unit axis; must be orthogonal to `right`.
 * @param fwd Third unit axis; must be `cross(right, up)` so the box closes.
 * @param halfExtents Half-size along `right`, `up` and `fwd`.
 * @param code Texture code written into every vertex.
 */
void emitBox(HexMeshData& out, const HexNoise& noise, HexVec3 origin, HexVec3 right, HexVec3 up, HexVec3 fwd,
             HexVec3 halfExtents, float code) {
    const HexVec3 r = right * (2.f * halfExtents.x);
    const HexVec3 u = up * (2.f * halfExtents.y);
    const HexVec3 f = fwd * (2.f * halfExtents.z);

    // `r`, `u` and `f` are the box's full edge vectors, so each face's two edge
    // arguments below are complete edges of that face.
    emitBoxFace(out, noise, origin + r * 0.5f, u, f * -1.f, code);
    emitBoxFace(out, noise, origin - r * 0.5f, u, f, code);
    emitBoxFace(out, noise, origin + u * 0.5f, r, f, code);
    emitBoxFace(out, noise, origin - u * 0.5f, r, f * -1.f, code);
    emitBoxFace(out, noise, origin - f * 0.5f, r, u, code);
    emitBoxFace(out, noise, origin + f * 0.5f, r, u * -1.f, code);
}

/** @brief Emits the walls, wall towers and bridges of one chunk. */
class WallMesher {
public:
    WallMesher(const HexMap& map, HexMeshData& out) noexcept : map_(map), out_(out) {}

    /** @brief Builds every cell of `chunkIndex`. */
    void run(std::int32_t chunkIndex) {
        for (std::int32_t row = 0; row < HexMetrics::kChunkSizeZ; ++row) {
            for (std::int32_t column = 0; column < HexMetrics::kChunkSizeX; ++column) {
                buildCell(map_.chunkCell(chunkIndex, column, row));
            }
        }
    }

private:
    /** @brief Emits the walls of the edges this chunk owns plus the cell's corners. */
    void buildCell(HexCoordinates coordinates) {
        if (!map_.contains(coordinates)) return;
        const HexCellData* cellData = map_.cell(coordinates);
        if (cellData == nullptr) return;

        const HexVec3 center = map_.cellPosition(coordinates);
        for (std::int32_t index = 0; index < kHexDirectionCount; ++index) {
            const auto direction = static_cast<HexDirection>(index);

            HexCoordinates     neighbour{};
            const bool         hasNeighbour  = map_.getNeighbor(coordinates, direction, neighbour);
            const HexCellData* neighbourData = hasNeighbour ? map_.cell(neighbour) : nullptr;
            const HexVec3      neighbourPosition = hasNeighbour ? map_.cellPosition(neighbour) : HexVec3{};

            const bool isOwnDirection =
                static_cast<std::int32_t>(direction) <= static_cast<std::int32_t>(HexDirection::SE);
            const EdgeVertices near = solidEdge(center, direction);
            const float        farY = neighbourData != nullptr ? neighbourPosition.y : near.v1.y;
            const EdgeVertices far  = shiftedEdge(center, direction, farY);

            // Chunk ownership: a wall is emitted for the NE, E and SE edges of a
            // cell, or when the neighbour is outside the grid. Every interior
            // edge is the NE/E/SE edge of exactly one of its two cells, so no
            // wall is generated twice.
            if (isOwnDirection || neighbourData == nullptr) {
                const bool hasRiver = map_.hasRiverThrough(coordinates, direction) ||
                                      (hasNeighbour && map_.hasRiverThrough(neighbour, opposite(direction)));
                const bool hasRoad = cellData->flags.hasRoad(direction) ||
                                     (hasNeighbour && neighbourData->flags.hasRoad(opposite(direction)));
                addWall(near, cellData, far, neighbourData, hasRiver, hasRoad);

                if (neighbourData != nullptr && cellData->flags.hasRoad(direction) && hasRiver) {
                    addBridge(center, neighbourPosition);
                }
            }

            if (!isOwnDirection) continue;

            HexCoordinates nextCoordinates{};
            if (!map_.getNeighbor(coordinates, next(direction), nextCoordinates)) continue;
            const HexCellData* nextCell = map_.cell(nextCoordinates);
            if (nextCell == nullptr) continue;

            HexVec3 left = near.v5 + HexMetrics::bridge(next(direction));
            left.y       = map_.cellPosition(nextCoordinates).y;
            addWallCorner(near.v5, cellData, left, neighbourData, far.v5, nextCell);
        }
    }

    /** @brief Emits the wall between two cell edges when only one side is walled. */
    void addWall(const EdgeVertices& near, const HexCellData* nearCell, const EdgeVertices& far,
                 const HexCellData* farCell, bool hasRiver, bool hasRoad) {
        if (farCell == nullptr) return;
        if (nearCell->flags.isWalled() == farCell->flags.isWalled()) return;
        if (nearCell->values.isUnderwater() || farCell->values.isUnderwater()) return;
        if (edgeType(nearCell->values.elevation(), farCell->values.elevation()) == HexEdgeType::Cliff) return;

        addWallSegment(near.v1, far.v1, near.v2, far.v2, false);
        if (hasRiver || hasRoad) {
            addWallCap(near.v2, far.v2);
            addWallCap(far.v4, near.v4);
        } else {
            addWallSegment(near.v2, far.v2, near.v3, far.v3, false);
            addWallSegment(near.v3, far.v3, near.v4, far.v4, false);
        }
        addWallSegment(near.v4, far.v4, near.v5, far.v5, false);
    }

    /** @brief One box-like wall segment; optionally carries a tower at its middle. */
    void addWallSegment(HexVec3 nearLeft, HexVec3 farLeft, HexVec3 nearRight, HexVec3 farRight, bool addTower) {
        const HexVec3 left        = HexMetrics::wallLerp(nearLeft, farLeft);
        const HexVec3 right       = HexMetrics::wallLerp(nearRight, farRight);
        const HexVec3 leftOffset  = HexMetrics::wallThicknessOffset(nearLeft, farLeft);
        const HexVec3 rightOffset = HexMetrics::wallThicknessOffset(nearRight, farRight);

        const HexVec3 leftLow  = left - leftOffset;
        const HexVec3 rightLow = right - rightOffset;
        HexVec3       leftTop  = leftLow;
        leftTop.y              = left.y + HexMetrics::kWallHeight;
        HexVec3 rightTop       = rightLow;
        rightTop.y             = right.y + HexMetrics::kWallHeight;

        emitEdgeQuad(out_, map_.noise(), leftLow, rightLow - leftLow, leftTop - leftLow, kWallCode);

        const HexVec3 leftInnerLow  = left + leftOffset;
        const HexVec3 rightInnerLow = right + rightOffset;
        HexVec3       leftInnerTop  = leftInnerLow;
        leftInnerTop.y              = left.y + HexMetrics::kWallHeight;
        HexVec3 rightInnerTop       = rightInnerLow;
        rightInnerTop.y             = right.y + HexMetrics::kWallHeight;

        emitEdgeQuad(out_, map_.noise(), rightInnerLow, leftInnerLow - rightInnerLow, rightInnerTop - rightInnerLow,
                     kWallCode);
        emitEdgeQuad(out_, map_.noise(), leftTop, rightTop - leftTop, leftInnerTop - leftTop, kWallCode);

        if (addTower) addTowerAt(left, right);
    }

    /** @brief Emits a short wide box straddling a wall segment. */
    void addTowerAt(HexVec3 left, HexVec3 right) {
        const HexVec3 along = right - left;
        if (dot(along, along) <= 1e-8f) return;

        const HexVec3 fwd       = normalize(along);
        const HexVec3 rightAxis = normalize(cross(fwd, HexVec3{0.f, 1.f, 0.f}));
        if (isZero(rightAxis)) return;

        const float   halfWidth  = HexMetrics::kWallThickness * kTowerHalfExtent;
        const float   halfHeight = HexMetrics::kWallThickness * kTowerHeight * 0.5f;
        const HexVec3 halfExtents{halfWidth, halfHeight, halfWidth};
        const HexVec3 centre = (left + right) * 0.5f + HexVec3{0.f, halfHeight, 0.f};
        emitBox(out_, map_.noise(), centre, rightAxis, HexVec3{0.f, 1.f, 0.f}, fwd, halfExtents, kTowerCode);
    }

    /** @brief Closes a wall end with a half-thickness quad. */
    void addWallCap(HexVec3 near, HexVec3 far) {
        const HexVec3 center    = HexMetrics::wallLerp(near, far);
        const HexVec3 thickness = HexMetrics::wallThicknessOffset(near, far);

        const HexVec3 lowA = center - thickness;
        const HexVec3 lowB = center + thickness;
        HexVec3       topA = lowA;
        topA.y             = center.y + HexMetrics::kWallHeight;
        HexVec3 topB       = lowB;
        topB.y             = center.y + HexMetrics::kWallHeight;

        // The cap is the wall's cross-section: topA -> lowA -> lowB -> topB.
        // `emitEdgeQuad` walks `c`, `c + e1`, `c + e1 + e2`, `c + e2`, so the
        // second edge has to span the far side at wall height. Using the near
        // side's offset instead (`lowB - topA`) would drag the fourth corner one
        // wall height below the base and skew the cap.
        emitEdgeQuad(out_, map_.noise(), topA, lowA - topA, topB - topA, kWallCode);
    }

    /** @brief Fills the triangular gap where a wall meets a higher cell. */
    void addWallWedge(HexVec3 near, HexVec3 far, HexVec3 point) {
        const HexVec3 center    = HexMetrics::wallLerp(near, far);
        const HexVec3 thickness = HexMetrics::wallThicknessOffset(near, far);

        HexVec3 flatPoint = point;
        flatPoint.y       = center.y;
        HexVec3 topPoint  = flatPoint;
        topPoint.y        = center.y + HexMetrics::kWallHeight;

        const HexVec3 lowA = center - thickness;
        const HexVec3 lowB = center + thickness;
        HexVec3       topA = lowA;
        topA.y             = topPoint.y;
        HexVec3 topB       = lowB;
        topB.y             = topPoint.y;

        // Each side spans from its wall centre to the apex, so the pair of edge
        // vectors has to be ordered for the face to point away from the wall.
        emitEdgeQuad(out_, map_.noise(), flatPoint, topPoint - flatPoint, lowA - flatPoint, kWallCode);
        emitEdgeQuad(out_, map_.noise(), flatPoint, lowB - flatPoint, topPoint - flatPoint, kWallCode);
        emitTriangle(out_, map_.noise(), topPoint, topB, topA, kWallCode);
    }

    /** @brief Emits the wedge or cap of the three-cell wall corner. */
    void addWallSegmentAtPivot(HexVec3 pivot, const HexCellData* pivotCell, HexVec3 left, const HexCellData* leftCell,
                               HexVec3 right, const HexCellData* rightCell) {
        if (pivotCell->values.isUnderwater()) return;

        const bool hasLeftWall = leftCell != nullptr && !leftCell->values.isUnderwater() &&
                                 edgeType(pivotCell->values.elevation(), leftCell->values.elevation()) !=
                                     HexEdgeType::Cliff;
        const bool hasRightWall = rightCell != nullptr && !rightCell->values.isUnderwater() &&
                                  edgeType(pivotCell->values.elevation(), rightCell->values.elevation()) !=
                                      HexEdgeType::Cliff;

        if (hasLeftWall && hasRightWall) {
            bool hasTower = false;
            if (leftCell->values.elevation() == rightCell->values.elevation()) {
                const HexVec3 midpoint = (pivot + left + right) * (1.f / 3.f);
                const HexHash hash     = HexHashGrid(map_.seed()).sample(midpoint);
                hasTower               = hash.e < kWallTowerThreshold;
            }
            addWallSegment(pivot, left, pivot, right, hasTower);
        } else if (hasLeftWall) {
            if (leftCell->values.elevation() < rightCell->values.elevation()) {
                addWallWedge(pivot, left, right);
            } else {
                addWallCap(pivot, left);
            }
        } else if (hasRightWall) {
            if (rightCell->values.elevation() < leftCell->values.elevation()) {
                addWallWedge(right, pivot, left);
            } else {
                addWallCap(right, pivot);
            }
        }
    }

    /** @brief Dispatches the three-cell corner onto the pivot/left/right triple. */
    void addWallCorner(HexVec3 c1, const HexCellData* cell1, HexVec3 c2, const HexCellData* cell2, HexVec3 c3,
                       const HexCellData* cell3) {
        if (cell2 == nullptr || cell3 == nullptr) return;
        const bool w1 = cell1->flags.isWalled();
        const bool w2 = cell2->flags.isWalled();
        const bool w3 = cell3->flags.isWalled();

        if (w1) {
            if (w2) {
                if (!w3) addWallSegmentAtPivot(c3, cell3, c1, cell1, c2, cell2);
            } else if (w3) {
                addWallSegmentAtPivot(c2, cell2, c3, cell3, c1, cell1);
            } else {
                addWallSegmentAtPivot(c1, cell1, c2, cell2, c3, cell3);
            }
        } else if (w2) {
            if (w3) {
                addWallSegmentAtPivot(c1, cell1, c2, cell2, c3, cell3);
            } else {
                addWallSegmentAtPivot(c2, cell2, c3, cell3, c1, cell1);
            }
        } else if (w3) {
            addWallSegmentAtPivot(c3, cell3, c1, cell1, c2, cell2);
        }
    }

    /** @brief Emits a slab spanning the two road centres across a river. */
    void addBridge(HexVec3 center, HexVec3 neighbourPosition) {
        const HexVec3 span = neighbourPosition - center;
        const float   length = std::sqrt(dot(span, span));
        if (length <= 1e-6f) return;

        const HexVec3 fwd       = span * (1.f / length);
        const HexVec3 rightAxis = normalize(cross(HexVec3{0.f, 1.f, 0.f}, fwd));
        if (isZero(rightAxis)) return;

        const float   halfWidth     = HexMetrics::innerRadius() * kBridgeHalfWidth;
        const float   halfThickness = HexMetrics::kWallHeight * kBridgeHalfThickness;
        const HexVec3 halfExtents{halfWidth, halfThickness, length * 0.5f};
        emitBox(out_, map_.noise(), center, rightAxis, HexVec3{0.f, 1.f, 0.f}, fwd, halfExtents, kBridgeCode);
    }

    /** @brief Edge frame of the solid border in `direction`, centred on `center`. */
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

/** @brief Emits the urban, farm, plant and special decorations of one chunk. */
class FeatureMesher {
public:
    explicit FeatureMesher(const HexMap& map, HexMeshData& out) noexcept
        : map_(map), out_(out), grid_(map.seed()) {}

    /** @brief Builds every cell of `chunkIndex`. */
    void run(std::int32_t chunkIndex) {
        for (std::int32_t row = 0; row < HexMetrics::kChunkSizeZ; ++row) {
            for (std::int32_t column = 0; column < HexMetrics::kChunkSizeX; ++column) {
                buildCell(map_.chunkCell(chunkIndex, column, row));
            }
        }
    }

private:
    /** @brief Emits at most one ordinary decoration and the cell's special one. */
    void buildCell(HexCoordinates coordinates) {
        if (!map_.contains(coordinates)) return;
        const HexCellData* cellData = map_.cell(coordinates);
        if (cellData == nullptr) return;
        if (cellData->values.isUnderwater()) return;

        const std::int32_t urbanLevel = cellData->values.urbanLevel();
        const std::int32_t farmLevel  = cellData->values.farmLevel();
        const std::int32_t plantLevel = cellData->values.plantLevel();
        const std::int32_t special    = cellData->values.specialIndex();
        if (urbanLevel == 0 && farmLevel == 0 && plantLevel == 0 && special == 0) return;

        const HexVec3 position = map_.cellPosition(coordinates);
        const HexHash hash     = grid_.sample(position);

        if (special == 0) {
            const int urban = pick(urbanLevel, hash.a);
            const int farm  = pick(farmLevel, hash.b);
            const int plant = pick(plantLevel, hash.c);

            // The reference compares the three collections by their hash and
            // keeps the lowest one that passed its level's thresholds.
            std::int32_t collection = -1;
            float        usedHash   = hash.a;
            if (urban >= 0) collection = 0;
            if (farm >= 0 && (collection < 0 || hash.b < usedHash)) {
                collection = 1;
                usedHash   = hash.b;
            }
            if (plant >= 0 && (collection < 0 || hash.c < usedHash)) collection = 2;

            if (collection == 0) addUrban(position, urbanLevel, hash.e);
            if (collection == 1) addFarm(position, hash.e);
            if (collection == 2) addPlant(position, hash.e);
        }

        if (special != 0) addSpecial(position, hash.e);
    }

    /**
     * @brief Reproduces the reference collection pick.
     *
     * @param level Feature level of the cell; `0` disables the collection.
     * @param hash Threshold hash of the collection.
     * @return The index of the first threshold above `hash`, or `-1` when none is.
     */
    [[nodiscard]] static int pick(std::int32_t level, float hash) noexcept {
        if (level <= 0) return -1;
        for (std::int32_t index = 0; index < kFeatureThresholdCount; ++index) {
            if (hash < featureThreshold(level - 1, index)) return static_cast<int>(index);
        }
        return -1;
    }

    /** @brief Emits a stack of one to three boxes whose size follows the urban level. */
    void addUrban(HexVec3 position, std::int32_t level, float orientation) {
        const int   boxes = level < 1 ? 1 : (level > 3 ? 3 : level);
        const float total = HexMetrics::kWallHeight * (0.75f + 0.35f * static_cast<float>(level - 1));
        const float base  = HexMetrics::innerRadius() * 0.42f;
        const float piece = total / static_cast<float>(boxes);
        // The orientation hash turns the stack so neighbouring cities do not all
        // face the same way; it is applied as the mesh's yaw about the world up axis.
        const HexVec3 right = yaw(orientation, HexVec3{1.f, 0.f, 0.f});
        const HexVec3 fwd   = yaw(orientation, HexVec3{0.f, 0.f, 1.f});

        float lift = 0.f;
        for (int index = 0; index < boxes; ++index) {
            const float   scale       = 1.f - 0.25f * static_cast<float>(index);
            const HexVec3 halfExtents{base * scale, piece * 0.5f, base * scale};
            const HexVec3 centre = position + HexVec3{0.f, lift + piece * 0.5f, 0.f};
            emitBox(out_, map_.noise(), centre, right, HexVec3{0.f, 1.f, 0.f}, fwd, halfExtents, kUrbanCode);
            lift += piece;
        }
    }

    /** @brief Emits a low flat box representing a farm. */
    void addFarm(HexVec3 position, float orientation) {
        const float   half = HexMetrics::kWallHeight * 0.12f;
        const HexVec3 right = yaw(orientation, HexVec3{1.f, 0.f, 0.f});
        const HexVec3 fwd   = yaw(orientation, HexVec3{0.f, 0.f, 1.f});
        const HexVec3 halfExtents{HexMetrics::innerRadius() * 0.5f, half, HexMetrics::innerRadius() * 0.5f};
        emitBox(out_, map_.noise(), position + HexVec3{0.f, half, 0.f}, right, HexVec3{0.f, 1.f, 0.f}, fwd,
                halfExtents, kFarmCode);
    }

    /** @brief Emits a squashed octahedron representing a plant or tree. */
    void addPlant(HexVec3 position, float orientation) {
        const float   height = HexMetrics::kWallHeight * 0.55f;
        const float   radius = HexMetrics::innerRadius() * 0.3f;
        const float   waist  = position.y + height * 0.35f;
        const HexVec3 top{position.x, position.y + height, position.z};
        const HexVec3 bottom{position.x, position.y, position.z};
        const HexVec3 first = yaw(orientation, HexVec3{radius, 0.f, 0.f});
        const HexVec3 ring[4] = {HexVec3{position.x + first.x, waist, position.z + first.z},
                                 HexVec3{position.x - first.z, waist, position.z + first.x},
                                 HexVec3{position.x - first.x, waist, position.z - first.z},
                                 HexVec3{position.x + first.z, waist, position.z - first.x}};

        for (int index = 0; index < 4; ++index) {
            const int next = (index + 1) % 4;
            emitTriangle(out_, map_.noise(), ring[index], top, ring[next], kPlantCode);
            emitTriangle(out_, map_.noise(), ring[next], bottom, ring[index], kPlantCode);
        }
    }

    /** @brief Emits a tall tapered column representing a special feature. */
    void addSpecial(HexVec3 position, float orientation) {
        const float   height = HexMetrics::kWallHeight * 1.5f;
        const float   base   = HexMetrics::innerRadius() * 0.3f;
        const float   footer = height * 0.2f;
        const float   shaft  = height * 0.55f;
        const HexVec3 right = yaw(orientation, HexVec3{1.f, 0.f, 0.f});
        const HexVec3 fwd   = yaw(orientation, HexVec3{0.f, 0.f, 1.f});

        emitBox(out_, map_.noise(), position + HexVec3{0.f, footer * 0.5f, 0.f}, right, HexVec3{0.f, 1.f, 0.f}, fwd,
                HexVec3{base, footer * 0.5f, base}, kSpecialCode);
        emitBox(out_, map_.noise(), position + HexVec3{0.f, footer + shaft * 0.5f, 0.f}, right, HexVec3{0.f, 1.f, 0.f},
                fwd, HexVec3{base * 0.6f, shaft * 0.5f, base * 0.6f}, kSpecialCode);
    }

    const HexMap& map_;
    HexMeshData&  out_;
    HexHashGrid   grid_;
};

}  // namespace

HexHash HexHashGrid::sample(HexVec3 position) const noexcept {
    const auto wrap = [](std::int32_t value) noexcept {
        std::int32_t wrapped = value % kSize;
        if (wrapped < 0) wrapped += kSize;
        return static_cast<std::uint32_t>(wrapped);
    };
    const auto grid = [](float coordinate) noexcept {
        return static_cast<std::int32_t>(std::floor(coordinate * kScale));
    };

    const std::uint32_t x = wrap(grid(position.x));
    const std::uint32_t z = wrap(grid(position.z));

    // Five independent streams of one integer hash; the channel constant mirrors
    // the lane separation used by `HexNoise`.
    const auto lane = [&](std::uint32_t channel) noexcept {
        std::uint32_t h = x * 0x9E3779B1u;
        h ^= z * 0x85EBCA77u;
        h = (h ^ (h >> 15)) * 0xC2B2AE3Du;
        h ^= (seed_ + channel * 0x9E3779B9u) * 0x27D4EB2Fu;
        h = (h ^ (h >> 13)) * 0x165667B1u;
        h ^= h >> 16;
        return static_cast<float>(h >> 8) * (1.f / 16777216.f);
    };

    HexHash hash;
    hash.a = lane(0u);
    hash.b = lane(1u);
    hash.c = lane(2u);
    hash.d = lane(3u);
    hash.e = lane(4u);
    return hash;
}

float featureThreshold(std::int32_t level, std::int32_t index) noexcept {
    // The reference table has three levels with three entries each; both indices
    // are clamped so an out-of-range level or entry can never read past it.
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    if (index < 0) index = 0;
    if (index > kFeatureThresholdCount - 1) index = kFeatureThresholdCount - 1;

    constexpr float kThresholds[3][kFeatureThresholdCount] = {
        {0.0f, 0.0f, 0.4f},
        {0.0f, 0.4f, 0.6f},
        {0.4f, 0.6f, 0.8f},
    };
    return kThresholds[static_cast<std::size_t>(level)][static_cast<std::size_t>(index)];
}

void buildWallMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out) {
    out.clear();
    if (map.empty() || chunkIndex < 0 || chunkIndex >= map.chunkCount()) {
        out.finalize();
        return;
    }

    WallMesher mesher(map, out);
    mesher.run(chunkIndex);
    out.finalize();
}

void buildFeatureMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out) {
    out.clear();
    if (map.empty() || chunkIndex < 0 || chunkIndex >= map.chunkCount()) {
        out.finalize();
        return;
    }

    FeatureMesher mesher(map, out);
    mesher.run(chunkIndex);
    out.finalize();
}

}  // namespace eve::hexmap
