/**
 * @file HexFogMesh.cpp
 * @brief Fog-of-war overlay mesh of one hex chunk.
 */

#include "hexmap/HexMapMesh.h"

#include "hexmap/HexCoordinates.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"
#include "hexmap/HexVisibility.h"

#include <cstdint>

namespace eve::hexmap {
namespace {

/** @brief Fog shade carried in the first texture coordinate: explored, but no viewer now. */
constexpr float kFogShadeSeen = 0.f;
/** @brief Fog shade carried in the first texture coordinate: never explored. */
constexpr float kFogShadeUnknown = 1.f;
/** @brief Vertical clearance of the fog cap above the highest surface it covers. */
constexpr float kFogCapLift = 0.25f;
/** @brief How far below the lowest editable elevation the fog skirt reaches. */
constexpr float kFogSkirtDepth = 6.f;

/**
 * @brief Highest surface a cell can show: its land or its water, whichever is higher.
 *
 * A cell whose water level is above its elevation draws its water surface above
 * its ground, so the fog cap has to clear the water too.
 */
[[nodiscard]] float cellTopY(const HexMap& map, HexCoordinates coordinates) noexcept {
    const float land    = map.cellPosition(coordinates).y;
    const float surface = HexMetrics::waterSurfaceY(map.waterLevel(coordinates));
    return land > surface ? land : surface;
}

/**
 * @brief Highest surface of a cell or any of its direct neighbours.
 *
 * The blend strip between two cells rises towards the higher one, so a cap that
 * only clears its own cell would be pierced by the neighbour's half of the strip.
 */
[[nodiscard]] float neighbourhoodTopY(const HexMap& map, HexCoordinates coordinates) noexcept {
    float top = cellTopY(map, coordinates);
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        HexCoordinates neighbour{};
        if (!map.getNeighbor(coordinates, static_cast<HexDirection>(i), neighbour)) continue;
        const float candidate = cellTopY(map, neighbour);
        if (candidate > top) top = candidate;
    }
    return top;
}

/** @brief Emits one triangle whose three vertices share a shade. */
void emitTriangle(HexMeshData& out, const HexVec3& p0, const HexVec3& p1, const HexVec3& p2, float shade) {
    const auto i0 = static_cast<std::uint32_t>(out.vertexCount());
    out.addVertex(p0, shade, 0.f);
    out.addVertex(p1, shade, 0.f);
    out.addVertex(p2, shade, 0.f);
    out.addTriangle(i0, i0 + 1u, i0 + 2u);
}

/**
 * @brief Emits the closed column that hides one cell.
 *
 * A flat cap alone would leave the cliff faces along a fog boundary lit, because
 * the terrain's vertical walls rise from the neighbouring cell's surface up to
 * this one's. The column therefore also carries a skirt reaching below the
 * lowest editable elevation.
 *
 * @param map Source map.
 * @param coordinates Cell to cover; must be inside the grid.
 * @param shade Fog shade to write into the first texture coordinate.
 * @param out Destination mesh.
 */
void appendFogColumn(const HexMap& map, HexCoordinates coordinates, float shade, HexMeshData& out) {
    const HexNoise& noise  = map.noise();
    const HexVec3   ground = map.cellGroundPosition(coordinates);
    const float     top    = neighbourhoodTopY(map, coordinates) + kFogCapLift;
    const float     bottom = HexMetrics::elevationY(HexMetrics::kMinElevation) - kFogSkirtDepth;

    HexVec3 ring[kHexDirectionCount];
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        HexVec3 corner = noise.perturb(ground + HexMetrics::corner(static_cast<HexDirection>(i)));
        corner.y       = top;
        ring[i]        = corner;
    }
    HexVec3 center = noise.perturb(HexVec3{ground.x, 0.f, ground.z});
    center.y       = top;

    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        const std::int32_t next = (i + 1) % kHexDirectionCount;
        emitTriangle(out, center, ring[i], ring[next], shade);

        HexVec3 topNear = ring[i];
        HexVec3 topFar  = ring[next];
        HexVec3 botFar  = ring[next];
        HexVec3 botNear = ring[i];
        botNear.y       = bottom;
        botFar.y        = bottom;
        const auto i0   = static_cast<std::uint32_t>(out.vertexCount());
        out.addVertex(topNear, shade, 0.f);
        out.addVertex(topFar, shade, 0.f);
        out.addVertex(botFar, shade, 0.f);
        out.addVertex(botNear, shade, 0.f);
        out.addQuad(i0, i0 + 1u, i0 + 2u, i0 + 3u);
    }
}

}  // namespace

void buildFogMesh(const HexMap& map, const HexVisibility& visibility, std::int32_t chunkIndex, HexMeshData& out) {
    out.clear();
    if (map.empty() || visibility.cellCount() != map.cellCount() || chunkIndex < 0 ||
        chunkIndex >= map.chunkCount()) {
        out.finalize();
        return;
    }

    for (std::int32_t row = 0; row < HexMetrics::kChunkSizeZ; ++row) {
        for (std::int32_t column = 0; column < HexMetrics::kChunkSizeX; ++column) {
            const HexCoordinates coordinates = map.chunkCell(chunkIndex, column, row);
            if (!map.contains(coordinates)) continue;
            const std::int32_t index = map.indexOf(coordinates);
            if (index < 0) continue;

            float shade = 0.f;
            if (!map.isExplored(coordinates)) {
                shade = kFogShadeUnknown;
            } else if (!visibility.isVisible(index)) {
                shade = kFogShadeSeen;
            } else {
                // Fully visible: no overlay at all.
                continue;
            }
            appendFogColumn(map, coordinates, shade, out);
        }
    }
    out.finalize();
}

}  // namespace eve::hexmap
