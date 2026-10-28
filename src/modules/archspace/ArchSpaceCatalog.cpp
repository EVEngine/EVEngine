#include "archspace/ArchSpaceCatalog.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace eve::archspace {
namespace {

constexpr double kEps     = 1e-8;
constexpr double kDeg2Rad = 0.017453292519943295;

void pushBox(MeshBake& bake, const Vec3 corners[8], const std::string& id) {
    const std::uint32_t base = static_cast<std::uint32_t>(bake.positions.size());
    for (int i = 0; i < 8; ++i) bake.positions.push_back(corners[i]);
    static constexpr std::uint32_t faces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 3, 7}, {0, 7, 4},
                                                   {1, 5, 6}, {1, 6, 2}, {0, 4, 5}, {0, 5, 1}, {3, 2, 6}, {3, 6, 7}};
    for (const auto& f : faces) {
        bake.indices.push_back(base + f[0]);
        bake.indices.push_back(base + f[1]);
        bake.indices.push_back(base + f[2]);
    }
    bake.primitiveIds.push_back(id);
}

void pushOrientedBox(MeshBake& bake, Vec3 center, Vec3 size, double yawDegrees, const std::string& id) {
    if (size.x <= kEps || size.y <= kEps || size.z <= kEps) return;
    const double c        = std::cos(yawDegrees * kDeg2Rad);
    const double s        = std::sin(yawDegrees * kDeg2Rad);
    const double hx       = size.x * 0.5;
    const double hy       = size.y * 0.5;
    const double hz       = size.z * 0.5;
    const Vec3   local[8] = {{-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, -hy, hz}, {-hx, -hy, hz},
                             {-hx, hy, -hz},  {hx, hy, -hz},  {hx, hy, hz},  {-hx, hy, hz}};
    Vec3         corners[8];
    for (int i = 0; i < 8; ++i) {
        corners[i] = {center.x + local[i].x * c - local[i].z * s, center.y + local[i].y,
                      center.z + local[i].x * s + local[i].z * c};
    }
    pushBox(bake, corners, id);
}

Vec2 mix(const Vec2& a, const Vec2& b, double t) { return {a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t}; }

void pushWallBox(MeshBake& bake, Vec2 start, Vec2 end, double elevation, double thickness, double bottom, double top,
                 const std::string& id) {
    if (top <= bottom + kEps) return;
    Vec2         dir{end.x - start.x, end.z - start.z};
    const double len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (len <= kEps || thickness <= kEps) return;
    dir.x /= len;
    dir.z /= len;
    const Vec2   n{-dir.z * thickness * 0.5, dir.x * thickness * 0.5};
    const double h = top - bottom;
    const Vec3   a{start.x, elevation + bottom, start.z};
    const Vec3   b{end.x, elevation + bottom, end.z};
    const Vec3   corners[8] = {{a.x + n.x, a.y, a.z + n.z},     {a.x - n.x, a.y, a.z - n.z},
                               {b.x - n.x, a.y, b.z - n.z},     {b.x + n.x, a.y, b.z + n.z},
                               {a.x + n.x, a.y + h, a.z + n.z}, {a.x - n.x, a.y + h, a.z - n.z},
                               {b.x - n.x, a.y + h, b.z - n.z}, {b.x + n.x, a.y + h, b.z + n.z}};
    pushBox(bake, corners, id);
}

const std::unordered_map<std::string, CatalogEntry>& table() {
    static const std::unordered_map<std::string, CatalogEntry> kTable = {
        {"furniture.desk",
         {"furniture.desk",
          "Desk",
          {{{0.0, 0.725, 0.0}, {1.40, 0.05, 0.70}, ".top"},
           {{-0.60, 0.35, -0.28}, {0.08, 0.70, 0.08}, ".leg.fl"},
           {{0.60, 0.35, -0.28}, {0.08, 0.70, 0.08}, ".leg.fr"},
           {{-0.60, 0.35, 0.28}, {0.08, 0.70, 0.08}, ".leg.bl"},
           {{0.60, 0.35, 0.28}, {0.08, 0.70, 0.08}, ".leg.br"}}}},
        {"furniture.sofa",
         {"furniture.sofa",
          "Sofa",
          {{{0.0, 0.22, 0.0}, {2.00, 0.44, 0.85}, ".seat"},
           {{0.0, 0.55, -0.32}, {2.00, 0.50, 0.20}, ".back"},
           {{-0.90, 0.40, 0.05}, {0.18, 0.40, 0.75}, ".arm.l"},
           {{0.90, 0.40, 0.05}, {0.18, 0.40, 0.75}, ".arm.r"}}}},
        {"furniture.bed",
         {"furniture.bed",
          "Bed",
          {{{0.0, 0.28, 0.0}, {2.00, 0.36, 1.60}, ".mattress"},
           {{0.0, 0.55, -0.72}, {2.00, 0.55, 0.12}, ".headboard"}}}},
        {"furniture.chair",
         {"furniture.chair",
          "Chair",
          {{{0.0, 0.45, 0.0}, {0.45, 0.08, 0.45}, ".seat"},
           {{0.0, 0.70, -0.18}, {0.45, 0.45, 0.08}, ".back"},
           {{-0.16, 0.22, -0.16}, {0.06, 0.44, 0.06}, ".leg.fl"},
           {{0.16, 0.22, -0.16}, {0.06, 0.44, 0.06}, ".leg.fr"},
           {{-0.16, 0.22, 0.16}, {0.06, 0.44, 0.06}, ".leg.bl"},
           {{0.16, 0.22, 0.16}, {0.06, 0.44, 0.06}, ".leg.br"}}}},
        {"furniture.table",
         {"furniture.table",
          "Table",
          {{{0.0, 0.72, 0.0}, {1.20, 0.06, 1.20}, ".top"}, {{0.0, 0.36, 0.0}, {0.12, 0.72, 0.12}, ".pedestal"}}}},
        {"furniture.wardrobe", {"furniture.wardrobe", "Wardrobe", {{{0.0, 1.05, 0.0}, {1.20, 2.10, 0.60}, ".body"}}}},
    };
    return kTable;
}

}  // namespace

std::optional<CatalogEntry> lookupCatalog(std::string_view catalogId) {
    const auto it = table().find(std::string(catalogId));
    if (it == table().end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> listCatalogIds() {
    std::vector<std::string> ids;
    ids.reserve(table().size());
    for (const auto& [id, _] : table()) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

void appendCatalogItem(MeshBake& bake, const std::string& itemId, const std::string& catalogId, const Vec3& position,
                       double yawDegrees) {
    if (const auto entry = lookupCatalog(catalogId)) {
        const double c = std::cos(yawDegrees * kDeg2Rad);
        const double s = std::sin(yawDegrees * kDeg2Rad);
        for (const CatalogPart& part : entry->parts) {
            const Vec3 center{position.x + part.center.x * c - part.center.z * s, position.y + part.center.y,
                              position.z + part.center.x * s + part.center.z * c};
            pushOrientedBox(bake, center, part.size, yawDegrees, itemId + part.suffix);
        }
        return;
    }
    pushOrientedBox(bake, {position.x, position.y + 0.45, position.z}, {0.50, 0.90, 0.50}, yawDegrees, itemId);
}

void appendWallWithOpenings(MeshBake& bake, const Node& wall, double elevation,
                            const std::vector<const Node*>& openings) {
    const double len = std::sqrt((wall.end.x - wall.start.x) * (wall.end.x - wall.start.x) +
                                 (wall.end.z - wall.start.z) * (wall.end.z - wall.start.z));
    if (len <= kEps) return;

    struct Span {
        double      t0   = 0;
        double      t1   = 0;
        OpeningKind kind = OpeningKind::Door;
        double      sill = 0;
        double      oh   = 0;
        std::string id;
    };
    std::vector<Span> spans;
    for (const Node* child : openings) {
        if (!child || child->kind != NodeKind::Opening) continue;
        const double halfT = (child->width * 0.5) / len;
        Span         span;
        span.t0   = std::clamp(child->t - halfT, 0.0, 1.0);
        span.t1   = std::clamp(child->t + halfT, 0.0, 1.0);
        span.kind = child->openingKind;
        span.sill = std::clamp(child->sill, 0.0, wall.height);
        span.oh   = std::clamp(child->openingHeight, 0.0, std::max(0.0, wall.height - span.sill));
        span.id   = child->id;
        if (span.t1 > span.t0 + 1e-6 && span.oh > kEps) spans.push_back(span);
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.t0 < b.t0; });

    std::vector<Span> merged;
    for (const Span& span : spans) {
        if (!merged.empty() && span.t0 <= merged.back().t1 + 1e-6) {
            Span& last           = merged.back();
            last.t1              = std::max(last.t1, span.t1);
            const double lastTop = last.sill + last.oh;
            const double spanTop = span.sill + span.oh;
            last.sill            = std::min(last.sill, span.sill);
            last.oh              = std::max(lastTop, spanTop) - last.sill;
            if (span.kind == OpeningKind::Door) last.kind = OpeningKind::Door;
            last.id += "+" + span.id;
        } else {
            merged.push_back(span);
        }
    }

    double cursor = 0.0;
    auto   emit   = [&](double t0, double t1, double bottom, double top, const std::string& id) {
        pushWallBox(bake, mix(wall.start, wall.end, t0), mix(wall.start, wall.end, t1), elevation, wall.thickness,
                        bottom, top, id);
    };

    for (const Span& opening : merged) {
        emit(cursor, opening.t0, 0.0, wall.height, wall.id);
        const double head = opening.sill + opening.oh;
        if (opening.kind == OpeningKind::Door) {
            emit(opening.t0, opening.t1, head, wall.height, wall.id + ".lintel");
        } else {
            emit(opening.t0, opening.t1, 0.0, opening.sill, wall.id + ".sill");
            emit(opening.t0, opening.t1, head, wall.height, wall.id + ".head");
        }

        const Vec2 p0 = mix(wall.start, wall.end, opening.t0);
        const Vec2 p1 = mix(wall.start, wall.end, opening.t1);
        Vec2       dir{wall.end.x - wall.start.x, wall.end.z - wall.start.z};
        dir.x /= len;
        dir.z /= len;
        const double reveal = std::min(0.04, wall.thickness * 0.5);
        pushWallBox(bake, {p0.x - dir.x * reveal, p0.z - dir.z * reveal},
                    {p0.x + dir.x * reveal, p0.z + dir.z * reveal}, elevation, wall.thickness, opening.sill, head,
                    opening.id + ".reveal.jamb.l");
        pushWallBox(bake, {p1.x - dir.x * reveal, p1.z - dir.z * reveal},
                    {p1.x + dir.x * reveal, p1.z + dir.z * reveal}, elevation, wall.thickness, opening.sill, head,
                    opening.id + ".reveal.jamb.r");
        if (opening.kind == OpeningKind::Window && opening.sill > kEps) {
            pushWallBox(bake, p0, p1, elevation, wall.thickness, opening.sill - std::min(0.03, opening.sill),
                        opening.sill, opening.id + ".reveal.sill");
        }
        if (head + kEps < wall.height) {
            pushWallBox(bake, p0, p1, elevation, wall.thickness, head, head + std::min(0.03, wall.height - head),
                        opening.id + ".reveal.head");
        }
        cursor = std::max(cursor, opening.t1);
    }
    emit(cursor, 1.0, 0.0, wall.height, wall.id);
}

}  // namespace eve::archspace
