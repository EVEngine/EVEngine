#include "building/BuildingAssemblyInternal.h"
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

void PlacementSystem::dissolveEdgeCurveGroup(PlacementWorld *world, EdgeCurveGroupId groupId) {
    if (!world || !groupId) return;
    const auto group = world->edgeCurveGroups_.find(groupId.value);
    if (group == world->edgeCurveGroups_.end()) return;
    for (int memberId : group->second.instanceIds) {
        const auto member = world->buildings_.find(memberId);
        if (member != world->buildings_.end() && member->second.edgeCurveGroupId == groupId)
            member->second.edgeCurveGroupId = {};
    }
    world->edgeCurveGroups_.erase(group);
}

bool PlacementSystem::removeBuildingUnchecked(PlacementWorld *world, int instanceId) {
    if (!world || instanceId <= 0) return false;
    auto it = world->buildings().find(instanceId);
    if (it == world->buildings().end()) return false;

    const EdgeCurveGroupId curveGroupId = it->second.edgeCurveGroupId;
    BuildingChangeEvent    ev;
    ev.action      = "remove";
    ev.worldId     = world->getId();
    ev.buildingId  = it->second.buildingId;
    ev.instanceId  = instanceId;
    ev.cellX       = it->second.originCellX;
    ev.cellY       = it->second.originCellY;
    ev.rotationDeg = it->second.rotationDeg;
    ev.worldX      = it->second.worldX;
    ev.worldY      = it->second.worldY;
    ev.elevation   = it->second.elevation;
    ev.channel     = it->second.channel;

    dissolveEdgeCurveGroup(world, curveGroupId);
    clearOccupancy(*world, instanceId);
    assembly_detail::removeMembership(*world->assemblyState_, instanceId);
    world->buildings().erase(it);
    auto &order = world->instanceOrder_;
    order.erase(std::remove(order.begin(), order.end(), instanceId), order.end());
    if (world->publishEvents_) emit(std::move(ev));
    return true;
}

bool PlacementSystem::moveBuilding(PlacementWorld *world, int instanceId, int cellX, int cellY, float rotationDeg) {
    auto result = moveBuildingResult(world, instanceId, cellX, cellY, rotationDeg);
    return result.ok();
}

bool PlacementSystem::removeBuilding(PlacementWorld *world, int instanceId) {
    if (!world || !world->hasBuilding(instanceId) || hasStructuralDependents(*world, instanceId)) return false;
    return removeBuildingUnchecked(world, instanceId);
}

eve::Result<StructuralRemovalReceipt> PlacementSystem::removeBuildingCascadeResult(PlacementWorld *world,
                                                                                   int             instanceId) {
    if (!world || instanceId <= 0) {
        return eve::Result<StructuralRemovalReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "cascade removal requires a world and positive instance id",
            std::to_string(instanceId), {}, "building.structure"));
    }
    if (!world->hasBuilding(instanceId)) {
        return eve::Result<StructuralRemovalReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "cascade removal source instance was not found",
                                   std::to_string(instanceId), {}, "building.structure"));
    }
    std::vector<int>         ordered;
    std::unordered_set<int>  visiting;
    std::unordered_set<int>  visited;
    bool                     cycle   = false;
    std::function<void(int)> collect = [&](int supportId) {
        if (visited.contains(supportId)) return;
        if (!visiting.insert(supportId).second) {
            cycle = true;
            return;
        }
        for (const auto &[candidateId, placed] : world->buildings_) {
            if (std::find(placed.supportInstanceIds.begin(), placed.supportInstanceIds.end(), supportId) !=
                placed.supportInstanceIds.end())
                collect(candidateId);
        }
        visiting.erase(supportId);
        visited.insert(supportId);
        ordered.push_back(supportId);
    };
    collect(instanceId);
    if (cycle) {
        return eve::Result<StructuralRemovalReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "structural dependency cycle prevents removal",
                                   "support_cycle", {}, "building.structure"));
    }

    StructuralRemovalReceipt receipt;
    receipt.removed.reserve(ordered.size());
    for (int id : ordered) receipt.removed.push_back(world->buildings_.at(id));
    const bool publishEvents = world->publishEvents_;
    world->publishEvents_    = false;
    for (int id : ordered) removeBuildingUnchecked(world, id);
    world->publishEvents_ = publishEvents;
    if (publishEvents) {
        for (const PlacedBuilding &removed : receipt.removed) {
            BuildingChangeEvent event;
            event.action      = "remove";
            event.worldId     = world->getId();
            event.buildingId  = removed.buildingId;
            event.instanceId  = removed.instanceId;
            event.cellX       = removed.originCellX;
            event.cellY       = removed.originCellY;
            event.level       = removed.level;
            event.rotationDeg = removed.rotationDeg;
            event.worldX      = removed.worldX;
            event.worldY      = removed.worldY;
            event.elevation   = removed.elevation;
            event.channel     = removed.channel;
            emit(std::move(event));
        }
    }
    return eve::Result<StructuralRemovalReceipt>::success(std::move(receipt));
}

int PlacementSystem::removeBuildingCascade(PlacementWorld *world, int instanceId) {
    auto result = removeBuildingCascadeResult(world, instanceId);
    return result.ok() ? int(result.value().removed.size()) : 0;
}

eve::Result<StructuralLinkRebuildReceipt> PlacementSystem::rebuildStructuralLinksResult(PlacementWorld *world) {
    if (!world) {
        return eve::Result<StructuralLinkRebuildReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "structural rebuild requires a world", {}, {}, "building.structure"));
    }
    auto candidate = world->cloneState();
    if (!candidate) {
        return eve::Result<StructuralLinkRebuildReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                   "failed to clone structural rebuild candidate", {}, {}, "building.structure"));
    }

    StructuralLinkRebuildReceipt receipt;
    for (int id : candidate->instanceOrder_) {
        auto placedIt = candidate->buildings_.find(id);
        if (placedIt == candidate->buildings_.end()) continue;
        PlacedBuilding &placed = placedIt->second;
        ++receipt.inspectedCount;
        const BuildingDefinition *def = BuildingRegistry::find(placed.buildingId);
        if (!def) {
            return eve::Result<StructuralLinkRebuildReceipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "structural rebuild encountered an unknown building definition",
                std::to_string(id), {}, "building.structure"));
        }

        std::vector<int> rebuilt;
        if (def->supportMode != "none") {
            PlacementQuery query;
            query.buildingId  = placed.buildingId;
            query.cellX       = placed.originCellX;
            query.cellY       = placed.originCellY;
            query.level       = placed.level;
            query.rotationDeg = placed.rotationDeg;
            std::string reason;
            if (!checkStructuralSupport(*candidate, *def, query, &reason)) {
                return eve::Result<StructuralLinkRebuildReceipt>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "structural rebuild found an invalid dependency",
                    std::to_string(id) + ":" + reason, {}, "building.structure"));
            }
            rebuilt = collectStructuralSupports(*candidate, *def, query);
        }
        if (rebuilt != placed.supportInstanceIds) {
            placed.supportInstanceIds = std::move(rebuilt);
            ++receipt.changedCount;
        }
    }
    world->swapState(*candidate);
    return eve::Result<StructuralLinkRebuildReceipt>::success(std::move(receipt));
}


}  // namespace eve::building
