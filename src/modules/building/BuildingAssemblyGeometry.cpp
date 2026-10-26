#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include "building/BuildingAssemblyInternal.h"
#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "grid/GridConfig.h"

namespace eve::building::assembly_detail {
uint64_t nextEpoch() {
    static std::atomic<uint64_t> sequence{1};
    return sequence.fetch_add(1, std::memory_order_relaxed);
}
Result<void> checkGrid(const PlacementWorld& world) {
    const auto& g = world.getGrid();
    if (g.layout != grid::GridLayout::Rectangle || g.cellW != g.cellH || g.cellGapX != 0 || g.cellGapY != 0 ||
        !std::isfinite(g.cellW) || g.cellW <= 0 || !std::isfinite(g.originX) || !std::isfinite(g.originY) ||
        world.hasGridFromLayer())
        return fail<void>("House composition requires an unbound rectangular square grid without gaps");
    return Result<void>::success();
}
namespace {
std::pair<double, double> rotate(double x, double y, int q) {
    switch ((q % 4 + 4) % 4) {
        case 1: return {y, -x};
        case 2: return {-x, -y};
        case 3: return {-y, x};
        default: return {x, y};
    }
}
}  // namespace
Result<PlacedBuilding> transformPart(const PlacedBuilding& part, const PlacementWorld& world, AssemblyPose from,
                                     AssemblyPose to) {
    if (from.quarterTurns < 0 || from.quarterTurns > 3 || to.quarterTurns < 0 || to.quarterTurns > 3 ||
        !std::isfinite(from.elevation) || !std::isfinite(to.elevation))
        return fail<PlacedBuilding>("Invalid assembly pose");
    const auto* def = BuildingRegistry::find(part.buildingId);
    if (!def) return fail<PlacedBuilding>("Missing definition: " + part.buildingId);
    const int q     = (to.quarterTurns - from.quarterTurns + 4) % 4;
    auto      point = [&](double x, double y) {
        auto [rx, ry] = rotate(x - from.x, y - from.y, q);
        return std::pair{rx + to.x, ry + to.y};
    };
    // Bound arithmetic before any integer conversion, including footprint expansion.
    constexpr double limit = 10000000;
    auto             valid = [&](double v) { return std::isfinite(v) && std::abs(v) < limit; };
    PlacedBuilding   out   = part;
    const double     level = double(part.level) - from.level + to.level;
    if (!valid(level)) return fail<PlacedBuilding>("Floor coordinate overflow");
    out.level       = static_cast<int>(level);
    out.elevation   = part.elevation - from.elevation + to.elevation;
    out.rotationDeg = std::fmod(part.rotationDeg + 90.f * q + 360.f, 360.f);
    if (part.placementKind == "cell") {
        if (def->rotationMode == "hex" || (q && def->rotationMode == "none"))
            return fail<PlacedBuilding>("Definition does not support rigid quarter-turn rotation");
        double minX = limit, minY = limit;
        PlacementSystem::foreachFootprintCell(
            *def, part.originCellX, part.originCellY, part.rotationDeg, [&](int cx, int cy) {
                for (auto [x, y] : {point(cx, cy), point(double(cx) + 1, cy), point(cx, double(cy) + 1),
                                    point(double(cx) + 1, double(cy) + 1)}) {
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                }
                return true;
            });
        if (!valid(minX) || !valid(minY)) return fail<PlacedBuilding>("Cell coordinate overflow");
        out.originCellX = int(minX);
        out.originCellY = int(minY);
        world.cellToWorldPlane(out.originCellX, out.originCellY, out.worldX, out.worldY);
    } else if (part.placementKind == "edge") {
        auto a = point(part.edge.x, part.edge.y);
        auto b = point(double(part.edge.x) + (part.edge.axis == EdgeAxis::Horizontal),
                       double(part.edge.y) + (part.edge.axis == EdgeAxis::Vertical));
        if (!valid(a.first) || !valid(a.second) || !valid(b.first) || !valid(b.second))
            return fail<PlacedBuilding>("Edge coordinate overflow");
        out.edge        = {int(std::min(a.first, b.first)), int(std::min(a.second, b.second)),
                    a.second == b.second ? EdgeAxis::Horizontal : EdgeAxis::Vertical};
        out.originCellX = out.edge.x;
        out.originCellY = out.edge.y;
        out.worldX      = world.getOriginX() + float((a.first + b.first) * 0.5) * world.getCellSize();
        out.worldY      = world.getOriginY() + float((a.second + b.second) * 0.5) * world.getCellSize();
        out.rotationDeg = out.edge.axis == EdgeAxis::Horizontal ? 0.f : 90.f;
    } else if (part.placementKind == "corner") {
        auto [x, y] = point(part.corner.x, part.corner.y);
        if (!valid(x) || !valid(y)) return fail<PlacedBuilding>("Corner coordinate overflow");
        out.corner      = {int(x), int(y)};
        out.originCellX = int(x);
        out.originCellY = int(y);
        out.worldX      = world.getOriginX() + float(x) * world.getCellSize();
        out.worldY      = world.getOriginY() + float(y) * world.getCellSize();
    } else if (part.placementKind == "free") {
        out.rotationDeg = std::fmod(part.rotationDeg - 90.f * q + 720.f, 360.f);
        auto [x, y]     = point((part.worldX - world.getOriginX()) / world.getCellSize(),
                                (part.worldY - world.getOriginY()) / world.getCellSize());
        if (!valid(x) || !valid(y)) return fail<PlacedBuilding>("Free coordinate overflow");
        out.worldX      = world.getOriginX() + float(x) * world.getCellSize();
        out.worldY      = world.getOriginY() + float(y) * world.getCellSize();
        out.originCellX = int(std::floor(x));
        out.originCellY = int(std::floor(y));
    } else
        return fail<PlacedBuilding>("Unknown placement domain");
    if (!std::isfinite(out.elevation) || !std::isfinite(out.rotationDeg))
        return fail<PlacedBuilding>("Non-finite transformed pose");
    out.supportInstanceIds.clear();
    // A rigid blueprint does not transfer provider-local surface attachments.
    out.surfaceId.clear();
    out.surfaceRevision        = 0;
    out.surfaceSampleCount     = 0;
    out.surfaceMaxSlopeDegrees = 0;
    out.surfaceHeightDelta     = 0;
    out.surfaceNormalX         = 0;
    out.surfaceNormalY         = world.getGridPlaneName() == "xz" ? 1.f : 0.f;
    out.surfaceNormalZ         = world.getGridPlaneName() == "xz" ? 0.f : 1.f;
    out.surfaceTangentX        = 1;
    out.surfaceTangentY        = 0;
    out.surfaceTangentZ        = 0;
    return Result<PlacedBuilding>::success(std::move(out));
}
Value poseValue(AssemblyPose p) {
    return Value::Object{
        {"x", p.x}, {"y", p.y}, {"level", p.level}, {"quarterTurns", p.quarterTurns}, {"elevation", p.elevation}};
}
Result<AssemblyPose> parsePose(const Value& v) {
    for (const auto& key : v.keys())
        if (key != "x" && key != "y" && key != "level" && key != "quarterTurns" && key != "elevation")
            return fail<AssemblyPose>("Unknown pose field");
    AssemblyPose p;
    for (auto [key, out] :
         {std::pair{"x", &p.x}, {"y", &p.y}, {"level", &p.level}, {"quarterTurns", &p.quarterTurns}}) {
        const auto* n = v.find(key);
        if (!n || !n->isInt64() || std::abs(double(n->asInt())) >= 10000000)
            return fail<AssemblyPose>("Invalid pose integer");
        *out = int(n->asInt());
    }
    const auto* e = v.find("elevation");
    if (!e || !e->isNumeric()) return fail<AssemblyPose>("Invalid pose elevation");
    p.elevation = float(e->isInt64() ? double(e->asInt()) : e->asDouble());
    if (!std::isfinite(p.elevation) || p.quarterTurns < 0 || p.quarterTurns > 3)
        return fail<AssemblyPose>("Invalid pose");
    return Result<AssemblyPose>::success(p);
}
void removeMembership(AssemblyState& state, int member) {
    for (auto it = state.records.begin(); it != state.records.end();) {
        std::erase_if(it->second.members, [&](const auto& entry) { return entry.second == member; });
        if (it->second.members.empty()) {
            state.handles.erase(it->first);
            it = state.records.erase(it);
        } else
            ++it;
    }
}
}  // namespace eve::building::assembly_detail
