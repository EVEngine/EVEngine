#include <algorithm>
#include <cmath>
#include <set>
#include "building/BuildingAssemblyInternal.h"
#include "building/BuildingDef.h"
#include "building/PlacementInternal.h"

namespace eve::building {
using namespace assembly_detail;
Value AssemblyService::admissionStamp(const PlacementWorld& w) {
    auto sequence = [](const auto& values) {
        Value::Array a;
        for (auto item : values) a.emplace_back(item);
        return Value(std::move(a));
    };
    auto channels = [&](const auto& values) {
        Value::Object a;
        for (const auto& [key, items] : values) a[key] = sequence(items);
        return Value(std::move(a));
    };
    auto levels = [&](const auto& values) {
        Value::Object a;
        for (const auto& [key, items] : values) a[std::to_string(key)] = channels(items);
        return Value(std::move(a));
    };
    Value::Object supports;
    for (const auto& [id, p] : w.buildings_) supports[std::to_string(id)] = sequence(p.supportInstanceIds);
    return Value::Object{{"cells", sequence(w.occupancy_)},
                         {"channels", channels(w.allChannels_)},
                         {"horizontal", channels(w.horizontalEdgeChannels_)},
                         {"vertical", channels(w.verticalEdgeChannels_)},
                         {"corners", channels(w.cornerChannels_)},
                         {"floors", levels(w.cellChannelsByLevel_)},
                         {"horizontalFloors", levels(w.horizontalEdgesByLevel_)},
                         {"verticalFloors", levels(w.verticalEdgesByLevel_)},
                         {"cornerFloors", levels(w.cornersByLevel_)},
                         {"supports", std::move(supports)}};
}
Result<void> AssemblyService::validateFinal(PlacementWorld& w) {
    auto grid = checkGrid(w);
    if (!grid.ok()) return grid;
    w.occupancy_.assign(size_t(w.width_) * size_t(w.height_), 0);
    w.allChannels_.clear();
    w.horizontalEdgeChannels_.clear();
    w.verticalEdgeChannels_.clear();
    w.cornerChannels_.clear();
    w.cellChannelsByLevel_.clear();
    w.horizontalEdgesByLevel_.clear();
    w.verticalEdgesByLevel_.clear();
    w.cornersByLevel_.clear();
    std::set<int> ids;
    // Install the complete final occupancy first; adjacency/support validation below sees
    // the same state regardless of member insertion order.
    for (int id : w.instanceOrder_) {
        if (!ids.insert(id).second || !w.buildings_.contains(id)) return fail<void>("Invalid instance order");
        auto&       p = w.buildings_.at(id);
        const auto* d = BuildingRegistry::find(p.buildingId);
        if (!d || p.instanceId != id || id <= 0 || d->placementKind != p.placementKind || d->channel != p.channel ||
            !std::isfinite(p.worldX) || !std::isfinite(p.worldY) || !std::isfinite(p.rotationDeg) ||
            !std::isfinite(p.elevation) || std::abs(double(p.originCellX)) > 10000000 ||
            std::abs(double(p.originCellY)) > 10000000 || std::abs(double(p.level)) > 10000000 || d->footprintW <= 0 ||
            d->footprintH <= 0 || d->footprintW > 4096 || d->footprintH > 4096)
            return fail<void>("Invalid instance or incompatible definition: " + p.buildingId);
        bool valid = true;
        auto write = [&](std::vector<int>& channel, size_t size, size_t index) {
            if (channel.size() != size) channel.assign(size, 0);
            if (index >= size || channel[index] != 0)
                valid = false;
            else
                channel[index] = id;
        };
        if (p.placementKind == "cell") {
            PlacementSystem::foreachFootprintCell(*d, p.originCellX, p.originCellY, p.rotationDeg, [&](int x, int y) {
                if (!w.inBounds(x, y))
                    valid = false;
                else
                    write(w.channelOccupancy(d->channel, p.level), size_t(w.width_) * w.height_,
                          size_t(y) * w.width_ + x);
                return valid;
            });
        } else if (p.placementKind == "edge") {
            const bool h     = p.edge.axis == EdgeAxis::Horizontal;
            const int  width = w.width_ + (h ? 0 : 1), height = w.height_ + (h ? 1 : 0);
            if (p.edge.x < 0 || p.edge.y < 0 || p.edge.x >= width || p.edge.y >= height)
                valid = false;
            else
                write(w.edgeChannels(p.edge.axis, p.level)[d->channel], size_t(width) * height,
                      size_t(p.edge.y) * width + p.edge.x);
        } else if (p.placementKind == "corner") {
            if (p.corner.x < 0 || p.corner.y < 0 || p.corner.x > w.width_ || p.corner.y > w.height_)
                valid = false;
            else
                write(w.cornerChannels(p.level)[d->channel], size_t(w.width_ + 1) * (w.height_ + 1),
                      size_t(p.corner.y) * (w.width_ + 1) + p.corner.x);
        } else if (p.placementKind == "free") {
            placement_detail::applyDefinitionFreeFootprint(p, *d, w);
        } else
            valid = false;
        if (!valid) return fail<void>("Final occupancy conflicts or exceeds world bounds", DiagnosticCode::Conflict);
    }
    if (ids.size() != w.buildings_.size()) return fail<void>("Unordered instance in world");
    for (int id : w.instanceOrder_) {
        auto&       p = w.buildings_.at(id);
        const auto& d = *BuildingRegistry::find(p.buildingId);
        std::string reason;
        bool        valid = false;
        if (p.placementKind == "cell")
            valid = PlacementSystem::canPlaceElev(&w, p.buildingId, p.originCellX, p.originCellY, p.elevation,
                                                  p.rotationDeg, id, &reason, p.level);
        else if (p.placementKind == "edge")
            valid = PlacementSystem::canPlaceEdge(&w, p.buildingId, p.edge.x, p.edge.y,
                                                  p.edge.axis == EdgeAxis::Horizontal ? "north" : "west", id, &reason,
                                                  p.level);
        else if (p.placementKind == "corner")
            valid = PlacementSystem::canPlaceCorner(&w, p.buildingId, p.corner.x, p.corner.y, id, &reason, p.level);
        else
            valid = PlacementSystem::canPlaceFree(&w, p.buildingId, p.worldX, p.worldY, id, &reason, p.level,
                                                  p.rotationDeg);
        PlacementQuery query;
        query.buildingId        = p.buildingId;
        query.cellX             = p.originCellX;
        query.cellY             = p.originCellY;
        query.level             = p.level;
        query.rotationDeg       = p.rotationDeg;
        query.excludeInstanceId = id;
        if (!valid || !PlacementSystem::checkStructuralSupport(w, d, query, &reason))
            return fail<void>("Final state rejected for " + std::to_string(id) + ": " + reason,
                              DiagnosticCode::Conflict);
        p.supportInstanceIds = PlacementSystem::collectStructuralSupports(w, d, query);
    }
    std::set<int> owned;
    for (const auto& [id, a] : w.assemblyState_->records) {
        if (id.isNil() || a.id != id || a.blueprint.isNil() || a.members.empty())
            return fail<void>("Invalid house record");
        for (const auto& [key, member] : a.members)
            if (!key || key >= a.nextPartKey || !w.buildings_.contains(member) || !owned.insert(member).second)
                return fail<void>("Invalid or multiply owned house member");
    }
    for (auto& [id, p] : w.buildings_) p.edgeCurveGroupId = {};
    for (const auto& [id, g] : w.edgeCurveGroups_) {
        if (!id || g.id.value != id || g.instanceIds.empty()) return fail<void>("Invalid curve identity");
        std::vector<BuildingInstanceSnapshot> parts;
        for (int member : g.instanceIds) {
            auto it = w.buildings_.find(member);
            if (it == w.buildings_.end() || it->second.edgeCurveGroupId || it->second.placementKind != "edge" ||
                it->second.buildingId != g.buildingId || it->second.level != g.level)
                return fail<void>("Invalid curve member");
            it->second.edgeCurveGroupId = g.id;
            parts.push_back(codec::snapshot(it->second));
        }
        auto parsed = codec::parseEdgeCurve(codec::edgeCurveValue(g, parts));
        if (!parsed.ok()) return fail<void>("Invalid curve geometry");
    }
    return Result<void>::success();
}
}  // namespace eve::building
