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

PlacementRestoreStatus PlacementSystem::restoreExact(PlacementWorld *world, const PlacedBuilding &placed,
                                                     std::string *reason) {
    ensureBuiltins();
    if (!world || placed.instanceId <= 0 || world->hasBuilding(placed.instanceId)) {
        if (reason) *reason = "instance_id_conflict";
        return PlacementRestoreStatus::Rejected;
    }
    const BuildingDefinition *def = BuildingRegistry::find(placed.buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return PlacementRestoreStatus::Rejected;
    }
    if (def->placementKind != placed.placementKind) {
        if (reason) *reason = "placement_domain_mismatch";
        return PlacementRestoreStatus::Rejected;
    }
    if (def->placementKind == "free") {
        if (!canPlaceFree(world, placed.buildingId, placed.worldX, placed.worldY, 0, reason, placed.level,
                          placed.rotationDeg))
            return PlacementRestoreStatus::Rejected;
        PlacedBuilding restored = placed;
        restored.placementKind  = "free";
        applyDefinitionFreeFootprint(restored, *def, *world);
        grid::worldToCell(*world->grid_, restored.worldX, restored.worldY, restored.originCellX, restored.originCellY,
                          world->width_, world->height_);
        restored.supportInstanceIds.clear();
        world->buildings_[restored.instanceId] = restored;
        world->instanceOrder_.push_back(restored.instanceId);
        world->instanceCounter_ = std::max(world->instanceCounter_, restored.instanceId);
        BuildingChangeEvent event;
        event.action      = "place";
        event.worldId     = world->getId();
        event.buildingId  = restored.buildingId;
        event.instanceId  = restored.instanceId;
        event.cellX       = restored.originCellX;
        event.cellY       = restored.originCellY;
        event.level       = restored.level;
        event.rotationDeg = restored.rotationDeg;
        event.worldX      = restored.worldX;
        event.worldY      = restored.worldY;
        event.elevation   = restored.elevation;
        event.channel     = restored.channel;
        if (world->publishEvents_) emit(std::move(event));
        return PlacementRestoreStatus::Restored;
    }
    if (def->placementKind == "corner" || placed.placementKind == "corner") {
        if (def->placementKind != "corner" || placed.placementKind != "corner") {
            if (reason) *reason = "placement_domain_mismatch";
            return PlacementRestoreStatus::Rejected;
        }
        if (!canPlaceCorner(world, placed.buildingId, placed.corner.x, placed.corner.y, 0, reason, placed.level))
            return PlacementRestoreStatus::Rejected;
        PlacedBuilding restored = placed;
        restored.originCellX    = restored.corner.x;
        restored.originCellY    = restored.corner.y;
        PlacementQuery query;
        query.cellX                 = restored.corner.x;
        query.cellY                 = restored.corner.y;
        query.level                 = restored.level;
        restored.supportInstanceIds = collectStructuralSupports(*world, *def, query);
        auto        &occupancy      = world->cornerChannels(restored.level)[def->channel];
        const size_t required       = size_t(world->width_ + 1) * size_t(world->height_ + 1);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        occupancy[size_t(restored.corner.y) * size_t(world->width_ + 1) + size_t(restored.corner.x)] =
            restored.instanceId;
        world->buildings_[restored.instanceId] = restored;
        world->instanceOrder_.push_back(restored.instanceId);
        world->instanceCounter_ = std::max(world->instanceCounter_, restored.instanceId);
        BuildingChangeEvent event;
        event.action     = "place";
        event.worldId    = world->getId();
        event.buildingId = restored.buildingId;
        event.instanceId = restored.instanceId;
        event.cellX      = restored.corner.x;
        event.cellY      = restored.corner.y;
        event.level      = restored.level;
        event.worldX     = restored.worldX;
        event.worldY     = restored.worldY;
        event.elevation  = restored.elevation;
        event.channel    = restored.channel;
        if (world->publishEvents_) emit(std::move(event));
        return PlacementRestoreStatus::Restored;
    }
    if (def->placementKind == "edge" || placed.placementKind == "edge") {
        const char *direction = placed.edge.axis == EdgeAxis::Horizontal ? "north" : "west";
        if (!canPlaceEdge(world, placed.buildingId, placed.edge.x, placed.edge.y, direction, 0, reason, placed.level))
            return PlacementRestoreStatus::Rejected;
        PlacedBuilding restored = placed;
        restored.placementKind  = "edge";
        const auto curveGroup   = world->edgeCurveGroups_.find(restored.edgeCurveGroupId.value);
        if (!restored.edgeCurveGroupId || curveGroup == world->edgeCurveGroups_.end() ||
            std::find(curveGroup->second.instanceIds.begin(), curveGroup->second.instanceIds.end(),
                      restored.instanceId) == curveGroup->second.instanceIds.end())
            restored.edgeCurveGroupId = {};
        // Support links are a derived cell-domain cache and must never be trusted from a snapshot.
        restored.supportInstanceIds.clear();
        auto        &channels  = world->edgeChannels(restored.edge.axis, restored.level);
        auto        &occupancy = channels[def->channel];
        const size_t required  = restored.edge.axis == EdgeAxis::Horizontal
                                     ? size_t(world->width_) * size_t(world->height_ + 1)
                                     : size_t(world->width_ + 1) * size_t(world->height_);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        const size_t index                      = restored.edge.axis == EdgeAxis::Horizontal
                                                      ? size_t(restored.edge.y) * size_t(world->width_) + size_t(restored.edge.x)
                                                      : size_t(restored.edge.y) * size_t(world->width_ + 1) + size_t(restored.edge.x);
        occupancy[index]                        = restored.instanceId;
        world->buildings()[restored.instanceId] = restored;
        world->instanceOrder_.push_back(restored.instanceId);
        world->instanceCounter_ = std::max(world->instanceCounter_, restored.instanceId);
        BuildingChangeEvent event;
        event.action      = "place";
        event.worldId     = world->getId();
        event.buildingId  = restored.buildingId;
        event.instanceId  = restored.instanceId;
        event.cellX       = restored.edge.x;
        event.cellY       = restored.edge.y;
        event.rotationDeg = restored.rotationDeg;
        event.worldX      = restored.worldX;
        event.worldY      = restored.worldY;
        event.elevation   = restored.elevation;
        event.channel     = restored.channel;
        if (world->publishEvents_) emit(std::move(event));
        return PlacementRestoreStatus::Restored;
    }
    const float rotation = normalizeRotation(placed.buildingId, placed.rotationDeg);
    if (!canPlaceElev(world, placed.buildingId, placed.originCellX, placed.originCellY, placed.elevation, rotation, 0,
                      reason, placed.level))
        return PlacementRestoreStatus::Rejected;
    PlacedBuilding restored = placed;
    restored.rotationDeg    = rotation;
    restored.channel        = def->channel;
    if (restored.tags.empty()) restored.tags = def->tags;
    PlacementQuery supportQuery;
    supportQuery.cellX          = restored.originCellX;
    supportQuery.cellY          = restored.originCellY;
    supportQuery.level          = restored.level;
    supportQuery.rotationDeg    = restored.rotationDeg;
    restored.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);
    writeOccupancy(*world, *def, restored, restored.instanceId);
    world->buildings()[restored.instanceId] = restored;
    world->instanceOrder_.push_back(restored.instanceId);
    world->instanceCounter_ = std::max(world->instanceCounter_, restored.instanceId);

    BuildingChangeEvent event;
    event.action      = "place";
    event.worldId     = world->getId();
    event.buildingId  = restored.buildingId;
    event.instanceId  = restored.instanceId;
    event.cellX       = restored.originCellX;
    event.cellY       = restored.originCellY;
    event.rotationDeg = restored.rotationDeg;
    event.worldX      = restored.worldX;
    event.worldY      = restored.worldY;
    event.elevation   = restored.elevation;
    event.channel     = restored.channel;
    if (world->publishEvents_) emit(std::move(event));
    return PlacementRestoreStatus::Restored;
}

eve::Result<EdgeCurveGroup> PlacementSystem::restoreEdgeCurveGroupExact(PlacementWorld                    *world,
                                                                        const EdgeCurveGroup              &group,
                                                                        const std::vector<PlacedBuilding> &members) {
    if (!world || !group.id || group.controlPoints.size() != 4 || group.subdivisions < 2 || group.subdivisions > 4096 ||
        members.empty() || group.instanceIds.size() != members.size()) {
        return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "curve group restore requires a world, identity, four controls, valid subdivisions and matching members",
            {}, {}, "building.edge-curve-group.restore"));
    }
    std::unordered_set<int> memberIds;
    for (size_t index = 0; index < members.size(); ++index) {
        const PlacedBuilding &member = members[index];
        if (member.instanceId <= 0 || member.instanceId != group.instanceIds[index] || member.placementKind != "edge" ||
            member.buildingId != group.buildingId || member.level != group.level ||
            !memberIds.insert(member.instanceId).second) {
            return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "curve group members must be unique ordered edges with matching definition and level",
                std::to_string(member.instanceId), {}, "building.edge-curve-group.restore"));
        }
    }
    for (const EdgeCurveControlPoint &point : group.controlPoints) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return eve::Result<EdgeCurveGroup>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "curve group controls must be finite", {},
                                       {}, "building.edge-curve-group.restore"));
        }
    }
    if ((!group.surfaceSamples.empty() && group.surfaceSamples.size() != static_cast<size_t>(group.subdivisions + 1)) ||
        (group.surfaceSamples.empty() &&
         (!group.surfaceProviderName.empty() || !group.surfaceId.empty() || group.surfaceRevision != 0))) {
        return eve::Result<EdgeCurveGroup>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "curve group surface metadata requires exactly subdivisions plus one frames", {}, {},
                                   "building.edge-curve-group.restore"));
    }
    for (const EdgeCurveSurfaceSample &sample : group.surfaceSamples) {
        const float normalLength = std::sqrt(sample.normalX * sample.normalX + sample.normalY * sample.normalY +
                                             sample.normalZ * sample.normalZ);
        if (!std::isfinite(sample.worldX) || !std::isfinite(sample.worldY) || !std::isfinite(sample.worldZ) ||
            !std::isfinite(normalLength) || normalLength <= 1e-5f) {
            return eve::Result<EdgeCurveGroup>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                       "curve group surface frames require finite positions and non-zero normals", {},
                                       {}, "building.edge-curve-group.restore"));
        }
    }

    std::unique_ptr<PlacementWorld> candidate = world->cloneState();
    if (!candidate || candidate->edgeCurveGroups_.contains(group.id.value)) {
        return eve::Result<EdgeCurveGroup>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "curve group identity is already in use",
                                   std::to_string(group.id.value), {}, "building.edge-curve-group.restore"));
    }
    for (const PlacedBuilding &member : members) {
        std::string reason;
        if (restoreExact(candidate.get(), member, &reason) != PlacementRestoreStatus::Restored) {
            return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "curve group member restore was rejected without destination mutation",
                reason, {}, "building.edge-curve-group.restore"));
        }
    }
    for (int instanceId : group.instanceIds) candidate->buildings_.at(instanceId).edgeCurveGroupId = group.id;
    candidate->edgeCurveGroups_.emplace(group.id.value, group);
    candidate->nextEdgeCurveGroupId_ = std::max(candidate->nextEdgeCurveGroupId_, group.id.value + 1);
    world->swapState(*candidate);
    return eve::Result<EdgeCurveGroup>::success(group);
}


}  // namespace eve::building
