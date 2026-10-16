#include "archspace/ArchSpaceProjector.h"

#include "archspace/ArchSpaceDocument.h"
#include "archspace/ArchSpaceQuery.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::archspace {
namespace {

constexpr double kEps = 1e-8;

bool pointInPolygon(Vec2 p, const std::vector<Vec2>& poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a         = poly[i];
        const Vec2& b         = poly[j];
        const bool  intersect = ((a.z > p.z) != (b.z > p.z)) &&
                               (p.x < (b.x - a.x) * (p.z - a.z) / ((b.z - a.z) + (b.z == a.z ? kEps : 0.0)) + a.x);
        if (intersect) inside = !inside;
    }
    return inside;
}

void stampSegment(GridProjection& grid, Vec2 a, Vec2 b, GridCellKind kind) {
    if (grid.width <= 0 || grid.height <= 0 || grid.cellSize <= kEps) return;
    const double len   = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
    const int    steps = std::max(1, static_cast<int>(std::ceil(len / (grid.cellSize * 0.5))));
    for (int s = 0; s <= steps; ++s) {
        const double t  = static_cast<double>(s) / static_cast<double>(steps);
        const double x  = a.x + (b.x - a.x) * t;
        const double z  = a.z + (b.z - a.z) * t;
        const int    cx = static_cast<int>(std::floor((x - grid.originX) / grid.cellSize));
        const int    cz = static_cast<int>(std::floor((z - grid.originZ) / grid.cellSize));
        if (cx < 0 || cz < 0 || cx >= grid.width || cz >= grid.height) continue;
        const std::size_t idx = static_cast<std::size_t>(cz * grid.width + cx);
        if (kind == GridCellKind::Wall || grid.cells[idx] == GridCellKind::Empty ||
            grid.cells[idx] == GridCellKind::Floor) {
            grid.cells[idx] = kind;
        }
    }
}

}  // namespace

eve::Result<void> MeshSceneProjector::sync(const Document& document, std::uint64_t revision) {
    mesh_     = document.bakeMesh();
    revision_ = revision;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void MeshSceneProjector::clear() {
    mesh_     = {};
    revision_ = 0;
}

eve::Result<GridProjection> bakeGridProjection(const Document& document, const std::string& levelId, double cellSize,
                                               const std::string& orientation) {
    const Node* level = document.find(levelId);
    if (!level || level->kind != NodeKind::Level)
        return eve::Result<GridProjection>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "ArchSpace grid bake requires an existing level",
                                   "levelId", {}, "archspace.grid"));
    if (!std::isfinite(cellSize) || cellSize <= kEps)
        return eve::Result<GridProjection>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "ArchSpace grid cellSize must be finite and > 0", "cellSize", {}, "archspace.grid"));
    if (orientation != "orthogonal" && orientation != "isometric")
        return eve::Result<GridProjection>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "ArchSpace grid orientation must be orthogonal or isometric",
            "orientation", {}, "archspace.grid"));

    double minX = std::numeric_limits<double>::infinity();
    double minZ = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxZ = -std::numeric_limits<double>::infinity();
    bool   any  = false;
    for (const auto& [id, node] : document.nodes()) {
        (void)id;
        if (node.kind == NodeKind::Zone && node.parentId == levelId) {
            for (const auto& p : node.polygon) {
                minX = std::min(minX, p.x);
                minZ = std::min(minZ, p.z);
                maxX = std::max(maxX, p.x);
                maxZ = std::max(maxZ, p.z);
                any  = true;
            }
        } else if (node.kind == NodeKind::Wall && node.parentId == levelId) {
            minX = std::min({minX, node.start.x, node.end.x});
            minZ = std::min({minZ, node.start.z, node.end.z});
            maxX = std::max({maxX, node.start.x, node.end.x});
            maxZ = std::max({maxZ, node.start.z, node.end.z});
            any  = true;
        }
    }
    GridProjection grid;
    grid.cellSize    = cellSize;
    grid.orientation = orientation;
    if (!any) {
        grid.width  = 0;
        grid.height = 0;
        return eve::Result<GridProjection>::success(std::move(grid));
    }
    // Pad one cell so wall midlines near the AABB edge stay in-bounds.
    minX -= cellSize;
    minZ -= cellSize;
    maxX += cellSize;
    maxZ += cellSize;
    grid.originX            = minX;
    grid.originZ            = minZ;
    grid.width              = std::max(1, static_cast<int>(std::ceil((maxX - minX) / cellSize)));
    grid.height             = std::max(1, static_cast<int>(std::ceil((maxZ - minZ) / cellSize)));
    const std::size_t count = static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
    grid.cells.assign(count, GridCellKind::Empty);
    grid.regionIds.assign(count, {});

    SpaceQuery query(document);
    for (int z = 0; z < grid.height; ++z) {
        for (int x = 0; x < grid.width; ++x) {
            const Vec2 sample{grid.originX + (static_cast<double>(x) + 0.5) * cellSize,
                              grid.originZ + (static_cast<double>(z) + 0.5) * cellSize};
            const auto region = query.regionAt(sample, levelId);
            if (!region) continue;
            const std::size_t idx = static_cast<std::size_t>(z * grid.width + x);
            grid.cells[idx]       = GridCellKind::Floor;
            grid.regionIds[idx]   = region->id;
        }
    }

    for (const auto& [id, node] : document.nodes()) {
        (void)id;
        if (node.kind == NodeKind::Wall && node.parentId == levelId) {
            stampSegment(grid, node.start, node.end, GridCellKind::Wall);
        } else if (node.kind == NodeKind::Portal && node.parentId == levelId) {
            stampSegment(grid, Vec2{node.portalPosition.x, node.portalPosition.z},
                         Vec2{node.portalPosition.x, node.portalPosition.z}, GridCellKind::Portal);
        } else if (node.kind == NodeKind::Opening) {
            const Node* wall = document.find(node.parentId);
            if (!wall || wall->parentId != levelId) continue;
            const Vec2 mid{wall->start.x + (wall->end.x - wall->start.x) * node.t,
                           wall->start.z + (wall->end.z - wall->start.z) * node.t};
            stampSegment(grid, mid, mid, GridCellKind::Portal);
        }
    }

    return eve::Result<GridProjection>::success(std::move(grid));
}

}  // namespace eve::archspace
