#include "building/PlacementInternal.h"

#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/HeightfieldSurface.h"
#include "building/PlacementWorld.h"
#include "building/StaticMeshSurface.h"
#include "common/Diagnostic.h"
#include "grid/GridProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace eve::building {

namespace placement_detail {

bool hasFreeEdgeOverlap(const PlacementWorld &world, const BuildingDefinition &freeDefinition,
                        const FreeFootprint &footprint, const BuildingDefinition &edgeDefinition, EdgeAddress edge) {
    if (edgeDefinition.edgeThicknessCells <= 0 ||
        std::find(freeDefinition.blockedByEdgeChannels.begin(), freeDefinition.blockedByEdgeChannels.end(),
                  edgeDefinition.channel) == freeDefinition.blockedByEdgeChannels.end())
        return false;
    float ax = 0, ay = 0, bx = 0, by = 0;
    world.cellToWorldPlane(edge.x, edge.y, ax, ay);
    world.cellToWorldPlane(edge.x + (edge.axis == EdgeAxis::Horizontal), edge.y + (edge.axis == EdgeAxis::Vertical), bx,
                           by);
    // cellToWorldPlane is a vertex projection for rectangle grids.
    FreeFootprint wall;
    wall.x               = (ax + bx) * .5f;
    wall.y               = (ay + by) * .5f;
    wall.halfWidth       = std::hypot(bx - ax, by - ay) * .5f;
    wall.halfHeight      = edgeDefinition.edgeThicknessCells * world.getCellSize() * .5f;
    wall.rotationRadians = std::atan2(by - ay, bx - ax);
    wall.vertices        = {{-wall.halfWidth, -wall.halfHeight},
                            {wall.halfWidth, -wall.halfHeight},
                            {wall.halfWidth, wall.halfHeight},
                            {-wall.halfWidth, wall.halfHeight}};
    return hasFreeFootprintOverlap(footprint, wall);
}

float wrapDeg(float deg) {
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}

int cardinalQuarter(float rotationDeg) {
    const float d = wrapDeg(rotationDeg);
    int         q = int(std::lround(d / 90.f)) % 4;
    if (q < 0) q += 4;
    return q;
}

float length3(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }


FreeFootprint definitionFreeFootprint(const BuildingDefinition &definition, const PlacementWorld &world, float x,
                                      float y, float rotationDeg) {
    FreeFootprint result;
    result.x      = x;
    result.y      = y;
    result.radius = definition.freeRadiusCells * std::min(world.getGrid().cellW, world.getGrid().cellH);
    if (!definition.freeFootprintVertices.empty()) {
        for (size_t i = 0; i < definition.freeFootprintVertices.size(); i += 2) {
            result.vertices.push_back({definition.freeFootprintVertices[i] * world.getGrid().cellW,
                                       definition.freeFootprintVertices[i + 1] * world.getGrid().cellH});
            result.halfWidth  = std::max(result.halfWidth, std::fabs(result.vertices.back().x));
            result.halfHeight = std::max(result.halfHeight, std::fabs(result.vertices.back().y));
        }
    } else if (definition.freeFootprintWidthCells > 0.f && definition.freeFootprintHeightCells > 0.f) {
        result.halfWidth  = definition.freeFootprintWidthCells * world.getGrid().cellW * 0.5f;
        result.halfHeight = definition.freeFootprintHeightCells * world.getGrid().cellH * 0.5f;
        result.vertices   = {{-result.halfWidth, -result.halfHeight},
                             {result.halfWidth, -result.halfHeight},
                             {result.halfWidth, result.halfHeight},
                             {-result.halfWidth, result.halfHeight}};
    }
    result.rotationRadians = wrapDeg(rotationDeg) * 3.14159265358979323846f / 180.f;
    return result;
}

void applyDefinitionFreeFootprint(PlacedBuilding &placed, const BuildingDefinition &definition,
                                  const PlacementWorld &world) {
    const FreeFootprint footprint = definitionFreeFootprint(definition, world, placed.worldX, placed.worldY, 0.f);
    placed.freeRadius             = footprint.radius;
    placed.freeHalfWidth          = footprint.halfWidth;
    placed.freeHalfHeight         = footprint.halfHeight;
    placed.freeFootprintVertices.clear();
    if (!definition.freeFootprintVertices.empty()) {
        placed.freeFootprintVertices.reserve(footprint.vertices.size() * 2);
        for (const auto &point : footprint.vertices) {
            placed.freeFootprintVertices.push_back(point.x);
            placed.freeFootprintVertices.push_back(point.y);
        }
    }
}

FreeFootprint placedFreeFootprint(const PlacedBuilding &placed) {
    FreeFootprint result;
    result.x               = placed.worldX;
    result.y               = placed.worldY;
    result.radius          = placed.freeRadius;
    result.halfWidth       = placed.freeHalfWidth;
    result.halfHeight      = placed.freeHalfHeight;
    result.rotationRadians = wrapDeg(placed.rotationDeg) * 3.14159265358979323846f / 180.f;
    for (size_t i = 0; i + 1 < placed.freeFootprintVertices.size(); i += 2)
        result.vertices.push_back({placed.freeFootprintVertices[i], placed.freeFootprintVertices[i + 1]});
    if (result.vertices.empty() && result.halfWidth > 0.f && result.halfHeight > 0.f)
        result.vertices = {{-result.halfWidth, -result.halfHeight},
                           {result.halfWidth, -result.halfHeight},
                           {result.halfWidth, result.halfHeight},
                           {-result.halfWidth, result.halfHeight}};
    return result;
}

std::vector<FreeFootprint::Point> worldVertices(const FreeFootprint &footprint) {
    std::vector<FreeFootprint::Point> result;
    result.reserve(footprint.vertices.size());
    const float c = std::cos(footprint.rotationRadians);
    const float s = std::sin(footprint.rotationRadians);
    for (const auto &point : footprint.vertices)
        result.push_back({footprint.x + point.x * c - point.y * s, footprint.y + point.x * s + point.y * c});
    return result;
}

bool isPointInPolygon(const FreeFootprint &polygon, float x, float y) {
    const auto vertices = worldVertices(polygon);
    float      winding  = 0.f;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto &a     = vertices[i];
        const auto &b     = vertices[(i + 1) % vertices.size()];
        const float cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        if (std::fabs(cross) <= 1e-5f) continue;
        if (winding == 0.f)
            winding = cross;
        else if (cross * winding < 0.f)
            return false;
    }
    return true;
}

bool hasFreeFootprintOverlap(const FreeFootprint &a, const FreeFootprint &b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    if (!a.isPolygon() && !b.isPolygon()) return std::hypot(dx, dy) < a.radius + b.radius;

    auto circlePolygonOverlap = [](const FreeFootprint &circle, const FreeFootprint &polygon) {
        if (isPointInPolygon(polygon, circle.x, circle.y)) return true;
        const auto  vertices      = worldVertices(polygon);
        const float radiusSquared = circle.radius * circle.radius;
        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto &a             = vertices[i];
            const auto &b             = vertices[(i + 1) % vertices.size()];
            const float edgeX         = b.x - a.x;
            const float edgeY         = b.y - a.y;
            const float lengthSquared = edgeX * edgeX + edgeY * edgeY;
            const float t =
                lengthSquared > 0.f
                    ? std::clamp(((circle.x - a.x) * edgeX + (circle.y - a.y) * edgeY) / lengthSquared, 0.f, 1.f)
                    : 0.f;
            const float closestX  = a.x + edgeX * t;
            const float closestY  = a.y + edgeY * t;
            const float distanceX = circle.x - closestX;
            const float distanceY = circle.y - closestY;
            if (distanceX * distanceX + distanceY * distanceY < radiusSquared) return true;
        }
        return false;
    };
    if (!a.isPolygon()) return circlePolygonOverlap(a, b);
    if (!b.isPolygon()) return circlePolygonOverlap(b, a);

    const auto aVertices = worldVertices(a);
    const auto bVertices = worldVertices(b);
    for (const auto *vertices : {&aVertices, &bVertices}) {
        for (size_t i = 0; i < vertices->size(); ++i) {
            const auto &p     = vertices->at(i);
            const auto &q     = vertices->at((i + 1) % vertices->size());
            const float axisX = -(q.y - p.y);
            const float axisY = q.x - p.x;
            float       aMin  = std::numeric_limits<float>::max();
            float       aMax  = std::numeric_limits<float>::lowest();
            float       bMin  = std::numeric_limits<float>::max();
            float       bMax  = std::numeric_limits<float>::lowest();
            for (const auto &vertex : aVertices) {
                const float projection = vertex.x * axisX + vertex.y * axisY;
                aMin                   = std::min(aMin, projection);
                aMax                   = std::max(aMax, projection);
            }
            for (const auto &vertex : bVertices) {
                const float projection = vertex.x * axisX + vertex.y * axisY;
                bMin                   = std::min(bMin, projection);
                bMax                   = std::max(bMax, projection);
            }
            if (aMax <= bMin || bMax <= aMin) return false;
        }
    }
    return true;
}

bool containsFreeFootprintPoint(const FreeFootprint &footprint, float x, float y) {
    const float dx = x - footprint.x;
    const float dy = y - footprint.y;
    if (!footprint.isPolygon()) return std::hypot(dx, dy) <= footprint.radius;
    return isPointInPolygon(footprint, x, y);
}

void cross3(float ax, float ay, float az, float bx, float by, float bz, float &x, float &y, float &z) {
    x = ay * bz - az * by;
    y = az * bx - ax * bz;
    z = ax * by - ay * bx;
}

SurfaceFrameStatus normalizeSurfaceFrame(PlacementSystem::PlacementHit &hit) {
    if (!std::isfinite(hit.worldX) || !std::isfinite(hit.worldY) || !std::isfinite(hit.worldZ) ||
        !std::isfinite(hit.normalX) || !std::isfinite(hit.normalY) || !std::isfinite(hit.normalZ)) {
        return SurfaceFrameStatus::Invalid;
    }
    const float normalLength = length3(hit.normalX, hit.normalY, hit.normalZ);
    if (normalLength <= 1e-5f) return SurfaceFrameStatus::Invalid;
    hit.normalX /= normalLength;
    hit.normalY /= normalLength;
    hit.normalZ /= normalLength;

    const float tangentDot = hit.tangentX * hit.normalX + hit.tangentY * hit.normalY + hit.tangentZ * hit.normalZ;
    hit.tangentX -= tangentDot * hit.normalX;
    hit.tangentY -= tangentDot * hit.normalY;
    hit.tangentZ -= tangentDot * hit.normalZ;
    float tangentLength = length3(hit.tangentX, hit.tangentY, hit.tangentZ);
    if (tangentLength <= 1e-5f) {
        if (std::fabs(hit.normalY) < 0.9f) {
            hit.tangentX = 0.f;
            hit.tangentY = 1.f;
            hit.tangentZ = 0.f;
        } else {
            hit.tangentX = 1.f;
            hit.tangentY = 0.f;
            hit.tangentZ = 0.f;
        }
        const float fallbackDot = hit.tangentX * hit.normalX + hit.tangentY * hit.normalY + hit.tangentZ * hit.normalZ;
        hit.tangentX -= fallbackDot * hit.normalX;
        hit.tangentY -= fallbackDot * hit.normalY;
        hit.tangentZ -= fallbackDot * hit.normalZ;
        tangentLength = length3(hit.tangentX, hit.tangentY, hit.tangentZ);
    }
    hit.tangentX /= tangentLength;
    hit.tangentY /= tangentLength;
    hit.tangentZ /= tangentLength;
    cross3(hit.tangentX, hit.tangentY, hit.tangentZ, hit.normalX, hit.normalY, hit.normalZ, hit.bitangentX,
           hit.bitangentY, hit.bitangentZ);
    return SurfaceFrameStatus::Normalized;
}

}  // namespace placement_detail
}  // namespace eve::building
