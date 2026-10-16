#include "hexmap/HexCoordinates.h"
#include <cmath>
#include <cstdint>
#include "hexmap/HexMap.h"
#include "hexmap/HexMapMesh.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"
#include "hexmap/HexVisibility.h"

// HexCoordinates.cpp
namespace eve::hexmap {

HexCoordinates HexCoordinates::fromWorldPosition(HexVec3 position) noexcept {
    const float diameter = HexMetrics::innerDiameter();
    if (diameter <= 0.f) return HexCoordinates{};
    const float fx     = position.x / diameter;
    const float fy     = -fx;
    const float offset = position.z / (HexMetrics::outerRadius() * 3.f);

    const float ax = fx - offset;
    const float ay = fy - offset;

    std::int32_t ix = static_cast<std::int32_t>(std::lround(ax));
    std::int32_t iy = static_cast<std::int32_t>(std::lround(ay));
    std::int32_t iz = static_cast<std::int32_t>(std::lround(-ax - ay));

    if (ix + iy + iz != 0) {
        const float dx = std::fabs(ax - static_cast<float>(ix));
        const float dy = std::fabs(ay - static_cast<float>(iy));
        const float dz = std::fabs(-ax - ay - static_cast<float>(iz));
        if (dx > dy && dx > dz)
            ix = -iy - iz;
        else if (dz > dy)
            iz = -ix - iy;
    }
    return HexCoordinates{ix, iz};
}

}  // namespace eve::hexmap

// HexFogMesh.cpp
/**
 * @file HexFogMesh.cpp
 * @brief Fog-of-war overlay mesh of one hex chunk.
 */


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
    if (map.empty() || visibility.cellCount() != map.cellCount() || chunkIndex < 0 || chunkIndex >= map.chunkCount()) {
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

// HexMapMesh.cpp
namespace eve::hexmap {

void buildChunkSurfaceMesh(const HexMap& map, std::int32_t chunkIndex, HexSurface surface, HexMeshData& out) {
    switch (surface) {
        case HexSurface::Terrain: buildTerrainMesh(map, chunkIndex, out); return;
        case HexSurface::Water: buildWaterMesh(map, chunkIndex, out); return;
        case HexSurface::River: buildRiverMesh(map, chunkIndex, out); return;
        case HexSurface::Road: buildRoadMesh(map, chunkIndex, out); return;
        case HexSurface::Fog:
            // The fog overlay also depends on the visibility counters, which a grid-only
            // helper cannot see; `HexMapModule::rebuildChunk` dispatches it to `buildFogMesh`.
            break;
        case HexSurface::Wall:
        case HexSurface::Feature:
            // Both are declared by `HexFeatures.h` and dispatched by
            // `HexMapModule::rebuildChunk`; `HexMapMesh` stays free of that header.
            break;
    }
    out.clear();
    out.finalize();
}

}  // namespace eve::hexmap

// HexMeshData.cpp
namespace eve::hexmap {

void HexMeshData::clear() noexcept {
    positions_.clear();
    normals_.clear();
    uvs_.clear();
    indices_.clear();
}

std::uint32_t HexMeshData::addVertex(HexVec3 position, float u, float v) {
    const auto index = static_cast<std::uint32_t>(positions_.size() / 3u);
    positions_.push_back(position.x);
    positions_.push_back(position.y);
    positions_.push_back(position.z);
    uvs_.push_back(u);
    uvs_.push_back(v);
    return index;
}

void HexMeshData::addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    indices_.push_back(a);
    indices_.push_back(b);
    indices_.push_back(c);
}

void HexMeshData::addQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    addTriangle(a, c, b);
    addTriangle(b, c, d);
}

void HexMeshData::finalize() noexcept {
    normals_.assign(positions_.size(), 0.f);
    for (std::size_t t = 0; t + 2 < indices_.size(); t += 3) {
        const std::uint32_t ia = indices_[t];
        const std::uint32_t ib = indices_[t + 1];
        const std::uint32_t ic = indices_[t + 2];
        const auto          at = [this](std::uint32_t i) {
            const std::size_t base = static_cast<std::size_t>(i) * 3u;
            return HexVec3{positions_[base], positions_[base + 1], positions_[base + 2]};
        };
        const HexVec3 a  = at(ia);
        const HexVec3 b  = at(ib);
        const HexVec3 c  = at(ic);
        const HexVec3 ab = b - a;
        const HexVec3 ac = c - a;
        HexVec3       n{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
        const float   len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-8f) {
            n.x /= len;
            n.y /= len;
            n.z /= len;
        } else {
            n = HexVec3{0.f, 1.f, 0.f};
        }
        for (std::uint32_t i : {ia, ib, ic}) {
            const std::size_t base = static_cast<std::size_t>(i) * 3u;
            normals_[base]         = n.x;
            normals_[base + 1]     = n.y;
            normals_[base + 2]     = n.z;
        }
    }
}

}  // namespace eve::hexmap
