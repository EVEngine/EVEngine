#include "building_editing/BuildingTarget.h"

#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"

#include <algorithm>
#include <set>
#include <utility>

namespace eve::building_editing {
namespace {

template <class T>
EditorResult<T> buildingError(EditorStatus status, std::string rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(std::move(rule)), std::move(message));
}

EditorValue stringArray(const std::vector<std::string>& values) {
    EditorValue::Array result;
    for (const std::string& value : values) result.emplace_back(value);
    return result;
}

std::vector<std::string> parseStringArray(const EditorValue& value, bool& valid) {
    std::vector<std::string> result;
    const auto* values = value.getIf<EditorValue::Array>();
    if (!values) {
        valid = false;
        return result;
    }
    for (const EditorValue& item : *values) {
        const auto* text = item.getIf<std::string>();
        if (!text) {
            valid = false;
            return {};
        }
        result.push_back(*text);
    }
    return result;
}

EditorValue instanceValue(const BuildingInstanceSnapshot& instance) {
    EditorValue::Object properties;
    for (const auto& [key, value] : instance.properties) properties[key] = value;
    EditorValue::Array garrison;
    for (const BuildingGarrisonMemberRecord& member : instance.garrison)
        garrison.emplace_back(EditorValue::Object{{"id", member.id}, {"type", member.type},
                                                  {"tags", stringArray(member.tags)}});
    return EditorValue::Object{{"instanceId", int64_t{instance.instanceId}},
                               {"buildingId", instance.buildingId},
                               {"cellX", int64_t{instance.cellX}}, {"cellY", int64_t{instance.cellY}},
                               {"worldX", instance.worldX}, {"worldY", instance.worldY},
                               {"elevation", instance.elevation}, {"rotationDegrees", instance.rotationDegrees},
                               {"channel", instance.channel}, {"properties", std::move(properties)},
                               {"tags", stringArray(instance.tags)}, {"garrison", std::move(garrison)},
                               {"garrisonRevision", static_cast<int64_t>(instance.garrisonRevision)}};
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

EditorResult<BuildingInstanceSnapshot> parseInstance(const EditorValue& value) {
    const auto integer = [&](const char* key) -> const int64_t* {
        const EditorValue* entry = field(value, key);
        return entry ? entry->getIf<int64_t>() : nullptr;
    };
    const auto number = [&](const char* key) -> const double* {
        const EditorValue* entry = field(value, key);
        return entry ? entry->getIf<double>() : nullptr;
    };
    const auto string = [&](const char* key) -> const std::string* {
        const EditorValue* entry = field(value, key);
        return entry ? entry->getIf<std::string>() : nullptr;
    };
    const auto* instanceId = integer("instanceId");
    const auto* buildingId = string("buildingId");
    const auto* cellX = integer("cellX");
    const auto* cellY = integer("cellY");
    const auto* worldX = number("worldX");
    const auto* worldY = number("worldY");
    const auto* elevation = number("elevation");
    const auto* rotation = number("rotationDegrees");
    const auto* channel = string("channel");
    const auto* propertiesValue = field(value, "properties");
    const auto* tagsValue = field(value, "tags");
    const auto* garrisonValue = field(value, "garrison");
    const auto* garrisonRevision = integer("garrisonRevision");
    const auto* properties = propertiesValue ? propertiesValue->getIf<EditorValue::Object>() : nullptr;
    const auto* garrison = garrisonValue ? garrisonValue->getIf<EditorValue::Array>() : nullptr;
    if (!instanceId || *instanceId <= 0 || !buildingId || buildingId->empty() || !cellX || !cellY || !worldX ||
        !worldY || !elevation || !rotation || !channel || !properties || !tagsValue || !garrison ||
        !garrisonRevision || *garrisonRevision < 0)
        return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                       "editor.building.invalid-instance",
                                                       "Building instance payload is incomplete");
    BuildingInstanceSnapshot result;
    result.instanceId = static_cast<int>(*instanceId);
    result.buildingId = *buildingId;
    result.cellX = static_cast<int>(*cellX);
    result.cellY = static_cast<int>(*cellY);
    result.worldX = *worldX;
    result.worldY = *worldY;
    result.elevation = *elevation;
    result.rotationDegrees = *rotation;
    result.channel = *channel;
    result.garrisonRevision = static_cast<std::uint64_t>(*garrisonRevision);
    for (const auto& [key, property] : *properties) {
        const auto* text = property.getIf<std::string>();
        if (!text)
            return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                           "editor.building.invalid-property",
                                                           "Building properties must be strings");
        result.properties[key] = *text;
    }
    bool valid = true;
    result.tags = parseStringArray(*tagsValue, valid);
    if (!valid)
        return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                       "editor.building.invalid-tags",
                                                       "Building tags must be strings");
    std::set<std::string> memberIds;
    for (const EditorValue& memberValue : *garrison) {
        const auto* idValue = field(memberValue, "id");
        const auto* typeValue = field(memberValue, "type");
        const auto* memberTags = field(memberValue, "tags");
        const auto* id = idValue ? idValue->getIf<std::string>() : nullptr;
        const auto* type = typeValue ? typeValue->getIf<std::string>() : nullptr;
        if (!id || id->empty() || !memberIds.insert(*id).second || !type || type->empty() || !memberTags)
            return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                           "editor.building.invalid-garrison",
                                                           "Garrison members require unique ids, type and tags");
        auto tags = parseStringArray(*memberTags, valid);
        if (!valid)
            return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                           "editor.building.invalid-garrison-tags",
                                                           "Garrison member tags must be strings");
        result.garrison.push_back({*id, *type, std::move(tags)});
    }
    return EditorResult<BuildingInstanceSnapshot>::applied(std::move(result));
}

BuildingInstanceSnapshot snapshot(const building::PlacedBuilding& placed) {
    BuildingInstanceSnapshot result;
    result.instanceId = placed.instanceId;
    result.buildingId = placed.buildingId;
    result.cellX = placed.originCellX;
    result.cellY = placed.originCellY;
    result.worldX = placed.worldX;
    result.worldY = placed.worldY;
    result.elevation = placed.elevation;
    result.rotationDegrees = placed.rotationDeg;
    result.channel = placed.channel;
    result.properties.insert(placed.props.begin(), placed.props.end());
    result.tags = placed.tags;
    for (const building::GarrisonMember& member : placed.garrison)
        result.garrison.push_back({member.id, member.type, member.tags});
    result.garrisonRevision = placed.garrisonRevision.value();
    return result;
}

building::PlacedBuilding runtime(const BuildingInstanceSnapshot& value) {
    building::PlacedBuilding result;
    result.instanceId = value.instanceId;
    result.buildingId = value.buildingId;
    result.originCellX = value.cellX;
    result.originCellY = value.cellY;
    result.worldX = static_cast<float>(value.worldX);
    result.worldY = static_cast<float>(value.worldY);
    result.elevation = static_cast<float>(value.elevation);
    result.rotationDeg = static_cast<float>(value.rotationDegrees);
    result.channel = value.channel;
    result.props.insert(value.properties.begin(), value.properties.end());
    result.tags = value.tags;
    for (const BuildingGarrisonMemberRecord& member : value.garrison)
        result.garrison.push_back({member.id, member.type, member.tags});
    result.garrisonRevision = eve::Revision(value.garrisonRevision);
    return result;
}

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

}  // namespace

BuildingPlacementTarget::BuildingPlacementTarget(std::string id, building::PlacementWorld* world)
    : id_(std::move(id)), world_(world) {}

TargetDescriptor BuildingPlacementTarget::describe() const {
    TargetDescriptor result;
    result.id = TargetId(id_);
    result.type = "building-placement-world";
    result.revision = revision_;
    result.readOnly = world_ == nullptr;
    result.capabilities = {editorCapabilityId()};
    return result;
}

void* BuildingPlacementTarget::queryCapability(const CapabilityId& capability) {
    return capability == editorCapabilityId() ? this : nullptr;
}

EditorResult<BuildingInstanceSnapshot> BuildingPlacementTarget::instance(int instanceId) const {
    if (!world_)
        return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                       "editor.building.world-required",
                                                       "Placement world is unavailable");
    const auto found = world_->buildings().find(instanceId);
    if (found == world_->buildings().end())
        return buildingError<BuildingInstanceSnapshot>(EditorStatus::NotFound,
                                                       "editor.building.instance-not-found",
                                                       "Placed building instance was not found");
    return EditorResult<BuildingInstanceSnapshot>::applied(snapshot(found->second));
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
    result.worldRevision = revision_;
    if (!world_) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back({RuleId("editor.building.world-required"), DiagnosticSeverity::Error,
                                      "Placement world is unavailable"});
        return result;
    }
    building::PlacementSystem::ensureBuiltins();
    const building::BuildingDefinition* definition = building::BuildingRegistry::find(buildingId);
    if (!definition) {
        result.status = EditorStatus::NotFound;
        result.diagnostics.push_back({RuleId("editor.building.unknown-building"), DiagnosticSeverity::Error,
                                      "Building definition was not found: " + buildingId});
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
        result.diagnostics.push_back({RuleId("editor.building." + (reason.empty() ? "rejected" : reason)),
                                      DiagnosticSeverity::Error,
                                      "Building footprint validation rejected placement: " + reason});
    return result;
}

EditorResult<DomainOperation> BuildingPlacementTarget::makePlace(
    const BuildingInstanceSnapshot& placed) const {
    if (!world_ || placed.instanceId <= 0 || world_->hasBuilding(placed.instanceId))
        return buildingError<DomainOperation>(EditorStatus::Conflict, "editor.building.instance-id-conflict",
                                              "Exact placement instance id is unavailable");
    std::string reason;
    if (!building::PlacementSystem::canPlaceElev(world_, placed.buildingId, placed.cellX, placed.cellY,
                                                  static_cast<float>(placed.elevation),
                                                  static_cast<float>(placed.rotationDegrees), 0, &reason))
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building." + (reason.empty() ? "rejected" : reason),
                                              "Building footprint validation rejected placement: " + reason);
    return EditorResult<DomainOperation>::applied(instanceOperation(
        id_, "building.instance.set.v1", "building.instance.delete.v1", placed, placed));
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeMove(int instanceId, int cellX, int cellY,
                                                                double rotationDegrees) const {
    auto current = instance(instanceId);
    if (!current.value) return buildingError<DomainOperation>(current.status, "editor.building.instance-not-found",
                                                              "Placed building instance was not found");
    BuildingInstanceSnapshot desired = *current.value;
    desired.cellX = cellX;
    desired.cellY = cellY;
    desired.rotationDegrees = building::PlacementSystem::normalizeRotation(
        desired.buildingId, static_cast<float>(rotationDegrees));
    float worldX = 0.f;
    float worldY = 0.f;
    world_->cellToWorldPlane(cellX, cellY, worldX, worldY);
    desired.worldX = worldX;
    desired.worldY = worldY;
    std::string reason;
    if (!building::PlacementSystem::canPlaceElev(world_, desired.buildingId, cellX, cellY,
                                                  static_cast<float>(desired.elevation),
                                                  static_cast<float>(desired.rotationDegrees), instanceId, &reason))
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building." + (reason.empty() ? "rejected" : reason),
                                              "Building footprint validation rejected move: " + reason);
    return EditorResult<DomainOperation>::applied(instanceOperation(
        id_, "building.instance.set.v1", "building.instance.set.v1", desired, *current.value));
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeRemove(int instanceId) const {
    auto current = instance(instanceId);
    if (!current.value) return buildingError<DomainOperation>(current.status, "editor.building.instance-not-found",
                                                              "Placed building instance was not found");
    return EditorResult<DomainOperation>::applied(instanceOperation(
        id_, "building.instance.delete.v1", "building.instance.set.v1", *current.value, *current.value));
}

EditorResult<void> BuildingPlacementTarget::applyDomainOperation(const DomainOperation& operation) {
    if (!world_)
        return buildingError<void>(EditorStatus::Rejected, "editor.building.world-required",
                                   "Placement world is unavailable");
    if (operation.target != TargetId(id_))
        return buildingError<void>(EditorStatus::Rejected, "editor.building.target-mismatch",
                                   "Building operation targets another world");
    auto parsed = parseInstance(operation.payload);
    if (!parsed.value)
        return buildingError<void>(parsed.status, "editor.building.invalid-instance",
                                   "Building operation payload is invalid");
    if (operation.type == "building.instance.delete.v1") {
        if (!world_->hasBuilding(parsed.value->instanceId))
            return buildingError<void>(EditorStatus::NotFound, "editor.building.instance-not-found",
                                       "Placed building instance was not found");
        if (!building::PlacementSystem::removeBuilding(world_, parsed.value->instanceId))
            return buildingError<void>(EditorStatus::Failed, "editor.building.remove-failed",
                                       "PlacementSystem rejected building removal");
    } else if (operation.type == "building.instance.set.v1") {
        const building::PlacedBuilding desired = runtime(*parsed.value);
        if (!world_->hasBuilding(desired.instanceId)) {
            std::string reason;
            if (building::PlacementSystem::restoreExact(world_, desired, &reason) !=
                building::PlacementRestoreStatus::Restored)
                return buildingError<void>(EditorStatus::Rejected,
                                           "editor.building." + (reason.empty() ? "restore-failed" : reason),
                                           "PlacementSystem rejected exact instance restore: " + reason);
        } else {
            const auto current = world_->buildings().find(desired.instanceId);
            if (current->second.buildingId != desired.buildingId)
                return buildingError<void>(EditorStatus::Conflict, "editor.building.type-change-unsupported",
                                           "Building instance type cannot change during move");
            if (!building::PlacementSystem::moveBuilding(world_, desired.instanceId, desired.originCellX,
                                                          desired.originCellY, desired.rotationDeg))
                return buildingError<void>(EditorStatus::Rejected, "editor.building.move-failed",
                                           "PlacementSystem rejected building movement");
            building::PlacedBuilding& updated = world_->buildings().at(desired.instanceId);
            updated.worldX = desired.worldX;
            updated.worldY = desired.worldY;
            updated.elevation = desired.elevation;
            updated.props = desired.props;
            updated.tags = desired.tags;
            updated.garrison = desired.garrison;
            updated.garrisonRevision = desired.garrisonRevision;
        }
    } else {
        return buildingError<void>(EditorStatus::Unsupported, "editor.building.operation-unsupported",
                                   "Building placement operation is unsupported: " + operation.type);
    }
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

EditorValue BuildingPlacementTarget::snapshotValue() const {
    EditorValue::Array instances;
    if (world_)
        for (int index = 0; index < world_->getBuildingCount(); ++index) {
            const int instanceId = world_->getBuildingInstanceAt(index);
            const auto found = world_->buildings().find(instanceId);
            if (found != world_->buildings().end()) instances.push_back(instanceValue(snapshot(found->second)));
        }
    return EditorValue::Object{{"schemaVersion", int64_t{1}},
                               {"worldId", world_ ? world_->getId() : std::string{}},
                               {"instances", std::move(instances)}};
}

}  // namespace eve::building_editing
