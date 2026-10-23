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

using namespace placement_detail;

eve::Result<PlacementSystem::EdgePathPreview> PlacementSystem::previewEdgePath(
    PlacementWorld *world, const std::string &buildingId, const std::vector<CornerAddress> &vertices) {
    if (!world || vertices.size() < 2) {
        return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "edge path preview requires a world and at least two vertices",
            buildingId, {}, "building.edge-path"));
    }
    const BuildingDefinition *definition = BuildingRegistry::find(buildingId);
    if (!definition || definition->placementKind != "edge") {
        return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "edge path preview requires an edge building definition", buildingId,
            {}, "building.edge-path"));
    }
    EdgePathPreview preview;
    preview.buildingId = buildingId;
    preview.level      = world->getActiveLevel();
    std::unordered_set<std::string> uniqueEdges;
    for (size_t i = 1; i < vertices.size(); ++i) {
        const CornerAddress start      = vertices[i - 1];
        const CornerAddress end        = vertices[i];
        const bool          horizontal = start.y == end.y;
        const bool          vertical   = start.x == end.x;
        if (horizontal == vertical) {
            return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "each edge path segment must be non-empty and axis-aligned",
                std::to_string(i - 1), {}, "building.edge-path"));
        }
        const int length = horizontal ? std::abs(end.x - start.x) : std::abs(end.y - start.y);
        for (int step = 0; step < length; ++step) {
            EdgeAddress edge{horizontal ? std::min(start.x, end.x) + step : start.x,
                             vertical ? std::min(start.y, end.y) + step : start.y,
                             horizontal ? EdgeAxis::Horizontal : EdgeAxis::Vertical};
            const std::string key =
                std::to_string(edge.x) + ":" + std::to_string(edge.y) + ":" + (horizontal ? "h" : "v");
            if (!uniqueEdges.insert(key).second) {
                return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "edge path contains the same canonical edge more than once",
                    "edge_path_duplicate", {}, "building.edge-path"));
            }
            const char *direction = horizontal ? "north" : "west";
            std::string reason;
            if (!canPlaceEdge(world, buildingId, edge.x, edge.y, direction, 0, &reason)) {
                return eve::Result<EdgePathPreview>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "edge path preflight rejected without mutation", reason, {},
                    "building.edge-path"));
            }
            preview.edges.push_back(edge);
        }
    }
    return eve::Result<EdgePathPreview>::success(std::move(preview));
}

eve::Result<std::vector<CornerAddress>> PlacementSystem::sampleEdgeCubicBezier(
    const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions) {
    constexpr int    kMaxSubdivisions = 4096;
    constexpr size_t kMaxRasterEdges  = 65536;
    if (controlPoints.size() != 4 || subdivisions < 2 || subdivisions > kMaxSubdivisions) {
        return eve::Result<std::vector<CornerAddress>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "cubic edge curve requires four controls and 2..4096 subdivisions",
            {}, {}, "building.edge-curve"));
    }
    for (const EdgeCurvePoint &point : controlPoints) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return eve::Result<std::vector<CornerAddress>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "edge curve controls must be finite", {},
                                       {}, "building.edge-curve"));
        }
    }

    auto roundedPoint = [&](int sample) -> eve::Result<CornerAddress> {
        const double t = static_cast<double>(sample) / static_cast<double>(subdivisions);
        const double u = 1.0 - t;
        const double x = u * u * u * controlPoints[0].x + 3.0 * u * u * t * controlPoints[1].x +
                         3.0 * u * t * t * controlPoints[2].x + t * t * t * controlPoints[3].x;
        const double y = u * u * u * controlPoints[0].y + 3.0 * u * u * t * controlPoints[1].y +
                         3.0 * u * t * t * controlPoints[2].y + t * t * t * controlPoints[3].y;
        constexpr double kIntMin = static_cast<double>(std::numeric_limits<int>::min()) + 1.0;
        constexpr double kIntMax = static_cast<double>(std::numeric_limits<int>::max()) - 1.0;
        if (x < kIntMin || x > kIntMax || y < kIntMin || y > kIntMax) {
            return eve::Result<CornerAddress>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "edge curve exceeds grid coordinate range",
                                       {}, {}, "building.edge-curve"));
        }
        return eve::Result<CornerAddress>::success(
            {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))});
    };

    auto first = roundedPoint(0);
    if (!first.ok()) return eve::Result<std::vector<CornerAddress>>::failure(first.status());
    std::vector<CornerAddress> vertices;
    vertices.push_back(std::move(first).takeValue());
    for (int sample = 1; sample <= subdivisions; ++sample) {
        auto targetResult = roundedPoint(sample);
        if (!targetResult.ok()) return eve::Result<std::vector<CornerAddress>>::failure(targetResult.status());
        const CornerAddress target = std::move(targetResult).takeValue();
        CornerAddress       cursor = vertices.back();
        while (cursor != target) {
            const int remainingX = target.x - cursor.x;
            const int remainingY = target.y - cursor.y;
            if (remainingX != 0 && (remainingY == 0 || std::abs(remainingX) >= std::abs(remainingY)))
                cursor.x += remainingX > 0 ? 1 : -1;
            else
                cursor.y += remainingY > 0 ? 1 : -1;
            vertices.push_back(cursor);
            if (vertices.size() - 1 > kMaxRasterEdges) {
                return eve::Result<std::vector<CornerAddress>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "edge curve rasterization exceeds 65536 unit edges", {}, {},
                    "building.edge-curve"));
            }
        }
    }
    if (vertices.size() < 2) {
        return eve::Result<std::vector<CornerAddress>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "edge curve rasterization must cover at least one unit edge", {}, {},
            "building.edge-curve"));
    }
    return eve::Result<std::vector<CornerAddress>>::success(std::move(vertices));
}

eve::Result<PlacementSystem::EdgePathPreview> PlacementSystem::previewEdgeCubicBezier(
    PlacementWorld *world, const std::string &buildingId, const std::vector<EdgeCurvePoint> &controlPoints,
    int subdivisions) {
    auto vertices = sampleEdgeCubicBezier(controlPoints, subdivisions);
    if (!vertices.ok()) return eve::Result<EdgePathPreview>::failure(vertices.status());
    return previewEdgePath(world, buildingId, std::move(vertices).takeValue());
}

eve::Result<PlacementSystem::EdgePathPlacement> PlacementSystem::placeEdgeCubicBezier(
    PlacementWorld *world, const std::string &buildingId, const std::vector<EdgeCurvePoint> &controlPoints,
    int subdivisions) {
    auto preview = previewEdgeCubicBezier(world, buildingId, controlPoints, subdivisions);
    if (!preview.ok()) return eve::Result<EdgePathPlacement>::failure(preview.status());
    return commitEdgePath(world, std::move(preview).takeValue(), &controlPoints, subdivisions);
}

eve::Result<PlacementSystem::EdgeCurveSurface> PlacementSystem::sampleEdgeCurveSurface(
    const PlacementWorld &world, const std::string &surfaceName, const std::vector<EdgeCurvePoint> &controlPoints,
    int subdivisions) {
    auto validated = sampleEdgeCubicBezier(controlPoints, subdivisions);
    if (!validated.ok()) return eve::Result<EdgeCurveSurface>::failure(validated.status());
    if (surfaceName.empty())
        return eve::Result<EdgeCurveSurface>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                             "curve surface provider name is required",
                                                                             {}, {}, "building.edge-curve-surface"));
    float ox = 0.f, oy = 0.f, xx = 0.f, xy = 0.f, yx = 0.f, yy = 0.f;
    world.cellToWorldPlane(0, 0, ox, oy);
    world.cellToWorldPlane(1, 0, xx, xy);
    world.cellToWorldPlane(0, 1, yx, yy);
    xx -= ox;
    xy -= oy;
    yx -= ox;
    yy -= oy;
    EdgeCurveSurface result;
    result.providerName = surfaceName;
    result.samples.reserve(static_cast<size_t>(subdivisions + 1));
    for (int index = 0; index <= subdivisions; ++index) {
        const double t  = static_cast<double>(index) / subdivisions;
        const double u  = 1.0 - t;
        const double gx = u * u * u * controlPoints[0].x + 3.0 * u * u * t * controlPoints[1].x +
                          3.0 * u * t * t * controlPoints[2].x + t * t * t * controlPoints[3].x;
        const double gy = u * u * u * controlPoints[0].y + 3.0 * u * u * t * controlPoints[1].y +
                          3.0 * u * t * t * controlPoints[2].y + t * t * t * controlPoints[3].y;
        auto sampled = sampleSurface(world, surfaceName, ox + static_cast<float>(gx) * xx + static_cast<float>(gy) * yx,
                                     oy + static_cast<float>(gx) * xy + static_cast<float>(gy) * yy);
        if (!sampled.ok()) return eve::Result<EdgeCurveSurface>::failure(sampled.status());
        const PlacementHit &hit = sampled.value();
        if (index == 0) {
            result.surfaceId       = hit.surfaceId;
            result.surfaceRevision = hit.surfaceRevision;
        } else if (hit.surfaceId != result.surfaceId || hit.surfaceRevision != result.surfaceRevision) {
            return eve::Result<EdgeCurveSurface>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "curve crosses a surface identity or revision boundary", hit.surfaceId,
                {}, "building.edge-curve-surface"));
        }
        result.samples.push_back({hit.worldX, hit.worldY, hit.worldZ, hit.normalX, hit.normalY, hit.normalZ});
    }
    return eve::Result<EdgeCurveSurface>::success(std::move(result));
}

eve::Result<PlacementSystem::EdgePathPlacement> PlacementSystem::placeEdgeCubicBezierOnSurface(
    PlacementWorld *world, const std::string &buildingId, const std::vector<EdgeCurvePoint> &controlPoints,
    int subdivisions, const std::string &surfaceName) {
    if (!world)
        return eve::Result<EdgePathPlacement>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "curve surface placement requires a world", {},
                                   {}, "building.edge-curve-surface"));
    auto preview = previewEdgeCubicBezier(world, buildingId, controlPoints, subdivisions);
    if (!preview.ok()) return eve::Result<EdgePathPlacement>::failure(preview.status());
    auto surface = sampleEdgeCurveSurface(*world, surfaceName, controlPoints, subdivisions);
    if (!surface.ok()) return eve::Result<EdgePathPlacement>::failure(surface.status());
    return commitEdgePath(world, std::move(preview).takeValue(), &controlPoints, subdivisions, &surface.value());
}

eve::Result<PlacementSystem::EdgeLinePlacement> PlacementSystem::placeEdgeLine(PlacementWorld    *world,
                                                                               const std::string &buildingId,
                                                                               int startVertexX, int startVertexY,
                                                                               int endVertexX, int endVertexY) {
    auto path = placeEdgePath(world, buildingId, {{startVertexX, startVertexY}, {endVertexX, endVertexY}});
    if (!path.ok()) return eve::Result<EdgeLinePlacement>::failure(path.status());
    EdgeLinePlacement result;
    result.instanceIds = std::move(path).takeValue().instanceIds;
    return eve::Result<EdgeLinePlacement>::success(std::move(result));
}

eve::Result<PlacementSystem::EdgePathPlacement> PlacementSystem::placeEdgePath(
    PlacementWorld *world, const std::string &buildingId, const std::vector<CornerAddress> &vertices) {
    auto previewResult = previewEdgePath(world, buildingId, vertices);
    if (!previewResult.ok()) return eve::Result<EdgePathPlacement>::failure(previewResult.status());
    return commitEdgePath(world, std::move(previewResult).takeValue());
}

eve::Result<PlacementSystem::EdgePathPlacement> PlacementSystem::commitEdgePath(
    PlacementWorld *world, EdgePathPreview preview, const std::vector<EdgeCurvePoint> *curveControls,
    int curveSubdivisions, const EdgeCurveSurface *curveSurface) {
    const BuildingDefinition       *def   = BuildingRegistry::find(preview.buildingId);
    const std::vector<EdgeAddress> &edges = preview.edges;
    const EdgeCurveGroupId          curveGroupId =
        curveControls ? EdgeCurveGroupId{world->nextEdgeCurveGroupId_++} : EdgeCurveGroupId{};

    EdgePathPlacement result;
    result.instanceIds.reserve(edges.size());
    std::vector<BuildingChangeEvent> events;
    events.reserve(edges.size());
    for (const EdgeAddress &edge : edges) {
        PlacedBuilding placed;
        placed.instanceId       = nextInstanceId(*world);
        placed.buildingId       = preview.buildingId;
        placed.placementKind    = "edge";
        placed.edge             = edge;
        placed.edgeCurveGroupId = curveGroupId;
        placed.originCellX      = edge.x;
        placed.originCellY      = edge.y;
        placed.level            = preview.level;
        placed.channel          = def->channel;
        placed.tags             = def->tags;
        float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
        world->cellToWorldPlane(edge.x, edge.y, ax, ay);
        world->cellToWorldPlane(edge.x + (edge.axis == EdgeAxis::Horizontal ? 1 : 0),
                                edge.y + (edge.axis == EdgeAxis::Vertical ? 1 : 0), bx, by);
        placed.worldX      = (ax + bx) * 0.5f;
        placed.worldY      = (ay + by) * 0.5f;
        placed.rotationDeg = edge.axis == EdgeAxis::Horizontal ? 0.f : 90.f;

        auto        &channels  = world->edgeChannels(edge.axis, placed.level);
        auto        &occupancy = channels[def->channel];
        const size_t required  = edge.axis == EdgeAxis::Horizontal ? size_t(world->width_) * size_t(world->height_ + 1)
                                                                   : size_t(world->width_ + 1) * size_t(world->height_);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        const size_t index                    = edge.axis == EdgeAxis::Horizontal
                                                    ? size_t(edge.y) * size_t(world->width_) + size_t(edge.x)
                                                    : size_t(edge.y) * size_t(world->width_ + 1) + size_t(edge.x);
        occupancy[index]                      = placed.instanceId;
        world->buildings()[placed.instanceId] = placed;
        world->instanceOrder_.push_back(placed.instanceId);
        result.instanceIds.push_back(placed.instanceId);

        BuildingChangeEvent event;
        event.action      = "place";
        event.worldId     = world->getId();
        event.buildingId  = preview.buildingId;
        event.instanceId  = placed.instanceId;
        event.cellX       = edge.x;
        event.cellY       = edge.y;
        event.level       = placed.level;
        event.rotationDeg = placed.rotationDeg;
        event.worldX      = placed.worldX;
        event.worldY      = placed.worldY;
        event.channel     = placed.channel;
        events.push_back(std::move(event));
    }
    if (curveControls) {
        EdgeCurveGroup group;
        group.id            = curveGroupId;
        group.buildingId    = preview.buildingId;
        group.level         = preview.level;
        group.controlPoints = *curveControls;
        group.subdivisions  = curveSubdivisions;
        group.instanceIds   = result.instanceIds;
        if (curveSurface) {
            group.surfaceProviderName = curveSurface->providerName;
            group.surfaceId           = curveSurface->surfaceId;
            group.surfaceRevision     = curveSurface->surfaceRevision;
            group.surfaceSamples      = curveSurface->samples;
        }
        world->edgeCurveGroups_.emplace(group.id.value, std::move(group));
    }
    if (world->publishEvents_)
        for (BuildingChangeEvent &event : events) emit(std::move(event));
    return eve::Result<EdgePathPlacement>::success(std::move(result));
}

eve::Result<PlacementSystem::AreaPreview> PlacementSystem::previewArea(PlacementWorld    *world,
                                                                       const std::string &buildingId,
                                                                       const std::vector<std::pair<int, int>> &anchors,
                                                                       float rotationDeg) {
    if (!world || anchors.empty()) {
        return eve::Result<AreaPreview>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                        "area preview requires a world and anchors",
                                                                        buildingId, {}, "building.area"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def || def->placementKind == "edge") {
        return eve::Result<AreaPreview>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                        "area tools require a cell building definition",
                                                                        buildingId, {}, "building.area"));
    }
    AreaPreview preview;
    preview.buildingId  = buildingId;
    preview.level       = world->getActiveLevel();
    preview.rotationDeg = normalizeRotation(buildingId, rotationDeg);
    std::unordered_set<uint64_t> reserved;
    for (const auto &[x, y] : anchors) {
        AreaCellPreview cell{x, y, false, {}};
        if (!canPlace(world, buildingId, x, y, preview.rotationDeg, 0, &cell.reason)) {
            ++preview.rejectedCount;
            preview.cells.push_back(std::move(cell));
            continue;
        }
        bool                  overlaps = false;
        std::vector<uint64_t> footprint;
        foreachFootprintCell(*def, x, y, preview.rotationDeg, [&](int cx, int cy) {
            const uint64_t key = (uint64_t(uint32_t(cx)) << 32u) | uint32_t(cy);
            footprint.push_back(key);
            if (reserved.count(key) != 0) overlaps = true;
            return true;
        });
        if (overlaps) {
            cell.reason = "area_candidate_overlap";
            ++preview.rejectedCount;
        } else {
            cell.accepted = true;
            ++preview.acceptedCount;
            reserved.insert(footprint.begin(), footprint.end());
        }
        preview.cells.push_back(std::move(cell));
    }
    return eve::Result<AreaPreview>::success(std::move(preview));
}

eve::Result<PlacementSystem::AreaPreview> PlacementSystem::previewRectangle(PlacementWorld    *world,
                                                                            const std::string &buildingId, int minCellX,
                                                                            int minCellY, int maxCellX, int maxCellY,
                                                                            float rotationDeg) {
    if (minCellX > maxCellX) std::swap(minCellX, maxCellX);
    if (minCellY > maxCellY) std::swap(minCellY, maxCellY);
    std::vector<std::pair<int, int>> anchors;
    for (int y = minCellY; y <= maxCellY; ++y)
        for (int x = minCellX; x <= maxCellX; ++x) anchors.emplace_back(x, y);
    return previewArea(world, buildingId, anchors, rotationDeg);
}

eve::Result<PlacementSystem::AreaPreview> PlacementSystem::previewBrush(PlacementWorld    *world,
                                                                        const std::string &buildingId, int centerCellX,
                                                                        int centerCellY, int radius,
                                                                        float rotationDeg) {
    if (radius < 0) {
        return eve::Result<AreaPreview>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                        "brush radius must be non-negative",
                                                                        std::to_string(radius), {}, "building.area"));
    }
    std::vector<std::pair<int, int>> anchors;
    for (int y = centerCellY - radius; y <= centerCellY + radius; ++y)
        for (int x = centerCellX - radius; x <= centerCellX + radius; ++x)
            if ((x - centerCellX) * (x - centerCellX) + (y - centerCellY) * (y - centerCellY) <= radius * radius)
                anchors.emplace_back(x, y);
    return previewArea(world, buildingId, anchors, rotationDeg);
}

eve::Result<PlacementSystem::AreaPreview> PlacementSystem::previewRectangleOutline(PlacementWorld    *world,
                                                                                   const std::string &buildingId,
                                                                                   int minCellX, int minCellY,
                                                                                   int maxCellX, int maxCellY,
                                                                                   float rotationDeg) {
    if (minCellX > maxCellX) std::swap(minCellX, maxCellX);
    if (minCellY > maxCellY) std::swap(minCellY, maxCellY);
    std::vector<std::pair<int, int>> anchors;
    for (int x = minCellX; x <= maxCellX; ++x) anchors.emplace_back(x, minCellY);
    if (maxCellY != minCellY)
        for (int x = minCellX; x <= maxCellX; ++x) anchors.emplace_back(x, maxCellY);
    for (int y = minCellY + 1; y < maxCellY; ++y) {
        anchors.emplace_back(minCellX, y);
        if (maxCellX != minCellX) anchors.emplace_back(maxCellX, y);
    }
    std::sort(anchors.begin(), anchors.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.second == rhs.second ? lhs.first < rhs.first : lhs.second < rhs.second;
    });
    return previewArea(world, buildingId, anchors, rotationDeg);
}

eve::Result<PlacementSystem::PatternPreview> PlacementSystem::previewPattern(PlacementWorld       *world,
                                                                             const std::string    &buildingId,
                                                                             const PatternRequest &request) {
    const auto failure = [&](std::string message) {
        return eve::Result<PatternPreview>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, std::move(message), buildingId, {}, "building.pattern"));
    };
    const auto corners = [&]() -> eve::Result<std::vector<CornerAddress>> {
        std::vector<CornerAddress> result;
        result.reserve(request.points.size());
        for (const EdgeCurvePoint &point : request.points) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                std::abs(point.x - std::round(point.x)) > 0.0001f || std::abs(point.y - std::round(point.y)) > 0.0001f)
                return eve::Result<std::vector<CornerAddress>>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                           "discrete pattern points must be finite integer grid vertices", buildingId,
                                           {}, "building.pattern"));
            result.push_back({static_cast<int>(std::lround(point.x)), static_cast<int>(std::lround(point.y))});
        }
        return eve::Result<std::vector<CornerAddress>>::success(std::move(result));
    };

    PatternPreview preview;
    preview.request = request;
    switch (request.kind) {
        case PatternKind::EdgeLine:
        case PatternKind::EdgePath: {
            if ((request.kind == PatternKind::EdgeLine && request.points.size() != 2) ||
                (request.kind == PatternKind::EdgePath && request.points.size() < 2))
                return failure(request.kind == PatternKind::EdgeLine
                                   ? "edge-line pattern requires exactly two points"
                                   : "edge-path pattern requires at least two points");
            if (!request.surfaceName.empty())
                return failure("custom surfaces are currently supported only by cubic curves");
            auto vertices = corners();
            if (!vertices.ok()) return eve::Result<PatternPreview>::failure(vertices.status());
            auto edge = previewEdgePath(world, buildingId, vertices.value());
            if (!edge.ok()) return eve::Result<PatternPreview>::failure(edge.status());
            preview.edge = std::move(edge).takeValue();
            break;
        }
        case PatternKind::EdgeCubicBezier: {
            if (request.points.size() != 4) return failure("cubic edge pattern requires exactly four control points");
            auto edge = previewEdgeCubicBezier(world, buildingId, request.points, request.subdivisions);
            if (!edge.ok()) return eve::Result<PatternPreview>::failure(edge.status());
            if (!request.surfaceName.empty()) {
                auto surface =
                    sampleEdgeCurveSurface(*world, request.surfaceName, request.points, request.subdivisions);
                if (!surface.ok()) return eve::Result<PatternPreview>::failure(surface.status());
            }
            preview.edge = std::move(edge).takeValue();
            break;
        }
        case PatternKind::RectangleFill:
        case PatternKind::RectangleOutline: {
            if (request.points.size() != 2) return failure("rectangle pattern requires exactly two corner points");
            if (!request.surfaceName.empty()) return failure("area patterns do not accept a custom surface name");
            auto vertices = corners();
            if (!vertices.ok()) return eve::Result<PatternPreview>::failure(vertices.status());
            const CornerAddress &a    = vertices.value()[0];
            const CornerAddress &b    = vertices.value()[1];
            auto                 area = request.kind == PatternKind::RectangleFill
                                            ? previewRectangle(world, buildingId, a.x, a.y, b.x, b.y, request.rotationDeg)
                                            : previewRectangleOutline(world, buildingId, a.x, a.y, b.x, b.y, request.rotationDeg);
            if (!area.ok()) return eve::Result<PatternPreview>::failure(area.status());
            preview.area = std::move(area).takeValue();
            break;
        }
        case PatternKind::CircleBrush: {
            if (request.points.size() != 1) return failure("circle-brush pattern requires exactly one center point");
            if (!request.surfaceName.empty()) return failure("area patterns do not accept a custom surface name");
            auto vertices = corners();
            if (!vertices.ok()) return eve::Result<PatternPreview>::failure(vertices.status());
            auto area = previewBrush(world, buildingId, vertices.value()[0].x, vertices.value()[0].y, request.radius,
                                     request.rotationDeg);
            if (!area.ok()) return eve::Result<PatternPreview>::failure(area.status());
            preview.area = std::move(area).takeValue();
            break;
        }
    }
    return eve::Result<PatternPreview>::success(std::move(preview));
}

eve::Result<PlacementSystem::PatternPlacement> PlacementSystem::placePattern(PlacementWorld       *world,
                                                                             const std::string    &buildingId,
                                                                             const PatternRequest &request) {
    auto expanded = previewPattern(world, buildingId, request);
    if (!expanded.ok()) return eve::Result<PatternPlacement>::failure(expanded.status());
    PatternPlacement result;
    result.preview = std::move(expanded).takeValue();
    if (request.kind == PatternKind::EdgeLine || request.kind == PatternKind::EdgePath) {
        auto placed = commitEdgePath(world, result.preview.edge);
        if (!placed.ok()) return eve::Result<PatternPlacement>::failure(placed.status());
        result.instanceIds = std::move(placed).takeValue().instanceIds;
    } else if (request.kind == PatternKind::EdgeCubicBezier) {
        auto placed = [&]() -> eve::Result<EdgePathPlacement> {
            if (request.surfaceName.empty())
                return commitEdgePath(world, result.preview.edge, &result.preview.request.points, request.subdivisions);
            auto surface = sampleEdgeCurveSurface(*world, request.surfaceName, result.preview.request.points,
                                                  request.subdivisions);
            if (!surface.ok()) return eve::Result<EdgePathPlacement>::failure(surface.status());
            const EdgeCurveSurface frames = std::move(surface).takeValue();
            return commitEdgePath(world, result.preview.edge, &result.preview.request.points, request.subdivisions,
                                  &frames);
        }();
        if (!placed.ok()) return eve::Result<PatternPlacement>::failure(placed.status());
        result.instanceIds = std::move(placed).takeValue().instanceIds;
    } else {
        auto placed = commitArea(world, result.preview.area);
        if (!placed.ok()) return eve::Result<PatternPlacement>::failure(placed.status());
        result.instanceIds = std::move(placed).takeValue().instanceIds;
    }
    return eve::Result<PatternPlacement>::success(std::move(result));
}

eve::Result<PlacementSystem::AreaPlacement> PlacementSystem::commitArea(PlacementWorld *world, AreaPreview preview) {
    if (!world || preview.rejectedCount != 0 || preview.cells.empty()) {
        return eve::Result<AreaPlacement>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "area commit requires a fully accepted preview",
                                   std::to_string(preview.rejectedCount), {}, "building.area"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(preview.buildingId);
    if (!def) {
        return eve::Result<AreaPlacement>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "area definition was not found", preview.buildingId, {}, "building.area"));
    }
    AreaPlacement placement;
    placement.preview = std::move(preview);
    std::vector<BuildingChangeEvent> events;
    for (const AreaCellPreview &cell : placement.preview.cells) {
        PlacedBuilding pb;
        pb.instanceId  = nextInstanceId(*world);
        pb.buildingId  = placement.preview.buildingId;
        pb.originCellX = cell.cellX;
        pb.originCellY = cell.cellY;
        pb.level       = placement.preview.level;
        pb.rotationDeg = placement.preview.rotationDeg;
        pb.channel     = def->channel;
        pb.tags        = def->tags;
        world->cellToWorldPlane(cell.cellX, cell.cellY, pb.worldX, pb.worldY);
        PlacementQuery supportQuery;
        supportQuery.cellX       = pb.originCellX;
        supportQuery.cellY       = pb.originCellY;
        supportQuery.level       = pb.level;
        supportQuery.rotationDeg = pb.rotationDeg;
        pb.supportInstanceIds    = collectStructuralSupports(*world, *def, supportQuery);
        writeOccupancy(*world, *def, pb, pb.instanceId);
        world->buildings()[pb.instanceId] = pb;
        world->instanceOrder_.push_back(pb.instanceId);
        placement.instanceIds.push_back(pb.instanceId);
        BuildingChangeEvent event;
        event.action      = "place";
        event.worldId     = world->getId();
        event.buildingId  = pb.buildingId;
        event.instanceId  = pb.instanceId;
        event.cellX       = pb.originCellX;
        event.cellY       = pb.originCellY;
        event.rotationDeg = pb.rotationDeg;
        event.worldX      = pb.worldX;
        event.worldY      = pb.worldY;
        event.channel     = pb.channel;
        events.push_back(std::move(event));
    }
    if (world->publishEvents_)
        for (BuildingChangeEvent &event : events) emit(std::move(event));
    return eve::Result<AreaPlacement>::success(std::move(placement));
}

eve::Result<PlacementSystem::AreaPlacement> PlacementSystem::placeRectangle(PlacementWorld    *world,
                                                                            const std::string &buildingId, int minCellX,
                                                                            int minCellY, int maxCellX, int maxCellY,
                                                                            float rotationDeg) {
    auto preview = previewRectangle(world, buildingId, minCellX, minCellY, maxCellX, maxCellY, rotationDeg);
    if (!preview.ok()) return eve::Result<AreaPlacement>::failure(preview.status());
    return commitArea(world, std::move(preview).takeValue());
}

eve::Result<PlacementSystem::AreaPlacement> PlacementSystem::placeBrush(PlacementWorld    *world,
                                                                        const std::string &buildingId, int centerCellX,
                                                                        int centerCellY, int radius,
                                                                        float rotationDeg) {
    auto preview = previewBrush(world, buildingId, centerCellX, centerCellY, radius, rotationDeg);
    if (!preview.ok()) return eve::Result<AreaPlacement>::failure(preview.status());
    return commitArea(world, std::move(preview).takeValue());
}

eve::Result<PlacementSystem::AreaPlacement> PlacementSystem::placeRectangleOutline(PlacementWorld    *world,
                                                                                   const std::string &buildingId,
                                                                                   int minCellX, int minCellY,
                                                                                   int maxCellX, int maxCellY,
                                                                                   float rotationDeg) {
    auto preview = previewRectangleOutline(world, buildingId, minCellX, minCellY, maxCellX, maxCellY, rotationDeg);
    if (!preview.ok()) return eve::Result<AreaPlacement>::failure(preview.status());
    return commitArea(world, std::move(preview).takeValue());
}


}  // namespace eve::building
