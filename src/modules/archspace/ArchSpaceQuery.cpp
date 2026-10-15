#include "archspace/ArchSpaceQuery.h"

#include "archspace/ArchSpaceDocument.h"

#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>
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

SpaceRegion toRegion(const Node& zone) {
    SpaceRegion region;
    region.id      = zone.id;
    region.levelId = zone.parentId;
    region.name    = zone.name;
    region.polygon = zone.polygon;
    return region;
}

Vec3 openingWorldPosition(const Document& document, const Node& opening) {
    const Node* wall = document.find(opening.parentId);
    if (!wall) return {};
    return Vec3{wall->start.x + (wall->end.x - wall->start.x) * opening.t, opening.sill + opening.openingHeight * 0.5,
                wall->start.z + (wall->end.z - wall->start.z) * opening.t};
}

std::string nearestRegionId(const Document& document, const std::string& levelId, Vec2 xz) {
    std::string best;
    double      bestDist2 = 0.0;
    bool        any       = false;
    for (const auto& [id, node] : document.nodes()) {
        (void)id;
        if (node.kind != NodeKind::Zone || node.parentId != levelId || node.polygon.empty()) continue;
        double cx = 0.0, cz = 0.0;
        for (const auto& p : node.polygon) {
            cx += p.x;
            cz += p.z;
        }
        cx /= static_cast<double>(node.polygon.size());
        cz /= static_cast<double>(node.polygon.size());
        const double dx = cx - xz.x;
        const double dz = cz - xz.z;
        const double d2 = dx * dx + dz * dz;
        if (!any || d2 < bestDist2) {
            any       = true;
            bestDist2 = d2;
            best      = node.id;
        }
    }
    return best;
}

SpacePortal toPortalFromOpening(const Document& document, const Node& opening) {
    SpacePortal portal;
    portal.id          = opening.id;
    portal.name        = opening.name;
    portal.wallHosted  = true;
    portal.passageMask = opening.openingKind == OpeningKind::Door ? kPassageWalk : PassageMask{0};
    portal.position    = openingWorldPosition(document, opening);
    const Node* wall   = document.find(opening.parentId);
    portal.levelId     = wall ? wall->parentId : std::string{};
    if (wall) {
        const Vec2 mid{(wall->start.x + wall->end.x) * 0.5, (wall->start.z + wall->end.z) * 0.5};
        // Sample slightly off the wall midline toward both normals to find adjacent rooms.
        Vec2         dir{wall->end.x - wall->start.x, wall->end.z - wall->start.z};
        const double len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (len > kEps) {
            dir.x /= len;
            dir.z /= len;
            const Vec2   n{-dir.z, dir.x};
            const double offset = std::max(wall->thickness, 0.2);
            const auto a = nearestRegionId(document, portal.levelId, Vec2{mid.x + n.x * offset, mid.z + n.z * offset});
            const auto b = nearestRegionId(document, portal.levelId, Vec2{mid.x - n.x * offset, mid.z - n.z * offset});
            portal.regionAId = a;
            portal.regionBId = (b != a) ? b : std::string{};
        }
    }
    return portal;
}

SpacePortal toPortalFromNode(const Node& node) {
    SpacePortal portal;
    portal.id          = node.id;
    portal.levelId     = node.parentId;
    portal.name        = node.name;
    portal.regionAId   = node.regionAId;
    portal.regionBId   = node.regionBId;
    portal.passageMask = node.passageMask;
    portal.position    = node.portalPosition;
    portal.wallHosted  = false;
    return portal;
}

}  // namespace

SpaceQuery::SpaceQuery(const Document& document) noexcept : document_(&document) {}

TopologyKind SpaceQuery::topology() const noexcept {
    return document_ ? document_->topology() : TopologyKind::Architectural;
}

std::vector<std::string> SpaceQuery::strata() const {
    std::vector<std::string> out;
    if (!document_) return out;
    for (const auto& [id, node] : document_->nodes()) {
        if (node.kind == NodeKind::Level) out.push_back(id);
    }
    return out;
}

std::vector<SpaceRegion> SpaceQuery::regions() const {
    std::vector<SpaceRegion> out;
    if (!document_) return out;
    for (const auto& [id, node] : document_->nodes()) {
        (void)id;
        if (node.kind == NodeKind::Zone) out.push_back(toRegion(node));
    }
    return out;
}

std::optional<SpaceRegion> SpaceQuery::regionAt(Vec2 xz, const std::string& levelId) const {
    if (!document_ || !std::isfinite(xz.x) || !std::isfinite(xz.z)) return std::nullopt;
    for (const auto& [id, node] : document_->nodes()) {
        (void)id;
        if (node.kind != NodeKind::Zone || node.parentId != levelId) continue;
        if (pointInPolygon(xz, node.polygon)) return toRegion(node);
    }
    return std::nullopt;
}

std::vector<SpacePortal> SpaceQuery::portals() const {
    std::vector<SpacePortal> out;
    if (!document_) return out;
    for (const auto& [id, node] : document_->nodes()) {
        (void)id;
        if (node.kind == NodeKind::Opening)
            out.push_back(toPortalFromOpening(*document_, node));
        else if (node.kind == NodeKind::Portal)
            out.push_back(toPortalFromNode(node));
    }
    return out;
}

std::vector<SpacePortal> SpaceQuery::portalsFrom(const std::string& regionId) const {
    std::vector<SpacePortal> out;
    if (regionId.empty()) return out;
    for (auto& portal : portals()) {
        if (portal.regionAId == regionId || portal.regionBId == regionId) out.push_back(std::move(portal));
    }
    return out;
}

eve::Result<bool> SpaceQuery::regionsConnected(const std::string& regionAId, const std::string& regionBId) const {
    if (!document_)
        return eve::Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace SpaceQuery has no document", "document", {},
                                                                 "archspace.query"));
    if (regionAId.empty() || regionBId.empty())
        return eve::Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace region ids must be non-empty", "regionId",
                                                                 {}, "archspace.query"));
    if (!document_->find(regionAId) || !document_->find(regionBId))
        return eve::Result<bool>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "ArchSpace region id is unknown", "regionId", {}, "archspace.query"));
    if (regionAId == regionBId) return eve::Result<bool>::success(true);
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (const auto& portal : portals()) {
        if (portal.regionAId.empty()) continue;
        if (!portal.regionBId.empty()) {
            adj[portal.regionAId].push_back(portal.regionBId);
            adj[portal.regionBId].push_back(portal.regionAId);
        }
    }
    std::unordered_set<std::string> seen;
    std::queue<std::string>         q;
    q.push(regionAId);
    seen.insert(regionAId);
    while (!q.empty()) {
        const std::string cur = std::move(q.front());
        q.pop();
        if (cur == regionBId) return eve::Result<bool>::success(true);
        const auto it = adj.find(cur);
        if (it == adj.end()) continue;
        for (const auto& next : it->second) {
            if (seen.insert(next).second) q.push(next);
        }
    }
    return eve::Result<bool>::success(false);
}

std::vector<SpaceAnchor> SpaceQuery::anchorsIn(const std::string& regionId) const {
    std::vector<SpaceAnchor> out;
    if (!document_) return out;
    const Node* region = document_->find(regionId);
    if (!region || region->kind != NodeKind::Zone) return out;
    for (const auto& [id, node] : document_->nodes()) {
        (void)id;
        if (node.kind != NodeKind::Item || node.parentId != region->parentId) continue;
        if (!pointInPolygon(Vec2{node.position.x, node.position.z}, region->polygon)) continue;
        SpaceAnchor anchor;
        anchor.id         = node.id;
        anchor.levelId    = node.parentId;
        anchor.name       = node.name;
        anchor.catalogId  = node.catalogId;
        anchor.position   = node.position;
        anchor.yawDegrees = node.yawDegrees;
        out.push_back(std::move(anchor));
    }
    return out;
}

}  // namespace eve::archspace
