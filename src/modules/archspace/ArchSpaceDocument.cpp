#include "archspace/ArchSpaceDocument.h"

#include "archspace/ArchSpaceCatalog.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace eve::archspace {
namespace {

constexpr double kEps = 1e-8;

eve::Result<void> ok() { return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)); }

bool isFinite(double v) { return std::isfinite(v); }

bool isFinite(const Vec2& v) { return isFinite(v.x) && isFinite(v.z); }

bool isFinite(const Vec3& v) { return isFinite(v.x) && isFinite(v.y) && isFinite(v.z); }

double distance(const Vec2& a, const Vec2& b) {
    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    return std::sqrt(dx * dx + dz * dz);
}

double polygonArea(const std::vector<Vec2>& poly) {
    if (poly.size() < 3) return 0.0;
    double acc = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[(i + 1) % poly.size()];
        acc += a.x * b.z - b.x * a.z;
    }
    return 0.5 * acc;
}

bool polygonValid(const std::vector<Vec2>& poly) {
    if (poly.size() < 3 || poly.size() > 256) return false;
    for (const auto& p : poly)
        if (!isFinite(p)) return false;
    return std::abs(polygonArea(poly)) > kEps;
}

void appendBox(MeshBake& bake, const Vec3& a, const Vec3& b, double thickness, double height, const std::string& id) {
    Vec2         dir{b.x - a.x, b.z - a.z};
    const double len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (len <= kEps || thickness <= kEps || height <= kEps) return;
    dir.x /= len;
    dir.z /= len;
    const Vec2          n{-dir.z * thickness * 0.5, dir.x * thickness * 0.5};
    const std::uint32_t base       = static_cast<std::uint32_t>(bake.positions.size());
    const Vec3          corners[8] = {
        {a.x + n.x, a.y, a.z + n.z},          {a.x - n.x, a.y, a.z - n.z},
        {b.x - n.x, a.y, b.z - n.z},          {b.x + n.x, a.y, b.z + n.z},
        {a.x + n.x, a.y + height, a.z + n.z}, {a.x - n.x, a.y + height, a.z - n.z},
        {b.x - n.x, a.y + height, b.z - n.z}, {b.x + n.x, a.y + height, b.z + n.z},
    };
    for (const auto& c : corners) bake.positions.push_back(c);
    static constexpr std::uint32_t faces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 3, 7}, {0, 7, 4},
                                                   {1, 5, 6}, {1, 6, 2}, {0, 4, 5}, {0, 5, 1}, {3, 2, 6}, {3, 6, 7}};
    for (const auto& f : faces) {
        bake.indices.push_back(base + f[0]);
        bake.indices.push_back(base + f[1]);
        bake.indices.push_back(base + f[2]);
    }
    bake.primitiveIds.push_back(id);
}

void appendSlab(MeshBake& bake, const std::vector<Vec2>& poly, double y, double thickness, const std::string& id) {
    if (poly.size() < 3 || thickness <= kEps) return;
    const std::uint32_t base = static_cast<std::uint32_t>(bake.positions.size());
    for (const auto& p : poly) bake.positions.push_back(Vec3{p.x, y, p.z});
    for (const auto& p : poly) bake.positions.push_back(Vec3{p.x, y + thickness, p.z});
    const std::uint32_t n = static_cast<std::uint32_t>(poly.size());
    for (std::uint32_t i = 1; i + 1 < n; ++i) {
        bake.indices.push_back(base);
        bake.indices.push_back(base + i);
        bake.indices.push_back(base + i + 1);
        bake.indices.push_back(base + n);
        bake.indices.push_back(base + n + i + 1);
        bake.indices.push_back(base + n + i);
    }
    bake.primitiveIds.push_back(id);
}

}  // namespace

const Node* Document::find(const std::string& id) const {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

Node* Document::findMutable(const std::string& id) {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

bool Document::canParent(NodeKind child, const std::string& parentId) const {
    if (child == NodeKind::Site) return parentId.empty();
    const Node* parent = find(parentId);
    if (!parent) return false;
    switch (child) {
        case NodeKind::Building: return parent->kind == NodeKind::Site;
        case NodeKind::Level: return parent->kind == NodeKind::Building;
        case NodeKind::Wall:
        case NodeKind::Slab:
        case NodeKind::Zone:
        case NodeKind::Item: return parent->kind == NodeKind::Level;
        case NodeKind::Opening: return parent->kind == NodeKind::Wall;
        case NodeKind::Site: return false;
    }
    return false;
}

bool Document::validateNode(const Node& node) const {
    if (node.id.empty() || node.id.size() > 128 || node.name.size() > 256) return false;
    if (node.kind == NodeKind::Site) {
        if (!node.parentId.empty()) return false;
    } else if (!canParent(node.kind, node.parentId)) {
        return false;
    }
    switch (node.kind) {
        case NodeKind::Site:
        case NodeKind::Building: return true;
        case NodeKind::Level: return isFinite(node.elevation) && isFinite(node.height) && node.height > kEps;
        case NodeKind::Wall:
            return isFinite(node.start) && isFinite(node.end) && isFinite(node.thickness) && isFinite(node.height) &&
                   node.thickness > kEps && node.height > kEps && distance(node.start, node.end) > kEps;
        case NodeKind::Slab:
            return polygonValid(node.polygon) && isFinite(node.slabThickness) && node.slabThickness > kEps;
        case NodeKind::Zone: return polygonValid(node.polygon);
        case NodeKind::Item:
            return isFinite(node.position) && isFinite(node.yawDegrees) && node.catalogId.size() <= 256;
        case NodeKind::Opening:
            return isFinite(node.t) && node.t >= 0.0 && node.t <= 1.0 && isFinite(node.width) && node.width > kEps &&
                   isFinite(node.openingHeight) && node.openingHeight > kEps && isFinite(node.sill) && node.sill >= 0.0;
    }
    return false;
}

void Document::unlinkFromParent(const std::string& id) {
    Node* node = findMutable(id);
    if (!node || node->parentId.empty()) return;
    if (Node* parent = findMutable(node->parentId)) {
        parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), id),
                               parent->children.end());
    }
}

void Document::collectDescendants(const std::string& id, std::vector<std::string>& out) const {
    const Node* node = find(id);
    if (!node) return;
    for (const auto& child : node->children) {
        out.push_back(child);
        collectDescendants(child, out);
    }
}

eve::Result<void> Document::insert(Node node) {
    if (nodes_.find(node.id) != nodes_.end())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "ArchSpace node id already exists", "id", {}, "archspace.document"));
    if (!validateNode(node))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace node failed validation", "node", {},
                                                                 "archspace.document"));
    if (node.kind == NodeKind::Site) {
        if (!rootId_.empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                                     "ArchSpace document already has a site root",
                                                                     "root", {}, "archspace.document"));
        rootId_ = node.id;
    } else {
        Node* parent = findMutable(node.parentId);
        if (!parent)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                     "ArchSpace parent node is missing", "parentId", {},
                                                                     "archspace.document"));
        parent->children.push_back(node.id);
    }
    nodes_.emplace(node.id, std::move(node));
    return ok();
}

eve::Result<void> Document::replace(Node node) {
    Node* existing = findMutable(node.id);
    if (!existing)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "ArchSpace node is missing", "id", {}, "archspace.document"));
    node.parentId = existing->parentId;
    node.kind     = existing->kind;
    node.children = existing->children;
    if (!validateNode(node))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace node failed validation", "node", {},
                                                                 "archspace.document"));
    *existing = std::move(node);
    return ok();
}

eve::Result<void> Document::eraseCascade(const std::string& id) {
    if (!find(id))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "ArchSpace node is missing", "id", {}, "archspace.document"));
    std::vector<std::string> order;
    collectDescendants(id, order);
    std::reverse(order.begin(), order.end());
    order.push_back(id);
    for (const auto& victim : order) {
        unlinkFromParent(victim);
        nodes_.erase(victim);
    }
    if (rootId_ == id) rootId_.clear();
    return ok();
}

void Document::clear() {
    nodes_.clear();
    rootId_.clear();
}

eve::Result<void> Document::bootstrap(const std::string& siteId, const std::string& buildingId,
                                      const std::string& levelId, double levelHeight) {
    if (!rootId_.empty() || siteId.empty() || buildingId.empty() || levelId.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "ArchSpace bootstrap requires an empty document and non-empty ids", {}, {}, "archspace.document"));
    if (siteId == buildingId || siteId == levelId || buildingId == levelId)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace bootstrap ids must be unique", {}, {},
                                                                 "archspace.document"));
    Node site;
    site.id   = siteId;
    site.kind = NodeKind::Site;
    site.name = "Site";
    Node building;
    building.id       = buildingId;
    building.kind     = NodeKind::Building;
    building.parentId = siteId;
    building.name     = "Building";
    Node level;
    level.id       = levelId;
    level.kind     = NodeKind::Level;
    level.parentId = buildingId;
    level.name     = "Level 0";
    level.height   = levelHeight;
    Document candidate;
    auto     inserted = candidate.insert(std::move(site));
    if (!inserted.ok()) return inserted;
    inserted = candidate.insert(std::move(building));
    if (!inserted.ok()) return inserted;
    inserted = candidate.insert(std::move(level));
    if (!inserted.ok()) return inserted;
    *this = std::move(candidate);
    return ok();
}

eve::Result<void> Document::createRoom(const std::string& levelId, const std::string& roomId, std::string roomName,
                                       std::vector<Vec2> polygon, double wallHeight, double wallThickness,
                                       double slabThickness) {
    const Node* level = find(levelId);
    if (!level || level->kind != NodeKind::Level)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                 "ArchSpace room requires an existing level", "levelId",
                                                                 {}, "archspace.document"));
    if (roomId.empty() || !polygonValid(polygon) || wallHeight <= kEps || wallThickness <= kEps ||
        slabThickness <= kEps)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace room geometry is invalid", "polygon", {},
                                                                 "archspace.document"));
    if (roomName.empty()) roomName = roomId;

    Document candidate = *this;
    Node     zone;
    zone.id       = roomId + ".zone";
    zone.kind     = NodeKind::Zone;
    zone.parentId = levelId;
    zone.name     = roomName;
    zone.polygon  = polygon;
    Node slab;
    slab.id            = roomId + ".slab";
    slab.kind          = NodeKind::Slab;
    slab.parentId      = levelId;
    slab.name          = roomName + " floor";
    slab.polygon       = polygon;
    slab.slabThickness = slabThickness;
    auto inserted      = candidate.insert(std::move(zone));
    if (!inserted.ok()) return inserted;
    inserted = candidate.insert(std::move(slab));
    if (!inserted.ok()) return inserted;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        Node wall;
        wall.id        = roomId + ".wall." + std::to_string(i);
        wall.kind      = NodeKind::Wall;
        wall.parentId  = levelId;
        wall.name      = roomName + " wall " + std::to_string(i);
        wall.start     = polygon[i];
        wall.end       = polygon[(i + 1) % polygon.size()];
        wall.height    = wallHeight;
        wall.thickness = wallThickness;
        inserted       = candidate.insert(std::move(wall));
        if (!inserted.ok()) return inserted;
    }
    *this = std::move(candidate);
    return ok();
}

eve::Result<void> Document::createWall(const std::string& levelId, const std::string& wallId, std::string wallName,
                                       Vec2 start, Vec2 end, double height, double thickness) {
    const Node* level = find(levelId);
    if (!level || level->kind != NodeKind::Level)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                 "ArchSpace wall requires an existing level", "levelId",
                                                                 {}, "archspace.document"));
    Node wall;
    wall.id        = wallId;
    wall.kind      = NodeKind::Wall;
    wall.parentId  = levelId;
    wall.name      = wallName.empty() ? wallId : std::move(wallName);
    wall.start     = start;
    wall.end       = end;
    wall.height    = height;
    wall.thickness = thickness;
    return insert(std::move(wall));
}

eve::Result<void> Document::createOpening(const std::string& wallId, const std::string& openingId,
                                          std::string openingName, OpeningKind kind, double t, double width,
                                          double height, double sill) {
    const Node* wall = find(wallId);
    if (!wall || wall->kind != NodeKind::Wall)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                 "ArchSpace opening requires an existing wall",
                                                                 "wallId", {}, "archspace.document"));
    Node opening;
    opening.id            = openingId;
    opening.kind          = NodeKind::Opening;
    opening.parentId      = wallId;
    opening.name          = openingName.empty() ? openingId : std::move(openingName);
    opening.openingKind   = kind;
    opening.t             = t;
    opening.width         = width;
    opening.openingHeight = height;
    opening.sill          = sill;
    return insert(std::move(opening));
}

eve::Result<void> Document::placeItem(const std::string& levelId, const std::string& itemId, std::string itemName,
                                      std::string catalogId, Vec3 position, double yawDegrees) {
    const Node* level = find(levelId);
    if (!level || level->kind != NodeKind::Level)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                 "ArchSpace item requires an existing level", "levelId",
                                                                 {}, "archspace.document"));
    Node item;
    item.id         = itemId;
    item.kind       = NodeKind::Item;
    item.parentId   = levelId;
    item.name       = itemName.empty() ? itemId : std::move(itemName);
    item.catalogId  = std::move(catalogId);
    item.position   = position;
    item.yawDegrees = yawDegrees;
    return insert(std::move(item));
}

std::vector<std::string> Document::diagnostics() const {
    std::vector<std::string> out;
    if (rootId_.empty() && !nodes_.empty()) out.push_back("document has nodes but no root site");
    if (!rootId_.empty() && !find(rootId_)) out.push_back("root id is missing from node map");
    std::unordered_map<std::string, std::size_t> referenced;
    for (const auto& [id, node] : nodes_) {
        if (!validateNode(node)) out.push_back("node failed validation: " + id);
        for (const auto& child : node.children) {
            ++referenced[child];
            const Node* childNode = find(child);
            if (!childNode)
                out.push_back("missing child " + child + " under " + id);
            else if (childNode->parentId != id)
                out.push_back("child parent mismatch: " + child);
        }
        if (node.kind != NodeKind::Site) {
            const Node* parent = find(node.parentId);
            if (!parent)
                out.push_back("missing parent for " + id);
            else if (!canParent(node.kind, node.parentId))
                out.push_back("illegal parent kind for " + id);
        }
    }
    for (const auto& [id, node] : nodes_) {
        if (node.kind == NodeKind::Site) continue;
        if (referenced.find(id) == referenced.end()) out.push_back("orphaned node: " + id);
    }
    return out;
}

MeshBake Document::bakeMesh() const {
    MeshBake bake;

    for (const auto& [id, node] : nodes_) {
        if (node.kind == NodeKind::Wall) {
            const Node* level = find(node.parentId);
            const double y = level ? level->elevation : 0.0;
            std::vector<const Node*> openings;
            openings.reserve(node.children.size());
            for (const auto& childId : node.children) {
                const Node* child = find(childId);
                if (child && child->kind == NodeKind::Opening) openings.push_back(child);
            }
            appendWallWithOpenings(bake, node, y, openings);
        } else if (node.kind == NodeKind::Slab) {
            const Node* level = find(node.parentId);
            const double y = level ? level->elevation : 0.0;
            appendSlab(bake, node.polygon, y, node.slabThickness, id);
        } else if (node.kind == NodeKind::Item) {
            appendCatalogItem(bake, id, node.catalogId, node.position, node.yawDegrees);
        }
    }
    return bake;
}

}  // namespace eve::archspace
