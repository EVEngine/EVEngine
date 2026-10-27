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

eve::Result<PlacementEditReceipt> PlacementSystem::moveBuildingResult(PlacementWorld *world, int instanceId, int cellX,
                                                                      int cellY, float rotationDeg) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "move requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.edit"));
    }
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "move source instance was not found",
                                   std::to_string(instanceId), {}, "building.edit"));
    }

    PlacedBuilding &pb = it->second;
    if (hasStructuralDependents(*world, instanceId)) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "move would invalidate structural dependents",
                                   "support_in_use", {}, "building.structure"));
    }
    if (pb.placementKind != "cell") {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "cell move cannot target a non-cell instance",
                                   std::to_string(instanceId), {}, "building.edit"));
    }
    const float rot = (rotationDeg < 0.f) ? pb.rotationDeg : normalizeRotation(pb.buildingId, rotationDeg);
    std::string reason;
    if (!canPlaceElev(world, pb.buildingId, cellX, cellY, pb.elevation, rot, instanceId, &reason, pb.level)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "move preflight rejected without mutation", reason, {}, "building.edit"));
    }

    const BuildingDefinition *def = BuildingRegistry::find(pb.buildingId);
    if (!def) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "move definition was not found", pb.buildingId, {}, "building.edit"));
    }
    PlacementEditReceipt receipt;
    receipt.before = pb;

    BuildingChangeEvent ev;
    ev.action      = "move";
    ev.worldId     = world->getId();
    ev.buildingId  = pb.buildingId;
    ev.instanceId  = instanceId;
    ev.otherCellX  = pb.originCellX;
    ev.otherCellY  = pb.originCellY;
    ev.cellX       = cellX;
    ev.cellY       = cellY;
    ev.rotationDeg = rot;
    float px = 0.f, py = 0.f;
    world->cellToWorldPlane(cellX, cellY, px, py);
    ev.worldX    = px;
    ev.worldY    = py;
    ev.elevation = pb.elevation;
    ev.channel   = pb.channel;

    clearOccupancy(*world, instanceId);
    pb.originCellX = cellX;
    pb.originCellY = cellY;
    world->cellToWorldPlane(cellX, cellY, pb.worldX, pb.worldY);
    pb.rotationDeg = rot;
    PlacementQuery supportQuery;
    supportQuery.cellX       = pb.originCellX;
    supportQuery.cellY       = pb.originCellY;
    supportQuery.level       = pb.level;
    supportQuery.rotationDeg = pb.rotationDeg;
    pb.supportInstanceIds    = collectStructuralSupports(*world, *def, supportQuery);
    writeOccupancy(*world, *def, pb, instanceId);
    receipt.after = pb;
    if (world->publishEvents_) emit(std::move(ev));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt> PlacementSystem::moveEdgeResult(PlacementWorld *world, int instanceId, int cellX,
                                                                  int cellY, const std::string &direction) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "edge move requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.edit"));
    }
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "edge move source instance was not found",
                                   std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding &pb = it->second;
    if (pb.placementKind != "edge") {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "edge move requires an edge instance",
                                   std::to_string(instanceId), {}, "building.edit"));
    }
    if (hasStructuralDependents(*world, instanceId)) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "edge move would invalidate structural dependents",
                                   "support_in_use", {}, "building.structure"));
    }
    std::string reason;
    if (!canPlaceEdge(world, pb.buildingId, cellX, cellY, direction, instanceId, &reason, pb.level)) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "edge move preflight rejected without mutation",
                                   reason, {}, "building.edit"));
    }
    auto addressResult = canonicalEdge(cellX, cellY, direction);
    if (!addressResult.ok()) return eve::Result<PlacementEditReceipt>::failure(addressResult.status());
    const EdgeAddress         target = std::move(addressResult).takeValue();
    const BuildingDefinition *def    = BuildingRegistry::find(pb.buildingId);
    if (!def) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "edge move definition was not found", pb.buildingId, {}, "building.edit"));
    }

    PlacementEditReceipt receipt;
    receipt.before = pb;
    BuildingChangeEvent event;
    event.action     = "move";
    event.worldId    = world->getId();
    event.buildingId = pb.buildingId;
    event.instanceId = instanceId;
    event.otherCellX = pb.edge.x;
    event.otherCellY = pb.edge.y;
    event.cellX      = target.x;
    event.cellY      = target.y;
    event.channel    = pb.channel;

    dissolveEdgeCurveGroup(world, pb.edgeCurveGroupId);
    clearOccupancy(*world, instanceId);
    pb.edge        = target;
    pb.originCellX = target.x;
    pb.originCellY = target.y;
    float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
    world->cellToWorldPlane(target.x, target.y, ax, ay);
    world->cellToWorldPlane(target.x + (target.axis == EdgeAxis::Horizontal ? 1 : 0),
                            target.y + (target.axis == EdgeAxis::Vertical ? 1 : 0), bx, by);
    pb.worldX              = (ax + bx) * 0.5f;
    pb.worldY              = (ay + by) * 0.5f;
    pb.rotationDeg         = target.axis == EdgeAxis::Horizontal ? 0.f : 90.f;
    auto        &channels  = world->edgeChannels(target.axis, pb.level);
    auto        &occupancy = channels[def->channel];
    const size_t required  = target.axis == EdgeAxis::Horizontal ? size_t(world->width_) * size_t(world->height_ + 1)
                                                                 : size_t(world->width_ + 1) * size_t(world->height_);
    if (occupancy.size() != required) occupancy.assign(required, 0);
    const size_t index = target.axis == EdgeAxis::Horizontal
                             ? size_t(target.y) * size_t(world->width_) + size_t(target.x)
                             : size_t(target.y) * size_t(world->width_ + 1) + size_t(target.x);
    occupancy[index]   = instanceId;
    event.rotationDeg  = pb.rotationDeg;
    event.worldX       = pb.worldX;
    event.worldY       = pb.worldY;
    event.elevation    = pb.elevation;
    receipt.after      = pb;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt> PlacementSystem::moveCornerResult(PlacementWorld *world, int instanceId, int vertexX,
                                                                    int vertexY) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "corner move requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.edit"));
    }
    auto found = world->buildings_.find(instanceId);
    if (found == world->buildings_.end()) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "corner move source instance was not found",
                                   std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding &placed = found->second;
    if (placed.placementKind != "corner") {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "corner move requires a corner instance",
                                   std::to_string(instanceId), {}, "building.edit"));
    }
    if (hasStructuralDependents(*world, instanceId)) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "corner move would invalidate structural dependents",
                                   "support_in_use", {}, "building.structure"));
    }
    std::string reason;
    if (!canPlaceCorner(world, placed.buildingId, vertexX, vertexY, instanceId, &reason, placed.level)) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "corner move preflight rejected without mutation",
                                   reason, {}, "building.edit"));
    }
    const BuildingDefinition *definition = BuildingRegistry::find(placed.buildingId);
    if (!definition) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "corner move definition was not found",
                                   placed.buildingId, {}, "building.edit"));
    }

    PlacementEditReceipt receipt;
    receipt.before = placed;
    clearOccupancy(*world, instanceId);
    placed.corner      = CornerAddress{vertexX, vertexY};
    placed.originCellX = vertexX;
    placed.originCellY = vertexY;
    world->cellToWorldPlane(vertexX, vertexY, placed.worldX, placed.worldY);
    PlacementQuery query;
    query.cellX               = vertexX;
    query.cellY               = vertexY;
    query.level               = placed.level;
    placed.supportInstanceIds = collectStructuralSupports(*world, *definition, query);
    auto        &occupancy    = world->cornerChannels(placed.level)[definition->channel];
    const size_t required     = size_t(world->width_ + 1) * size_t(world->height_ + 1);
    if (occupancy.size() != required) occupancy.assign(required, 0);
    occupancy[size_t(vertexY) * size_t(world->width_ + 1) + size_t(vertexX)] = instanceId;
    receipt.after                                                            = placed;

    BuildingChangeEvent event;
    event.action     = "move";
    event.worldId    = world->getId();
    event.buildingId = placed.buildingId;
    event.instanceId = instanceId;
    event.otherCellX = receipt.before.corner.x;
    event.otherCellY = receipt.before.corner.y;
    event.cellX      = vertexX;
    event.cellY      = vertexY;
    event.level      = placed.level;
    event.worldX     = placed.worldX;
    event.worldY     = placed.worldY;
    event.elevation  = placed.elevation;
    event.channel    = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt> PlacementSystem::moveFreeResult(PlacementWorld *world, int instanceId, float worldX,
                                                                  float worldY, float elevation, float rotationDeg) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "free move requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.edit"));
    }
    auto found = world->buildings_.find(instanceId);
    if (found == world->buildings_.end() || found->second.placementKind != "free") {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "free move source instance was not found",
                                   std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding &placed = found->second;
    std::string     reason;
    if (!canPlaceFree(world, placed.buildingId, worldX, worldY, instanceId, &reason, placed.level, rotationDeg)) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "free move preflight rejected without mutation",
                                   reason, {}, "building.edit"));
    }
    PlacementEditReceipt receipt;
    receipt.before     = placed;
    placed.worldX      = worldX;
    placed.worldY      = worldY;
    placed.elevation   = elevation;
    placed.rotationDeg = normalizeRotation(placed.buildingId, rotationDeg);
    grid::worldToCell(*world->grid_, worldX, worldY, placed.originCellX, placed.originCellY, world->width_,
                      world->height_);
    receipt.after = placed;
    BuildingChangeEvent event;
    event.action      = "move";
    event.worldId     = world->getId();
    event.buildingId  = placed.buildingId;
    event.instanceId  = instanceId;
    event.cellX       = placed.originCellX;
    event.cellY       = placed.originCellY;
    event.level       = placed.level;
    event.rotationDeg = placed.rotationDeg;
    event.worldX      = placed.worldX;
    event.worldY      = placed.worldY;
    event.elevation   = placed.elevation;
    event.channel     = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt> PlacementSystem::moveFreeSurfaceResult(PlacementWorld *world, int instanceId,
                                                                         const SurfacePatch &patch, float rotationDeg) {
    if (!world || instanceId <= 0) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "free surface move requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.edit"));
    }
    auto found = world->buildings_.find(instanceId);
    if (found == world->buildings_.end() || found->second.placementKind != "free") {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "free surface move source instance was not found",
                                   std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding           &placed = found->second;
    const BuildingDefinition *def    = BuildingRegistry::find(placed.buildingId);
    if (!def) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "free surface move definition was not found",
                                   placed.buildingId, {}, "building.edit"));
    }
    const float planeX    = patch.anchor.worldX;
    const float planeY    = world->getGrid().plane == grid::GridPlane::XZ ? patch.anchor.worldZ : patch.anchor.worldY;
    const float elevation = world->getGrid().plane == grid::GridPlane::XZ ? patch.anchor.worldY : patch.anchor.worldZ;
    std::string reason;
    if (!canPlaceFree(world, placed.buildingId, planeX, planeY, instanceId, &reason, placed.level, rotationDeg) ||
        patch.maxSlopeDegrees > def->maxSurfaceSlopeDegrees ||
        (def->maxSurfaceHeightDelta >= 0.f && patch.heightDelta > def->maxSurfaceHeightDelta)) {
        if (reason.empty())
            reason = patch.maxSlopeDegrees > def->maxSurfaceSlopeDegrees ? "surface_slope" : "surface_height_delta";
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "free surface move preflight rejected without mutation", reason, {},
            "building.edit"));
    }

    PlacementEditReceipt receipt;
    receipt.before     = placed;
    placed.worldX      = planeX;
    placed.worldY      = planeY;
    placed.elevation   = elevation;
    placed.rotationDeg = normalizeRotation(placed.buildingId, rotationDeg);
    grid::worldToCell(*world->grid_, planeX, planeY, placed.originCellX, placed.originCellY, world->width_,
                      world->height_);
    placed.surfaceId              = patch.anchor.surfaceId;
    placed.surfaceRevision        = patch.anchor.surfaceRevision;
    placed.surfaceNormalX         = patch.anchor.normalX;
    placed.surfaceNormalY         = patch.anchor.normalY;
    placed.surfaceNormalZ         = patch.anchor.normalZ;
    placed.surfaceTangentX        = patch.anchor.tangentX;
    placed.surfaceTangentY        = patch.anchor.tangentY;
    placed.surfaceTangentZ        = patch.anchor.tangentZ;
    placed.surfaceSampleCount     = static_cast<int>(patch.samples.size());
    placed.surfaceMaxSlopeDegrees = patch.maxSlopeDegrees;
    placed.surfaceHeightDelta     = patch.heightDelta;
    receipt.after                 = placed;

    BuildingChangeEvent event;
    event.action      = "move";
    event.worldId     = world->getId();
    event.buildingId  = placed.buildingId;
    event.instanceId  = instanceId;
    event.cellX       = placed.originCellX;
    event.cellY       = placed.originCellY;
    event.level       = placed.level;
    event.rotationDeg = placed.rotationDeg;
    event.worldX      = placed.worldX;
    event.worldY      = placed.worldY;
    event.elevation   = placed.elevation;
    event.channel     = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

eve::Result<PlacementEditReceipt> PlacementSystem::replaceBuildingResult(PlacementWorld *world, int instanceId,
                                                                         const std::string &replacementBuildingId) {
    if (!world || instanceId <= 0 || replacementBuildingId.empty()) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "replace requires a world, positive instance id and replacement definition",
                                   replacementBuildingId, {}, "building.edit"));
    }
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "replace source instance was not found",
                                   std::to_string(instanceId), {}, "building.edit"));
    }
    PlacedBuilding           &pb          = it->second;
    const BuildingDefinition *replacement = BuildingRegistry::find(replacementBuildingId);
    if (!replacement) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "replacement definition was not found",
                                   replacementBuildingId, {}, "building.edit"));
    }
    const bool sourceEdge   = pb.placementKind == "edge";
    const bool sourceCorner = pb.placementKind == "corner";
    const bool sourceFree   = pb.placementKind == "free";
    if (pb.placementKind != replacement->placementKind) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "replacement must use the same placement domain as the source",
            replacementBuildingId, {}, "building.edit"));
    }
    if (hasStructuralDependents(*world, instanceId) &&
        !replacementPreservesStructuralDependents(*world, instanceId, *replacement)) {
        return eve::Result<PlacementEditReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "replace would invalidate structural dependents",
                                   "support_in_use", {}, "building.structure"));
    }

    std::string reason;
    if (sourceEdge) {
        const char *direction = pb.edge.axis == EdgeAxis::Horizontal ? "north" : "west";
        if (!canPlaceEdge(world, replacementBuildingId, pb.edge.x, pb.edge.y, direction, instanceId, &reason,
                          pb.level)) {
            return eve::Result<PlacementEditReceipt>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "replace preflight rejected without mutation",
                                       reason, {}, "building.edit"));
        }
    } else if (sourceCorner) {
        if (!canPlaceCorner(world, replacementBuildingId, pb.corner.x, pb.corner.y, instanceId, &reason, pb.level)) {
            return eve::Result<PlacementEditReceipt>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "replace preflight rejected without mutation",
                                       reason, {}, "building.edit"));
        }
    } else if (sourceFree) {
        if (!canPlaceFree(world, replacementBuildingId, pb.worldX, pb.worldY, instanceId, &reason, pb.level,
                          pb.rotationDeg)) {
            return eve::Result<PlacementEditReceipt>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "replace preflight rejected without mutation",
                                       reason, {}, "building.edit"));
        }
    } else if (!canPlaceElev(world, replacementBuildingId, pb.originCellX, pb.originCellY, pb.elevation, pb.rotationDeg,
                             instanceId, &reason, pb.level)) {
        return eve::Result<PlacementEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "replace preflight rejected without mutation", reason, {}, "building.edit"));
    }

    PlacementEditReceipt receipt;
    receipt.before = pb;
    BuildingChangeEvent event;
    event.action          = "replace";
    event.worldId         = world->getId();
    event.otherBuildingId = pb.buildingId;
    event.buildingId      = replacementBuildingId;
    event.instanceId      = instanceId;
    event.otherCellX      = pb.originCellX;
    event.otherCellY      = pb.originCellY;

    dissolveEdgeCurveGroup(world, pb.edgeCurveGroupId);
    clearOccupancy(*world, instanceId);
    pb.buildingId    = replacementBuildingId;
    pb.placementKind = replacement->placementKind;
    pb.channel       = replacement->channel;
    pb.tags          = replacement->tags;
    if (sourceFree) {
        applyDefinitionFreeFootprint(pb, *replacement, *world);
    }
    pb.rotationDeg = sourceEdge ? pb.rotationDeg : normalizeRotation(replacementBuildingId, pb.rotationDeg);
    if (sourceEdge) {
        pb.supportInstanceIds.clear();
        auto        &channels  = world->edgeChannels(pb.edge.axis, pb.level);
        auto        &occupancy = channels[replacement->channel];
        const size_t required  = pb.edge.axis == EdgeAxis::Horizontal
                                     ? size_t(world->width_) * size_t(world->height_ + 1)
                                     : size_t(world->width_ + 1) * size_t(world->height_);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        const size_t index = pb.edge.axis == EdgeAxis::Horizontal
                                 ? size_t(pb.edge.y) * size_t(world->width_) + size_t(pb.edge.x)
                                 : size_t(pb.edge.y) * size_t(world->width_ + 1) + size_t(pb.edge.x);
        occupancy[index]   = instanceId;
    } else if (sourceCorner) {
        PlacementQuery supportQuery;
        supportQuery.cellX     = pb.corner.x;
        supportQuery.cellY     = pb.corner.y;
        supportQuery.level     = pb.level;
        pb.supportInstanceIds  = collectStructuralSupports(*world, *replacement, supportQuery);
        auto        &occupancy = world->cornerChannels(pb.level)[replacement->channel];
        const size_t required  = size_t(world->width_ + 1) * size_t(world->height_ + 1);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        occupancy[size_t(pb.corner.y) * size_t(world->width_ + 1) + size_t(pb.corner.x)] = instanceId;
    } else {
        PlacementQuery supportQuery;
        supportQuery.cellX       = pb.originCellX;
        supportQuery.cellY       = pb.originCellY;
        supportQuery.level       = pb.level;
        supportQuery.rotationDeg = pb.rotationDeg;
        pb.supportInstanceIds    = collectStructuralSupports(*world, *replacement, supportQuery);
        writeOccupancy(*world, *replacement, pb, instanceId);
    }
    event.cellX       = pb.originCellX;
    event.cellY       = pb.originCellY;
    event.rotationDeg = pb.rotationDeg;
    event.worldX      = pb.worldX;
    event.worldY      = pb.worldY;
    event.elevation   = pb.elevation;
    event.channel     = pb.channel;
    receipt.after     = pb;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacementEditReceipt>::success(std::move(receipt));
}

void PlacementSystem::clearBuildings(PlacementWorld *world) {
    if (!world) return;
    // Collect ids first so remove events fire consistently.
    std::vector<int> ids = world->instanceOrder_;
    for (int id : ids)
        if (world->hasBuilding(id)) removeBuildingCascade(world, id);
    world->buildings().clear();
    world->edgeCurveGroups_.clear();
    world->instanceOrder_.clear();
    for (auto &kv : world->allChannels()) {
        std::fill(kv.second.begin(), kv.second.end(), 0);
    }
    for (auto &kv : world->horizontalEdgeChannels_) std::fill(kv.second.begin(), kv.second.end(), 0);
    for (auto &kv : world->verticalEdgeChannels_) std::fill(kv.second.begin(), kv.second.end(), 0);
    world->cellChannelsByLevel_.clear();
    world->horizontalEdgesByLevel_.clear();
    world->verticalEdgesByLevel_.clear();
}

}  // namespace eve::building
