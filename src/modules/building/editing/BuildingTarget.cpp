#include "building/editing/BuildingTarget.h"
#include "building/editing/BuildingTargetInternal.h"

#include "building/BuildingDef.h"
#include "building/BuildingSnapshot.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace eve::building_editing {
using building::codec::runtime;
using building::codec::snapshot;
namespace target_detail {

DomainOperation instanceOperation(const std::string& target, std::string type, std::string inverseType,
                                  const BuildingInstanceSnapshot& payload,
                                  const BuildingInstanceSnapshot& inverse) {
    DomainOperation result;
    result.type = std::move(type);
    result.inverseType = std::move(inverseType);
    result.target = TargetId(target);
    result.payload = instanceValue(payload);
    result.inverse = instanceValue(inverse);
    result.hasInverse = true;
    result.affectedObjects.push_back({TargetId(target), std::to_string(payload.instanceId), 0});
    return result;
}

Result<DomainOperation> areaOperation(
    const std::string& target, building::PlacementWorld* world,
    const building::PlacementSystem::AreaPreview& preview) {
    if (!world || preview.cells.empty() || preview.rejectedCount != 0 ||
        preview.acceptedCount != static_cast<int>(preview.cells.size()))
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.building.area-rejected"),
                                                     "Area placement requires a fully accepted preview");
    const building::BuildingDefinition* definition =
        building::BuildingRegistry::find(preview.buildingId);
    if (!definition || definition->placementKind == "edge")
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected,
                                                     RuleId("editor.building.area-definition-invalid"),
                                                     "Area placement requires a cell building definition");
    DomainOperation operation;
    operation.type = "building.area.set.v1";
    operation.inverseType = "building.area.delete.v1";
    operation.target = TargetId(target);
    EditorValue::Array instances;
    int nextId = 1;
    for (const building::PlacementSystem::AreaCellPreview& cell : preview.cells) {
        while (world->hasBuilding(nextId)) ++nextId;
        BuildingInstanceSnapshot placed;
        placed.instanceId = nextId++;
        placed.buildingId = preview.buildingId;
        placed.cellX = cell.cellX;
        placed.cellY = cell.cellY;
        placed.rotationDegrees = preview.rotationDeg;
        placed.channel = definition->channel;
        placed.tags = definition->tags;
        float worldX = 0.f;
        float worldY = 0.f;
        world->cellToWorldPlane(cell.cellX, cell.cellY, worldX, worldY);
        placed.worldX = worldX;
        placed.worldY = worldY;
        instances.push_back(instanceValue(placed));
        operation.affectedObjects.push_back(
            {TargetId(target), std::to_string(placed.instanceId), 0});
    }
    operation.payload = instances;
    operation.inverse = std::move(instances);
    operation.hasInverse = true;
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

}  // namespace target_detail
using namespace target_detail;

BuildingPlacementTarget::BuildingPlacementTarget(std::string id, building::PlacementWorld* world)
    : id_(std::move(id)), world_(world) {}

BuildingPlacementTarget::BuildingPlacementTarget(std::string id, std::unique_ptr<building::PlacementWorld> world,
                                                 unsigned long long revision)
    : EditableTargetState(revision), id_(std::move(id)), world_(world.get()), ownedWorld_(std::move(world)) {}

BuildingPlacementTarget::BuildingPlacementTarget(BuildingPlacementTarget&&) noexcept            = default;
BuildingPlacementTarget& BuildingPlacementTarget::operator=(BuildingPlacementTarget&&) noexcept = default;
BuildingPlacementTarget::~BuildingPlacementTarget()                                             = default;

TargetDescriptor BuildingPlacementTarget::describe() const {
    TargetDescriptor result;
    result.id = TargetId(id_);
    result.type = "building-placement-world";
    result.revision     = revisionValue();
    result.readOnly = world_ == nullptr;
    result.capabilities = {editorCapabilityId()};
    return result;
}

void* BuildingPlacementTarget::queryCapability(const CapabilityId& capability) {
    return capability == editorCapabilityId() ? this : nullptr;
}

Result<BuildingInstanceSnapshot> BuildingPlacementTarget::instance(int instanceId) const {
    if (!world_)
        return eve::editing::failed<BuildingInstanceSnapshot>(
            EditorStatus::Rejected, RuleId("editor.building.world-required"), "Placement world is unavailable");
    const auto found = world_->buildings().find(instanceId);
    if (found == world_->buildings().end())
        return eve::editing::failed<BuildingInstanceSnapshot>(EditorStatus::NotFound,
                                                              RuleId("editor.building.instance-not-found"),
                                                              "Placed building instance was not found");
    return eve::editing::applied<BuildingInstanceSnapshot>(snapshot(found->second));
}

int BuildingPlacementTarget::nextAvailableInstanceId() const {
    if (!world_) return 0;
    int result = 1;
    while (world_->hasBuilding(result)) ++result;
    return result;
}

BuildingPlacementPreview BuildingPlacementTarget::preview(const std::string& buildingId, double worldX,
                                                          double worldY, double elevation,
                                                          double rotationDegrees,
                                                          int excludeInstanceId) const {
    BuildingPlacementPreview result;
    result.worldRevision = revisionValue();
    if (!world_) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
            RuleId("editor.building.world-required"), DiagnosticSeverity::Error,
            "Placement world is unavailable"));
        return result;
    }
    building::PlacementSystem::ensureBuiltins();
    const building::BuildingDefinition* definition = building::BuildingRegistry::find(buildingId);
    if (!definition) {
        result.status = EditorStatus::NotFound;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::NotFound,
            RuleId("editor.building.unknown-building"), DiagnosticSeverity::Error,
            "Building definition was not found: " + buildingId));
        return result;
    }
    const building::SnapResult snapped = building::PlacementSystem::snap(*world_, buildingId,
                                                                          static_cast<float>(worldX),
                                                                          static_cast<float>(worldY));
    result.snappedCellX = snapped.cellX;
    result.snappedCellY = snapped.cellY;
    result.snappedWorldX = snapped.worldX;
    result.snappedWorldY = snapped.worldY;
    result.elevation = elevation;
    result.normalizedRotation = building::PlacementSystem::normalizeRotation(
        buildingId, static_cast<float>(rotationDegrees));
    building::PlacementSystem::foreachFootprintCell(
        *definition, result.snappedCellX, result.snappedCellY,
        static_cast<float>(result.normalizedRotation), [&](int x, int y) {
            BuildingFootprintCell cell;
            cell.x = x;
            cell.y = y;
            cell.inBounds = world_->inBounds(x, y);
            if (cell.inBounds) {
                cell.occupant = world_->getOccupantInChannel(definition->channel, x, y);
                cell.terrain = world_->getTerrain(x, y);
            }
            result.cells.push_back(cell);
            return true;
        });
    std::string reason;
    const bool valid = building::PlacementSystem::canPlaceElev(
        world_, buildingId, result.snappedCellX, result.snappedCellY, static_cast<float>(elevation),
        static_cast<float>(result.normalizedRotation), excludeInstanceId, &reason);
    result.status = valid ? EditorStatus::Applied : EditorStatus::Rejected;
    if (!valid)
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
            RuleId("editor.building." + (reason.empty() ? "rejected" : reason)), DiagnosticSeverity::Error,
            "Building footprint validation rejected placement: " + reason));
    return result;
}

Result<DomainOperation> BuildingPlacementTarget::makePlace(
    const BuildingInstanceSnapshot& placed) const {
    if (!world_ || placed.instanceId <= 0 || world_->hasBuilding(placed.instanceId))
        return eve::editing::failed<DomainOperation>(EditorStatus::Conflict,
                                                     RuleId("editor.building.instance-id-conflict"),
                                                     "Exact placement instance id is unavailable");
    std::string reason;
    const bool valid = placed.placementKind == "edge"
                           ? building::PlacementSystem::canPlaceEdge(
                                 world_, placed.buildingId, placed.edgeX, placed.edgeY,
                                 placed.edgeAxis == "vertical" ? "west" : "north", 0, &reason)
                       : placed.placementKind == "corner"
                           ? building::PlacementSystem::canPlaceCorner(
                                 world_, placed.buildingId, placed.cornerX, placed.cornerY, 0,
                                 &reason)
                       : placed.placementKind == "free"
                           ? building::PlacementSystem::canPlaceFree(
                                 world_, placed.buildingId, static_cast<float>(placed.worldX),
                                 static_cast<float>(placed.worldY), 0, &reason, placed.level)
                           : building::PlacementSystem::canPlaceElev(
                                 world_, placed.buildingId, placed.cellX, placed.cellY,
                                 static_cast<float>(placed.elevation),
                                 static_cast<float>(placed.rotationDegrees), 0, &reason);
    if (!valid)
        return eve::editing::failed<DomainOperation>(
            EditorStatus::Rejected, RuleId("editor.building." + (reason.empty() ? "rejected" : reason)),
            "Building footprint validation rejected placement: " + reason);
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.set.v4", "building.instance.delete.v4", placed, placed));
}

Result<DomainOperation> BuildingPlacementTarget::makeMove(int instanceId, int cellX, int cellY,
                                                                double rotationDegrees) const {
    auto current = instance(instanceId);
    if (!current.ok())
        return eve::editing::failed<DomainOperation>(current.code(), RuleId("editor.building.instance-not-found"),
                                                     "Placed building instance was not found");
    BuildingInstanceSnapshot desired = current.value();
    if (desired.placementKind == "free")
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected,
                                                     RuleId("editor.building.free-move-requires-world-anchor"),
                                                     "Free objects must be moved with makeMoveFree");
    desired.cellX = cellX;
    desired.cellY = cellY;
    if (desired.placementKind == "corner") {
        desired.cornerX = cellX;
        desired.cornerY = cellY;
    }
    desired.rotationDegrees = building::PlacementSystem::normalizeRotation(
        desired.buildingId, static_cast<float>(rotationDegrees));
    float worldX = 0.f;
    float worldY = 0.f;
    world_->cellToWorldPlane(cellX, cellY, worldX, worldY);
    desired.worldX = worldX;
    desired.worldY = worldY;
    std::string reason;
    const bool valid = desired.placementKind == "corner"
                           ? building::PlacementSystem::canPlaceCorner(
                                 world_, desired.buildingId, desired.cornerX, desired.cornerY,
                                 instanceId, &reason)
                       : desired.placementKind == "free"
                           ? building::PlacementSystem::canPlaceFree(
                                 world_, desired.buildingId, static_cast<float>(desired.worldX),
                                 static_cast<float>(desired.worldY), instanceId, &reason,
                                 desired.level)
                           : building::PlacementSystem::canPlaceElev(
                                 world_, desired.buildingId, cellX, cellY,
                                 static_cast<float>(desired.elevation),
                                 static_cast<float>(desired.rotationDegrees), instanceId,
                                 &reason);
    if (!valid)
        return eve::editing::failed<DomainOperation>(
            EditorStatus::Rejected, RuleId("editor.building." + (reason.empty() ? "rejected" : reason)),
            "Building footprint validation rejected move: " + reason);
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.set.v4", "building.instance.set.v4", desired, current.value()));
}

Result<DomainOperation> BuildingPlacementTarget::makeMoveFree(
    int instanceId, double worldX, double worldY, double elevation,
    double rotationDegrees) const {
    auto current = instance(instanceId);
    if (!current.ok())
        return eve::editing::failed<DomainOperation>(current.code(), RuleId("editor.building.instance-not-found"),
                                                     "Placed building instance was not found");
    if (current.value().placementKind != "free")
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected,
                                                     RuleId("editor.building.not-free-instance"),
                                                     "makeMoveFree requires a free-domain instance");
    BuildingInstanceSnapshot desired = current.value();
    desired.worldX = worldX;
    desired.worldY = worldY;
    desired.elevation = elevation;
    desired.rotationDegrees = building::PlacementSystem::normalizeRotation(
        desired.buildingId, static_cast<float>(rotationDegrees));
    std::string reason;
    if (!building::PlacementSystem::canPlaceFree(
            world_, desired.buildingId, static_cast<float>(worldX), static_cast<float>(worldY),
            instanceId, &reason, desired.level))
        return eve::editing::failed<DomainOperation>(
            EditorStatus::Rejected, RuleId("editor.building." + (reason.empty() ? "rejected" : reason)),
            "Free building validation rejected move: " + reason);
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.set.v4", "building.instance.set.v4", desired,
        current.value()));
}

Result<DomainOperation> BuildingPlacementTarget::makeReplace(
    int instanceId, const std::string& replacementBuildingId) const {
    auto current = instance(instanceId);
    if (!current.ok())
        return eve::editing::failed<DomainOperation>(current.code(), RuleId("editor.building.instance-not-found"),
                                                     "Placed building instance was not found");
    const building::BuildingDefinition* replacement =
        building::BuildingRegistry::find(replacementBuildingId);
    if (!replacement)
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound,
                                                     RuleId("editor.building.replacement-not-found"),
                                                     "Replacement building definition was not found");
    const auto source = world_->buildings().find(instanceId);
    if (source == world_->buildings().end())
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound,
                                                     RuleId("editor.building.instance-not-found"),
                                                     "Placed building instance was not found");
    const std::string& sourceKind = source->second.placementKind;
    if (sourceKind != replacement->placementKind)
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected,
                                                     RuleId("editor.building.replace-domain-mismatch"),
                                                     "Replacement must use the same placement domain");
    std::string reason;
    const bool valid = sourceKind == "edge"
                           ? building::PlacementSystem::canPlaceEdge(
                                 world_, replacementBuildingId, current.value().edgeX,
                                 current.value().edgeY,
                                 current.value().edgeAxis == "vertical" ? "west" : "north",
                                 instanceId, &reason)
                       : sourceKind == "corner"
                           ? building::PlacementSystem::canPlaceCorner(
                                 world_, replacementBuildingId, current.value().cornerX,
                                 current.value().cornerY, instanceId, &reason)
                       : sourceKind == "free"
                           ? building::PlacementSystem::canPlaceFree(
                                 world_, replacementBuildingId,
                                 static_cast<float>(current.value().worldX),
                                 static_cast<float>(current.value().worldY), instanceId, &reason,
                                 current.value().level)
                           : building::PlacementSystem::canPlaceElev(
                                 world_, replacementBuildingId, current.value().cellX,
                                 current.value().cellY,
                                 static_cast<float>(current.value().elevation),
                                 static_cast<float>(current.value().rotationDegrees), instanceId,
                                 &reason);
    if (!valid)
        return eve::editing::failed<DomainOperation>(
            EditorStatus::Rejected, RuleId("editor.building." + (reason.empty() ? "rejected" : reason)),
            "Building replacement validation failed: " + reason);
    BuildingInstanceSnapshot desired = current.value();
    desired.buildingId = replacementBuildingId;
    desired.placementKind = replacement->placementKind;
    desired.channel = replacement->channel;
    desired.tags = replacement->tags;
    if (sourceKind == "cell")
        desired.rotationDegrees = building::PlacementSystem::normalizeRotation(
            replacementBuildingId, static_cast<float>(desired.rotationDegrees));
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.set.v4", "building.instance.set.v4", desired, current.value()));
}

Result<DomainOperation> BuildingPlacementTarget::makeRectangle(
    const std::string& buildingId, int minCellX, int minCellY, int maxCellX, int maxCellY,
    double rotationDegrees) const {
    auto preview = building::PlacementSystem::previewRectangle(
        world_, buildingId, minCellX, minCellY, maxCellX, maxCellY,
        static_cast<float>(rotationDegrees));
    if (!preview.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected,
                                                     RuleId("editor.building.area-preview-failed"),
                                                     "Rectangle placement preview failed");
    return areaOperation(id_, world_, preview.value());
}

Result<DomainOperation> BuildingPlacementTarget::makeBrush(
    const std::string& buildingId, int centerCellX, int centerCellY, int radius,
    double rotationDegrees) const {
    auto preview = building::PlacementSystem::previewBrush(
        world_, buildingId, centerCellX, centerCellY, radius,
        static_cast<float>(rotationDegrees));
    if (!preview.ok())
        return eve::editing::failed<DomainOperation>(
            EditorStatus::Rejected, RuleId("editor.building.area-preview-failed"), "Brush placement preview failed");
    return areaOperation(id_, world_, preview.value());
}

Result<DomainOperation> BuildingPlacementTarget::makeEdgePath(
    const std::string& buildingId,
    const std::vector<BuildingEdgePathVertex>& vertices) const {
    std::vector<building::CornerAddress> runtimeVertices;
    runtimeVertices.reserve(vertices.size());
    for (const BuildingEdgePathVertex& vertex : vertices)
        runtimeVertices.push_back({vertex.x, vertex.y});
    auto preview = building::PlacementSystem::previewEdgePath(world_, buildingId,
                                                              runtimeVertices);
    if (!preview.ok())
        return eve::editing::failed<DomainOperation>(
            EditorStatus::Rejected, RuleId("editor.building.edge-path-rejected"), "Edge path preview was rejected");
    const building::BuildingDefinition* definition =
        building::BuildingRegistry::find(buildingId);
    if (!definition)
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound,
                                                     RuleId("editor.building.definition-not-found"),
                                                     "Edge path definition was not found");
    DomainOperation operation;
    operation.type = "building.area.set.v1";
    operation.inverseType = "building.area.delete.v1";
    operation.target = TargetId(id_);
    EditorValue::Array instances;
    int nextId = 1;
    for (const building::EdgeAddress& edge : preview.value().edges) {
        while (world_->hasBuilding(nextId)) ++nextId;
        BuildingInstanceSnapshot placed;
        placed.instanceId = nextId++;
        placed.buildingId = buildingId;
        placed.placementKind = "edge";
        placed.edgeX = edge.x;
        placed.edgeY = edge.y;
        placed.edgeAxis = edge.axis == building::EdgeAxis::Horizontal ? "horizontal" : "vertical";
        placed.cellX = edge.x;
        placed.cellY = edge.y;
        placed.level = preview.value().level;
        placed.rotationDegrees = edge.axis == building::EdgeAxis::Horizontal ? 0.0 : 90.0;
        placed.channel = definition->channel;
        placed.tags = definition->tags;
        float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
        world_->cellToWorldPlane(edge.x, edge.y, ax, ay);
        world_->cellToWorldPlane(edge.x + (edge.axis == building::EdgeAxis::Horizontal ? 1 : 0),
                                 edge.y + (edge.axis == building::EdgeAxis::Vertical ? 1 : 0),
                                 bx, by);
        placed.worldX = (ax + bx) * 0.5;
        placed.worldY = (ay + by) * 0.5;
        instances.push_back(instanceValue(placed));
        operation.affectedObjects.push_back(
            {TargetId(id_), std::to_string(placed.instanceId), 0});
    }
    operation.payload = instances;
    operation.inverse = std::move(instances);
    operation.hasInverse = true;
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

Result<DomainOperation> BuildingPlacementTarget::makeRemove(int instanceId) const {
    auto current = instance(instanceId);
    if (!current.ok())
        return eve::editing::failed<DomainOperation>(current.code(), RuleId("editor.building.instance-not-found"),
                                                     "Placed building instance was not found");
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.delete.v4", "building.instance.set.v4", current.value(), current.value()));
}

Result<void> BuildingPlacementTarget::applyDomainOperation(const DomainOperation& operation) {
    if (!world_)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.building.world-required"),
                                          "Placement world is unavailable");
    if (operation.target != TargetId(id_))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.building.target-mismatch"),
                                          "Building operation targets another world");
    const bool areaSet = operation.type == "building.area.set.v1";
    const bool areaDelete = operation.type == "building.area.delete.v1";
    const bool edgeCurveSet = operation.type == "building.edge-curve.set.v1";
    const bool edgeCurveDelete = operation.type == "building.edge-curve.delete.v1";
    const bool edgeCurveReplace = operation.type == "building.edge-curve.replace.v1";
    if (edgeCurveSet || edgeCurveDelete || edgeCurveReplace) {
        auto parsedCurve = parseEdgeCurve(operation.payload);
        if (!parsedCurve.ok())
            return eve::editing::failed<void>(parsedCurve.code(), RuleId("editor.building.invalid-edge-curve-payload"),
                                              "Building edge curve operation payload is invalid");
        std::unique_ptr<IDomainOperationTarget> candidate = cloneDomainState();
        auto* buildingCandidate = dynamic_cast<BuildingPlacementTarget*>(candidate.get());
        if (!buildingCandidate)
            return eve::editing::failed<void>(EditorStatus::Failed,
                                              RuleId("editor.building.edge-curve-staging-unavailable"),
                                              "Building edge curve operation could not create a staging world");
        if (edgeCurveSet || edgeCurveReplace) {
            if (edgeCurveReplace) {
                auto current = buildingCandidate->world_->edgeCurveGroup(
                    parsedCurve.value().group.id);
                if (!current.ok())
                    return eve::editing::failed<void>(EditorStatus::NotFound,
                                                      RuleId("editor.building.edge-curve-replace-source-not-found"),
                                                      "Building edge curve replacement source was not found");
                for (int instanceId : current.value().instanceIds)
                    if (!building::PlacementSystem::removeBuilding(buildingCandidate->world_,
                                                                   instanceId))
                        return eve::editing::failed<void>(
                            EditorStatus::Rejected, RuleId("editor.building.edge-curve-replace-delete-rejected"),
                            "Building edge curve replacement could not remove its source");
            }
            std::vector<building::PlacedBuilding> members;
            for (const BuildingInstanceSnapshot& instance : parsedCurve.value().instances)
                members.push_back(runtime(instance));
            auto restored = building::PlacementSystem::restoreEdgeCurveGroupExact(
                buildingCandidate->world_, parsedCurve.value().group, members);
            if (!restored.ok())
                return eve::editing::failed<void>(EditorStatus::Rejected,
                                                  RuleId("editor.building.edge-curve-atomic-rejected"),
                                                  "Building edge curve restore was rejected without mutation");
        } else {
            for (int instanceId : parsedCurve.value().group.instanceIds) {
                if (!buildingCandidate->world_->hasBuilding(instanceId) ||
                    !building::PlacementSystem::removeBuilding(buildingCandidate->world_,
                                                               instanceId))
                    return eve::editing::failed<void>(EditorStatus::Rejected,
                                                      RuleId("editor.building.edge-curve-delete-rejected"),
                                                      "Building edge curve delete was rejected without mutation");
            }
        }
        return commitDomainState(std::move(candidate));
    }
    if (areaSet || areaDelete) {
        auto parsedArea = parseInstances(operation.payload);
        if (!parsedArea.ok())
            return eve::editing::failed<void>(parsedArea.code(), RuleId("editor.building.invalid-area-payload"),
                                              "Building area operation payload is invalid");
        std::unique_ptr<IDomainOperationTarget> candidate = cloneDomainState();
        auto* buildingCandidate = dynamic_cast<BuildingPlacementTarget*>(candidate.get());
        if (!buildingCandidate)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.building.area-staging-unavailable"),
                                              "Building area operation could not create a staging world");
        for (const BuildingInstanceSnapshot& item : parsedArea.value()) {
            DomainOperation single = instanceOperation(
                id_, areaSet ? "building.instance.set.v4" : "building.instance.delete.v4",
                areaSet ? "building.instance.delete.v4" : "building.instance.set.v4", item,
                item);
            auto applied = buildingCandidate->applyDomainOperation(single);
            if (!applied.ok())
                return eve::editing::failed<void>(applied.code(), RuleId("editor.building.area-atomic-rejected"),
                                                  "Building area operation was rejected without mutation");
        }
        return commitDomainState(std::move(candidate));
    }
    auto parsed = parseInstance(operation.payload);
    if (!parsed.ok())
        return eve::editing::failed<void>(parsed.code(), RuleId("editor.building.invalid-instance"),
                                          "Building operation payload is invalid");
    if (operation.type == "building.instance.delete.v1" ||
        operation.type == "building.instance.delete.v2" ||
        operation.type == "building.instance.delete.v3" ||
        operation.type == "building.instance.delete.v4") {
        if (!world_->hasBuilding(parsed.value().instanceId))
            return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.building.instance-not-found"),
                                              "Placed building instance was not found");
        if (!building::PlacementSystem::removeBuilding(world_, parsed.value().instanceId))
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.building.remove-failed"),
                                              "PlacementSystem rejected building removal");
    } else if (operation.type == "building.instance.set.v1" ||
               operation.type == "building.instance.set.v2" ||
               operation.type == "building.instance.set.v3" ||
               operation.type == "building.instance.set.v4") {
        const building::PlacedBuilding desired = runtime(parsed.value());
        if (!world_->hasBuilding(desired.instanceId)) {
            std::string reason;
            if (building::PlacementSystem::restoreExact(world_, desired, &reason) !=
                building::PlacementRestoreStatus::Restored)
                return eve::editing::failed<void>(
                    EditorStatus::Rejected, RuleId("editor.building." + (reason.empty() ? "restore-failed" : reason)),
                    "PlacementSystem rejected exact instance restore: " + reason);
        } else {
            const auto current = world_->buildings().find(desired.instanceId);
            const building::BuildingDefinition* desiredDefinition =
                building::BuildingRegistry::find(desired.buildingId);
            if (!desiredDefinition)
                return eve::editing::failed<void>(EditorStatus::NotFound,
                                                  RuleId("editor.building.definition-not-found"),
                                                  "Desired building definition was not found");
            if (current->second.placementKind != desired.placementKind ||
                desired.placementKind != desiredDefinition->placementKind)
                return eve::editing::failed<void>(EditorStatus::Rejected,
                                                  RuleId("editor.building.placement-domain-mismatch"),
                                                  "Building set cannot change placement domain");
            std::string reason;
            const bool valid = desired.placementKind == "edge"
                                   ? building::PlacementSystem::canPlaceEdge(
                                         world_, desired.buildingId, desired.edge.x, desired.edge.y,
                                         desired.edge.axis == building::EdgeAxis::Vertical ? "west"
                                                                                          : "north",
                                         desired.instanceId, &reason)
                               : desired.placementKind == "corner"
                                   ? building::PlacementSystem::canPlaceCorner(
                                         world_, desired.buildingId, desired.corner.x,
                                         desired.corner.y, desired.instanceId, &reason)
                               : desired.placementKind == "free"
                                   ? building::PlacementSystem::canPlaceFree(
                                         world_, desired.buildingId, desired.worldX,
                                         desired.worldY, desired.instanceId, &reason,
                                         desired.level)
                                   : building::PlacementSystem::canPlaceElev(
                                         world_, desired.buildingId, desired.originCellX,
                                         desired.originCellY, desired.elevation,
                                         desired.rotationDeg, desired.instanceId, &reason);
            if (!valid)
                return eve::editing::failed<void>(
                    EditorStatus::Rejected, RuleId("editor.building." + (reason.empty() ? "set-rejected" : reason)),
                    "Building set preflight rejected without mutation: " + reason);
            if (current->second.buildingId != desired.buildingId) {
                auto replaced = building::PlacementSystem::replaceBuildingResult(
                    world_, desired.instanceId, desired.buildingId);
                if (!replaced.ok())
                    return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.building.replace-failed"),
                                                      "PlacementSystem rejected building replacement");
            }
            auto moved = desired.placementKind == "edge"
                             ? building::PlacementSystem::moveEdgeResult(
                                   world_, desired.instanceId, desired.edge.x, desired.edge.y,
                                   desired.edge.axis == building::EdgeAxis::Vertical ? "west"
                                                                                    : "north")
                         : desired.placementKind == "corner"
                             ? building::PlacementSystem::moveCornerResult(
                                   world_, desired.instanceId, desired.corner.x,
                                   desired.corner.y)
                         : desired.placementKind == "free"
                             ? building::PlacementSystem::moveFreeResult(
                                   world_, desired.instanceId, desired.worldX, desired.worldY,
                                   desired.elevation, desired.rotationDeg)
                             : building::PlacementSystem::moveBuildingResult(
                                   world_, desired.instanceId, desired.originCellX,
                                   desired.originCellY, desired.rotationDeg);
            if (!moved.ok())
                return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.building.move-failed"),
                                                  "PlacementSystem rejected building movement");
            building::PlacedBuilding& updated = world_->buildings().at(desired.instanceId);
            updated.worldX = desired.worldX;
            updated.worldY = desired.worldY;
            updated.elevation = desired.elevation;
            updated.placementKind = desired.placementKind;
            updated.edge = desired.edge;
            updated.corner = desired.corner;
            updated.surfaceId = desired.surfaceId;
            updated.surfaceRevision = desired.surfaceRevision;
            updated.surfaceNormalX = desired.surfaceNormalX;
            updated.surfaceNormalY = desired.surfaceNormalY;
            updated.surfaceNormalZ = desired.surfaceNormalZ;
            updated.surfaceTangentX = desired.surfaceTangentX;
            updated.surfaceTangentY = desired.surfaceTangentY;
            updated.surfaceTangentZ = desired.surfaceTangentZ;
            updated.surfaceSampleCount = desired.surfaceSampleCount;
            updated.surfaceMaxSlopeDegrees = desired.surfaceMaxSlopeDegrees;
            updated.surfaceHeightDelta = desired.surfaceHeightDelta;
            updated.props = desired.props;
            updated.tags = desired.tags;
            updated.garrison = desired.garrison;
            updated.garrisonRevision = desired.garrisonRevision;
        }
    } else {
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.building.operation-unsupported"),
                                          "Building placement operation is unsupported: " + operation.type);
    }
    bumpRevision();
    widenDirty(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> BuildingPlacementTarget::cloneDomainState() const {
    if (!world_) return {};
    return std::unique_ptr<IDomainOperationTarget>(
        new BuildingPlacementTarget(id_, world_->cloneState(), revisionValue()));
}

Result<void> BuildingPlacementTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* buildingCandidate = dynamic_cast<BuildingPlacementTarget*>(candidate.get());
    if (!world_ || !buildingCandidate || !buildingCandidate->ownedWorld_ ||
        buildingCandidate->targetId() != targetId())
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.building.candidate-mismatch"),
                                          "Building candidate cannot be published to this target");
    world_->swapState(*buildingCandidate->ownedWorld_);
    bumpRevision();
    widenDirty(0, 0);
    return eve::editing::applied<void>();
}

EditorValue BuildingPlacementTarget::snapshotValue() const {
    EditorValue::Array instances;
    EditorValue::Array curveGroups;
    if (world_)
        for (int index = 0; index < world_->getBuildingCount(); ++index) {
            const int instanceId = world_->getBuildingInstanceAt(index);
            const auto found = world_->buildings().find(instanceId);
            if (found != world_->buildings().end()) instances.push_back(instanceValue(snapshot(found->second)));
        }
    if (world_)
        for (building::EdgeCurveGroupId groupId : world_->edgeCurveGroupIds()) {
            auto group = world_->edgeCurveGroup(groupId);
            if (!group.ok()) continue;
            std::vector<BuildingInstanceSnapshot> members;
            for (int instanceId : group.value().instanceIds) {
                const auto found = world_->buildings().find(instanceId);
                if (found != world_->buildings().end()) members.push_back(snapshot(found->second));
            }
            curveGroups.push_back(edgeCurveValue(group.value(), members));
        }
    return EditorValue::Object{{"schemaVersion", int64_t{8}},
                               {"worldId", world_ ? world_->getId() : std::string{}},
                               {"instances", std::move(instances)},
                               {"edgeCurveGroups", std::move(curveGroups)}};
}

}  // namespace eve::building_editing
