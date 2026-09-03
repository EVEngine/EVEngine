#include "building_editing/BuildingTarget.h"

#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace eve::building_editing {
namespace {

template <class T>
EditorResult<T> buildingError(EditorStatus status, std::string rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(std::move(rule)), std::move(message));
}

EditorValue stringArray(const std::vector<std::string>& values) {
    EditorValue::Array result;
    for (const std::string& value : values) result.emplace_back(value);
    return result;
}

EditorValue numberArray(const std::vector<double>& values) {
    EditorValue::Array result;
    for (double value : values) result.emplace_back(value);
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
                               {"placementKind", instance.placementKind},
                               {"edgeX", int64_t{instance.edgeX}}, {"edgeY", int64_t{instance.edgeY}},
                               {"edgeAxis", instance.edgeAxis},
                               {"cornerX", int64_t{instance.cornerX}},
                               {"cornerY", int64_t{instance.cornerY}},
                               {"freeRadius", instance.freeRadius},
                               {"freeHalfWidth", instance.freeHalfWidth},
                               {"freeHalfHeight", instance.freeHalfHeight},
                               {"freeFootprintVertices", numberArray(instance.freeFootprintVertices)},
                               {"cellX", int64_t{instance.cellX}}, {"cellY", int64_t{instance.cellY}},
                               {"level", int64_t{instance.level}},
                               {"worldX", instance.worldX}, {"worldY", instance.worldY},
                               {"elevation", instance.elevation}, {"surfaceId", instance.surfaceId},
                               {"surfaceRevision", static_cast<int64_t>(instance.surfaceRevision)},
                               {"surfaceNormalX", instance.surfaceNormalX},
                               {"surfaceNormalY", instance.surfaceNormalY},
                               {"surfaceNormalZ", instance.surfaceNormalZ},
                               {"surfaceTangentX", instance.surfaceTangentX},
                               {"surfaceTangentY", instance.surfaceTangentY},
                               {"surfaceTangentZ", instance.surfaceTangentZ},
                               {"surfaceSampleCount", int64_t{instance.surfaceSampleCount}},
                               {"surfaceMaxSlopeDegrees", instance.surfaceMaxSlopeDegrees},
                               {"surfaceHeightDelta", instance.surfaceHeightDelta},
                               {"rotationDegrees", instance.rotationDegrees},
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
    if (const auto* placementKind = string("placementKind")) result.placementKind = *placementKind;
    if (const auto* edgeX = integer("edgeX")) result.edgeX = static_cast<int>(*edgeX);
    if (const auto* edgeY = integer("edgeY")) result.edgeY = static_cast<int>(*edgeY);
    if (const auto* edgeAxis = string("edgeAxis")) result.edgeAxis = *edgeAxis;
    if (const auto* cornerX = integer("cornerX")) result.cornerX = static_cast<int>(*cornerX);
    if (const auto* cornerY = integer("cornerY")) result.cornerY = static_cast<int>(*cornerY);
    if (const auto* freeRadius = number("freeRadius")) result.freeRadius = *freeRadius;
    if (const auto* freeHalfWidth = number("freeHalfWidth"))
        result.freeHalfWidth = *freeHalfWidth;
    if (const auto* freeHalfHeight = number("freeHalfHeight"))
        result.freeHalfHeight = *freeHalfHeight;
    if (const EditorValue* polygon = field(value, "freeFootprintVertices")) {
        const auto* vertices = polygon->getIf<EditorValue::Array>();
        if (!vertices)
            return buildingError<BuildingInstanceSnapshot>(
                EditorStatus::Rejected, "editor.building.invalid-free-footprint",
                "Free footprint vertices must be a numeric array");
        for (const EditorValue& vertex : *vertices) {
            const auto* coordinate = vertex.getIf<double>();
            if (!coordinate)
                return buildingError<BuildingInstanceSnapshot>(
                    EditorStatus::Rejected, "editor.building.invalid-free-footprint",
                    "Free footprint vertices must contain only numbers");
            result.freeFootprintVertices.push_back(*coordinate);
        }
    }
    result.cellX = static_cast<int>(*cellX);
    result.cellY = static_cast<int>(*cellY);
    if (const auto* level = integer("level")) result.level = static_cast<int>(*level);
    result.worldX = *worldX;
    result.worldY = *worldY;
    result.elevation = *elevation;
    if (const auto* surfaceId = string("surfaceId")) result.surfaceId = *surfaceId;
    if (const auto* surfaceRevision = integer("surfaceRevision")) {
        if (*surfaceRevision < 0)
            return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                           "editor.building.invalid-surface-revision",
                                                           "Surface revision must not be negative");
        result.surfaceRevision = static_cast<std::uint64_t>(*surfaceRevision);
    }
    if (const auto* n = number("surfaceNormalX")) result.surfaceNormalX = *n;
    if (const auto* n = number("surfaceNormalY")) result.surfaceNormalY = *n;
    if (const auto* n = number("surfaceNormalZ")) result.surfaceNormalZ = *n;
    if (const auto* n = number("surfaceTangentX")) result.surfaceTangentX = *n;
    if (const auto* n = number("surfaceTangentY")) result.surfaceTangentY = *n;
    if (const auto* n = number("surfaceTangentZ")) result.surfaceTangentZ = *n;
    if (const auto* count = integer("surfaceSampleCount")) result.surfaceSampleCount = static_cast<int>(*count);
    if (const auto* slope = number("surfaceMaxSlopeDegrees")) result.surfaceMaxSlopeDegrees = *slope;
    if (const auto* delta = number("surfaceHeightDelta")) result.surfaceHeightDelta = *delta;
    if (result.placementKind != "cell" && result.placementKind != "edge" &&
        result.placementKind != "corner" && result.placementKind != "free")
        return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                       "editor.building.invalid-placement-kind",
                                                       "Placement kind must be cell, edge, corner, or free");
    if (result.placementKind == "edge" && result.edgeAxis != "horizontal" &&
        result.edgeAxis != "vertical")
        return buildingError<BuildingInstanceSnapshot>(EditorStatus::Rejected,
                                                       "editor.building.invalid-edge-axis",
                                                       "Edge axis must be horizontal or vertical");
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
    return eve::editing::applied<BuildingInstanceSnapshot>(std::move(result));
}

EditorResult<std::vector<BuildingInstanceSnapshot>> parseInstances(const EditorValue& value) {
    const auto* values = value.getIf<EditorValue::Array>();
    if (!values || values->empty())
        return buildingError<std::vector<BuildingInstanceSnapshot>>(
            EditorStatus::Rejected, "editor.building.invalid-area-payload",
            "Building area payload must contain at least one instance");
    std::vector<BuildingInstanceSnapshot> result;
    std::set<int> ids;
    result.reserve(values->size());
    for (const EditorValue& item : *values) {
        auto parsed = parseInstance(item);
        if (!parsed.ok())
            return buildingError<std::vector<BuildingInstanceSnapshot>>(
                parsed.code(), "editor.building.invalid-area-instance",
                "Building area payload contains an invalid instance");
        if (!ids.insert(parsed.value().instanceId).second)
            return buildingError<std::vector<BuildingInstanceSnapshot>>(
                EditorStatus::Rejected, "editor.building.duplicate-area-instance",
                "Building area payload contains duplicate instance ids");
        result.push_back(std::move(parsed).takeValue());
    }
    return eve::editing::applied<std::vector<BuildingInstanceSnapshot>>(std::move(result));
}

struct EdgeCurvePayload {
    building::EdgeCurveGroup group;
    std::vector<BuildingInstanceSnapshot> instances;
};

EditorValue edgeCurveValue(const building::EdgeCurveGroup& group,
                           const std::vector<BuildingInstanceSnapshot>& instances) {
    std::vector<double> controls;
    for (const building::EdgeCurveControlPoint& point : group.controlPoints) {
        controls.push_back(point.x);
        controls.push_back(point.y);
    }
    EditorValue::Array values;
    for (const BuildingInstanceSnapshot& instance : instances)
        values.push_back(instanceValue(instance));
    std::vector<double> surfaceSamples;
    for (const building::EdgeCurveSurfaceSample& sample : group.surfaceSamples) {
        surfaceSamples.insert(surfaceSamples.end(),
                              {sample.worldX, sample.worldY, sample.worldZ, sample.normalX,
                               sample.normalY, sample.normalZ});
    }
    return EditorValue::Object{{"groupId", static_cast<int64_t>(group.id.value)},
                               {"buildingId", group.buildingId},
                               {"level", int64_t{group.level}},
                               {"subdivisions", int64_t{group.subdivisions}},
                               {"controlPoints", numberArray(controls)},
                               {"surfaceProviderName", group.surfaceProviderName},
                               {"surfaceId", group.surfaceId},
                               {"surfaceRevision", static_cast<int64_t>(group.surfaceRevision)},
                               {"surfaceSamples", numberArray(surfaceSamples)},
                               {"instances", std::move(values)}};
}

EditorResult<EdgeCurvePayload> parseEdgeCurve(const EditorValue& value) {
    const EditorValue* groupIdValue = field(value, "groupId");
    const EditorValue* buildingIdValue = field(value, "buildingId");
    const EditorValue* levelValue = field(value, "level");
    const EditorValue* subdivisionsValue = field(value, "subdivisions");
    const EditorValue* controlsValue = field(value, "controlPoints");
    const EditorValue* instancesValue = field(value, "instances");
    const auto* groupId = groupIdValue ? groupIdValue->getIf<int64_t>() : nullptr;
    const auto* buildingId =
        buildingIdValue ? buildingIdValue->getIf<std::string>() : nullptr;
    const auto* level = levelValue ? levelValue->getIf<int64_t>() : nullptr;
    const auto* subdivisions =
        subdivisionsValue ? subdivisionsValue->getIf<int64_t>() : nullptr;
    const auto* controls = controlsValue ? controlsValue->getIf<EditorValue::Array>() : nullptr;
    if (!groupId || *groupId <= 0 || !buildingId || buildingId->empty() || !level ||
        !subdivisions || *subdivisions < 2 || *subdivisions > 4096 || !controls ||
        controls->size() != 8 || !instancesValue)
        return buildingError<EdgeCurvePayload>(EditorStatus::Rejected,
                                               "editor.building.invalid-edge-curve",
                                               "Edge curve payload is incomplete");
    auto instances = parseInstances(*instancesValue);
    if (!instances.ok())
        return buildingError<EdgeCurvePayload>(instances.code(),
                                               "editor.building.invalid-edge-curve-instances",
                                               "Edge curve instances are invalid");
    EdgeCurvePayload result;
    result.group.id = {static_cast<std::uint64_t>(*groupId)};
    result.group.buildingId = *buildingId;
    result.group.level = static_cast<int>(*level);
    result.group.subdivisions = static_cast<int>(*subdivisions);
    if (const EditorValue* provider = field(value, "surfaceProviderName"))
        if (const auto* text = provider->getIf<std::string>())
            result.group.surfaceProviderName = *text;
    if (const EditorValue* surface = field(value, "surfaceId"))
        if (const auto* text = surface->getIf<std::string>()) result.group.surfaceId = *text;
    if (const EditorValue* revision = field(value, "surfaceRevision")) {
        const auto* integer = revision->getIf<int64_t>();
        if (!integer || *integer < 0)
            return buildingError<EdgeCurvePayload>(
                EditorStatus::Rejected, "editor.building.invalid-edge-curve-surface-revision",
                "Edge curve surface revision is invalid");
        result.group.surfaceRevision = static_cast<std::uint64_t>(*integer);
    }
    if (const EditorValue* sampleValue = field(value, "surfaceSamples")) {
        const auto* samples = sampleValue->getIf<EditorValue::Array>();
        if (!samples || samples->size() % 6 != 0)
            return buildingError<EdgeCurvePayload>(
                EditorStatus::Rejected, "editor.building.invalid-edge-curve-surface-samples",
                "Edge curve surface samples must contain position and normal sextuples");
        for (size_t index = 0; index < samples->size(); index += 6) {
            std::array<float, 6> frame{};
            for (size_t component = 0; component < 6; ++component) {
                const auto* number = (*samples)[index + component].getIf<double>();
                if (!number)
                    return buildingError<EdgeCurvePayload>(
                        EditorStatus::Rejected,
                        "editor.building.invalid-edge-curve-surface-samples",
                        "Edge curve surface samples must be numeric");
                frame[component] = static_cast<float>(*number);
            }
            result.group.surfaceSamples.push_back(
                {frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]});
        }
    }
    for (size_t index = 0; index < controls->size(); index += 2) {
        const auto* x = (*controls)[index].getIf<double>();
        const auto* y = (*controls)[index + 1].getIf<double>();
        if (!x || !y)
            return buildingError<EdgeCurvePayload>(EditorStatus::Rejected,
                                                   "editor.building.invalid-edge-curve-controls",
                                                   "Edge curve controls must be numbers");
        result.group.controlPoints.push_back(
            {static_cast<float>(*x), static_cast<float>(*y)});
    }
    result.instances = std::move(instances).takeValue();
    for (const BuildingInstanceSnapshot& instance : result.instances)
        result.group.instanceIds.push_back(instance.instanceId);
    return eve::editing::applied<EdgeCurvePayload>(std::move(result));
}

BuildingInstanceSnapshot snapshot(const building::PlacedBuilding& placed) {
    BuildingInstanceSnapshot result;
    result.instanceId = placed.instanceId;
    result.buildingId = placed.buildingId;
    result.placementKind = placed.placementKind;
    result.edgeX = placed.edge.x;
    result.edgeY = placed.edge.y;
    result.edgeAxis = placed.edge.axis == building::EdgeAxis::Horizontal ? "horizontal" : "vertical";
    result.cornerX = placed.corner.x;
    result.cornerY = placed.corner.y;
    result.freeRadius = placed.freeRadius;
    result.freeHalfWidth = placed.freeHalfWidth;
    result.freeHalfHeight = placed.freeHalfHeight;
    for (float coordinate : placed.freeFootprintVertices)
        result.freeFootprintVertices.push_back(coordinate);
    result.cellX = placed.originCellX;
    result.cellY = placed.originCellY;
    result.level = placed.level;
    result.worldX = placed.worldX;
    result.worldY = placed.worldY;
    result.elevation = placed.elevation;
    result.surfaceId = placed.surfaceId;
    result.surfaceRevision = placed.surfaceRevision;
    result.surfaceNormalX = placed.surfaceNormalX;
    result.surfaceNormalY = placed.surfaceNormalY;
    result.surfaceNormalZ = placed.surfaceNormalZ;
    result.surfaceTangentX = placed.surfaceTangentX;
    result.surfaceTangentY = placed.surfaceTangentY;
    result.surfaceTangentZ = placed.surfaceTangentZ;
    result.surfaceSampleCount = placed.surfaceSampleCount;
    result.surfaceMaxSlopeDegrees = placed.surfaceMaxSlopeDegrees;
    result.surfaceHeightDelta = placed.surfaceHeightDelta;
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
    result.placementKind = value.placementKind;
    result.edge = {value.edgeX, value.edgeY,
                   value.edgeAxis == "vertical" ? building::EdgeAxis::Vertical
                                                : building::EdgeAxis::Horizontal};
    result.corner = {value.cornerX, value.cornerY};
    result.freeRadius = static_cast<float>(value.freeRadius);
    result.freeHalfWidth = static_cast<float>(value.freeHalfWidth);
    result.freeHalfHeight = static_cast<float>(value.freeHalfHeight);
    for (double coordinate : value.freeFootprintVertices)
        result.freeFootprintVertices.push_back(static_cast<float>(coordinate));
    result.originCellX = value.cellX;
    result.originCellY = value.cellY;
    result.level = value.level;
    result.worldX = static_cast<float>(value.worldX);
    result.worldY = static_cast<float>(value.worldY);
    result.elevation = static_cast<float>(value.elevation);
    result.surfaceId = value.surfaceId;
    result.surfaceRevision = value.surfaceRevision;
    result.surfaceNormalX = static_cast<float>(value.surfaceNormalX);
    result.surfaceNormalY = static_cast<float>(value.surfaceNormalY);
    result.surfaceNormalZ = static_cast<float>(value.surfaceNormalZ);
    result.surfaceTangentX = static_cast<float>(value.surfaceTangentX);
    result.surfaceTangentY = static_cast<float>(value.surfaceTangentY);
    result.surfaceTangentZ = static_cast<float>(value.surfaceTangentZ);
    result.surfaceSampleCount = value.surfaceSampleCount;
    result.surfaceMaxSlopeDegrees = static_cast<float>(value.surfaceMaxSlopeDegrees);
    result.surfaceHeightDelta = static_cast<float>(value.surfaceHeightDelta);
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

EditorResult<DomainOperation> areaOperation(
    const std::string& target, building::PlacementWorld* world,
    const building::PlacementSystem::AreaPreview& preview) {
    if (!world || preview.cells.empty() || preview.rejectedCount != 0 ||
        preview.acceptedCount != static_cast<int>(preview.cells.size()))
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.area-rejected",
                                              "Area placement requires a fully accepted preview");
    const building::BuildingDefinition* definition =
        building::BuildingRegistry::find(preview.buildingId);
    if (!definition || definition->placementKind == "edge")
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.area-definition-invalid",
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

}  // namespace

BuildingPlacementTarget::BuildingPlacementTarget(std::string id, building::PlacementWorld* world)
    : id_(std::move(id)), world_(world) {}

BuildingPlacementTarget::BuildingPlacementTarget(
    std::string id, std::unique_ptr<building::PlacementWorld> world, unsigned long long revision)
    : id_(std::move(id)), world_(world.get()), ownedWorld_(std::move(world)), revision_(revision) {}

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
    result.worldRevision = revision_;
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

EditorResult<DomainOperation> BuildingPlacementTarget::makePlace(
    const BuildingInstanceSnapshot& placed) const {
    if (!world_ || placed.instanceId <= 0 || world_->hasBuilding(placed.instanceId))
        return buildingError<DomainOperation>(EditorStatus::Conflict, "editor.building.instance-id-conflict",
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
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building." + (reason.empty() ? "rejected" : reason),
                                              "Building footprint validation rejected placement: " + reason);
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.set.v4", "building.instance.delete.v4", placed, placed));
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeMove(int instanceId, int cellX, int cellY,
                                                                double rotationDegrees) const {
    auto current = instance(instanceId);
    if (!current.ok()) return buildingError<DomainOperation>(current.code(), "editor.building.instance-not-found",
                                                              "Placed building instance was not found");
    BuildingInstanceSnapshot desired = current.value();
    if (desired.placementKind == "free")
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.free-move-requires-world-anchor",
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
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building." + (reason.empty() ? "rejected" : reason),
                                              "Building footprint validation rejected move: " + reason);
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.set.v4", "building.instance.set.v4", desired, current.value()));
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeMoveFree(
    int instanceId, double worldX, double worldY, double elevation,
    double rotationDegrees) const {
    auto current = instance(instanceId);
    if (!current.ok())
        return buildingError<DomainOperation>(current.code(),
                                              "editor.building.instance-not-found",
                                              "Placed building instance was not found");
    if (current.value().placementKind != "free")
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.not-free-instance",
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
        return buildingError<DomainOperation>(
            EditorStatus::Rejected,
            "editor.building." + (reason.empty() ? "rejected" : reason),
            "Free building validation rejected move: " + reason);
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.set.v4", "building.instance.set.v4", desired,
        current.value()));
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeReplace(
    int instanceId, const std::string& replacementBuildingId) const {
    auto current = instance(instanceId);
    if (!current.ok())
        return buildingError<DomainOperation>(current.code(), "editor.building.instance-not-found",
                                              "Placed building instance was not found");
    const building::BuildingDefinition* replacement =
        building::BuildingRegistry::find(replacementBuildingId);
    if (!replacement)
        return buildingError<DomainOperation>(EditorStatus::NotFound,
                                              "editor.building.replacement-not-found",
                                              "Replacement building definition was not found");
    const auto source = world_->buildings().find(instanceId);
    if (source == world_->buildings().end())
        return buildingError<DomainOperation>(EditorStatus::NotFound,
                                              "editor.building.instance-not-found",
                                              "Placed building instance was not found");
    const std::string& sourceKind = source->second.placementKind;
    if (sourceKind != replacement->placementKind)
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.replace-domain-mismatch",
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
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building." +
                                                  (reason.empty() ? "rejected" : reason),
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

EditorResult<DomainOperation> BuildingPlacementTarget::makeRectangle(
    const std::string& buildingId, int minCellX, int minCellY, int maxCellX, int maxCellY,
    double rotationDegrees) const {
    auto preview = building::PlacementSystem::previewRectangle(
        world_, buildingId, minCellX, minCellY, maxCellX, maxCellY,
        static_cast<float>(rotationDegrees));
    if (!preview.ok())
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.area-preview-failed",
                                              "Rectangle placement preview failed");
    return areaOperation(id_, world_, preview.value());
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeBrush(
    const std::string& buildingId, int centerCellX, int centerCellY, int radius,
    double rotationDegrees) const {
    auto preview = building::PlacementSystem::previewBrush(
        world_, buildingId, centerCellX, centerCellY, radius,
        static_cast<float>(rotationDegrees));
    if (!preview.ok())
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.area-preview-failed",
                                              "Brush placement preview failed");
    return areaOperation(id_, world_, preview.value());
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeEdgePath(
    const std::string& buildingId,
    const std::vector<BuildingEdgePathVertex>& vertices) const {
    std::vector<building::CornerAddress> runtimeVertices;
    runtimeVertices.reserve(vertices.size());
    for (const BuildingEdgePathVertex& vertex : vertices)
        runtimeVertices.push_back({vertex.x, vertex.y});
    auto preview = building::PlacementSystem::previewEdgePath(world_, buildingId,
                                                              runtimeVertices);
    if (!preview.ok())
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.edge-path-rejected",
                                              "Edge path preview was rejected");
    const building::BuildingDefinition* definition =
        building::BuildingRegistry::find(buildingId);
    if (!definition)
        return buildingError<DomainOperation>(EditorStatus::NotFound,
                                              "editor.building.definition-not-found",
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

EditorResult<DomainOperation> BuildingPlacementTarget::makeEdgeCubicBezier(
    const std::string& buildingId,
    const std::vector<BuildingEdgeCurvePoint>& controlPoints,
    int subdivisions) const {
    std::vector<building::PlacementSystem::EdgeCurvePoint> runtimeControls;
    runtimeControls.reserve(controlPoints.size());
    for (const BuildingEdgeCurvePoint& point : controlPoints)
        runtimeControls.push_back(
            {static_cast<float>(point.x), static_cast<float>(point.y)});
    auto vertices =
        building::PlacementSystem::sampleEdgeCubicBezier(runtimeControls, subdivisions);
    if (!vertices.ok())
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.edge-curve-rejected",
                                              "Cubic edge curve sampling was rejected");
    std::vector<BuildingEdgePathVertex> editorVertices;
    editorVertices.reserve(vertices.value().size());
    for (const building::CornerAddress& vertex : vertices.value())
        editorVertices.push_back({vertex.x, vertex.y});
    auto path = makeEdgePath(buildingId, editorVertices);
    if (!path.ok()) return path;
    auto instances = parseInstances(path.value().payload);
    if (!instances.ok())
        return buildingError<DomainOperation>(EditorStatus::Failed,
                                              "editor.building.edge-curve-encoding-failed",
                                              "Edge curve instances could not be encoded");
    building::EdgeCurveGroup group;
    group.id = world_->nextEdgeCurveGroupId();
    group.buildingId = buildingId;
    group.level = instances.value().front().level;
    group.subdivisions = subdivisions;
    for (const BuildingEdgeCurvePoint& point : controlPoints)
        group.controlPoints.push_back(
            {static_cast<float>(point.x), static_cast<float>(point.y)});
    for (const BuildingInstanceSnapshot& instance : instances.value())
        group.instanceIds.push_back(instance.instanceId);
    DomainOperation operation = std::move(path).takeValue();
    operation.type = "building.edge-curve.set.v1";
    operation.inverseType = "building.edge-curve.delete.v1";
    operation.payload = edgeCurveValue(group, instances.value());
    operation.inverse = operation.payload;
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeEdgeCubicBezierOnSurface(
    const std::string& buildingId,
    const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
    const std::string& surfaceName) const {
    if (!world_)
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.world-required",
                                              "Placement world is unavailable");
    auto operation = makeEdgeCubicBezier(buildingId, controlPoints, subdivisions);
    if (!operation.ok()) return operation;
    std::vector<building::PlacementSystem::EdgeCurvePoint> controls;
    controls.reserve(controlPoints.size());
    for (const BuildingEdgeCurvePoint& point : controlPoints)
        controls.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
    auto surface = building::PlacementSystem::sampleEdgeCurveSurface(
        *world_, surfaceName, controls, subdivisions);
    if (!surface.ok())
        return buildingError<DomainOperation>(
            EditorStatus::Rejected, "editor.building.edge-curve-surface-rejected",
            surface.error() ? surface.error()->message()
                            : "Curve could not be sampled on the requested surface");
    auto parsed = parseEdgeCurve(operation.value().payload);
    if (!parsed.ok())
        return buildingError<DomainOperation>(EditorStatus::Failed,
                                              "editor.building.edge-curve-encoding-failed",
                                              "Edge curve instances could not be encoded");
    parsed.value().group.surfaceProviderName = surface.value().providerName;
    parsed.value().group.surfaceId = surface.value().surfaceId;
    parsed.value().group.surfaceRevision = surface.value().surfaceRevision;
    parsed.value().group.surfaceSamples = surface.value().samples;
    DomainOperation enriched = std::move(operation).takeValue();
    enriched.payload = edgeCurveValue(parsed.value().group, parsed.value().instances);
    enriched.inverse = enriched.payload;
    return eve::editing::applied<DomainOperation>(std::move(enriched));
}

BuildingEdgeCurvePreview BuildingPlacementTarget::previewEdgeCubicBezier(
    const std::string& buildingId,
    const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
    int replacingMemberInstanceId, const std::string& surfaceName) const {
    BuildingEdgeCurvePreview result;
    result.worldRevision = revision_;
    result.controlPoints = controlPoints;
    if (!world_) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation,
            RuleId("editor.building.world-required"), DiagnosticSeverity::Error,
            "Placement world is unavailable"));
        return result;
    }
    std::vector<building::PlacementSystem::EdgeCurvePoint> controls;
    for (const BuildingEdgeCurvePoint& point : controlPoints)
        controls.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
    auto sampled = building::PlacementSystem::sampleEdgeCubicBezier(controls, subdivisions);
    if (!sampled.ok()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument,
            RuleId("editor.building.edge-curve-invalid"), DiagnosticSeverity::Error,
            "Cubic edge curve controls or subdivisions are invalid"));
        return result;
    }
    for (const building::CornerAddress& vertex : sampled.value())
        result.sampledVertices.push_back({vertex.x, vertex.y});
    std::unique_ptr<building::PlacementWorld> previewOwner;
    building::PlacementWorld* previewWorld = world_;
    if (replacingMemberInstanceId > 0) {
        auto replacingGroup = world_->edgeCurveGroupForInstance(replacingMemberInstanceId);
        if (!replacingGroup.ok()) {
            result.status = EditorStatus::NotFound;
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::NotFound,
                RuleId("editor.building.edge-curve-group-not-found"),
                DiagnosticSeverity::Error,
                "Replacement preview instance does not belong to a curve group"));
            return result;
        }
        previewOwner = world_->cloneState();
        for (int instanceId : replacingGroup.value().instanceIds)
            building::PlacementSystem::removeBuilding(previewOwner.get(), instanceId);
        previewWorld = previewOwner.get();
    }
    auto preview = building::PlacementSystem::previewEdgeCubicBezier(
        previewWorld, buildingId, controls, subdivisions);
    if (!preview.ok()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            preview.error() ? preview.error()->code()
                            : eve::DiagnosticCode::PreconditionViolation,
            RuleId("editor.building.edge-curve-rejected"), DiagnosticSeverity::Error,
            preview.error() ? preview.error()->message()
                            : "Cubic edge curve preview was rejected"));
        return result;
    }
    for (const building::EdgeAddress& edge : preview.value().edges)
        result.edges.push_back(
            {edge.x, edge.y,
             edge.axis == building::EdgeAxis::Horizontal ? "horizontal" : "vertical"});
    if (!surfaceName.empty()) {
        auto surface = building::PlacementSystem::sampleEdgeCurveSurface(
            *world_, surfaceName, controls, subdivisions);
        if (!surface.ok()) {
            result.status = EditorStatus::Rejected;
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                surface.error() ? surface.error()->code()
                                : eve::DiagnosticCode::PreconditionViolation,
                RuleId("editor.building.edge-curve-surface-rejected"),
                DiagnosticSeverity::Error,
                surface.error() ? surface.error()->message()
                                : "Curve surface preview was rejected"));
            return result;
        }
        result.surfaceProviderName = surface.value().providerName;
        result.surfaceId = surface.value().surfaceId;
        result.surfaceRevision = surface.value().surfaceRevision;
        for (const building::EdgeCurveSurfaceSample& sample : surface.value().samples)
            result.surfaceSamples.push_back(
                {sample.worldX, sample.worldY, sample.worldZ, sample.normalX,
                 sample.normalY, sample.normalZ});
    }
    result.status = EditorStatus::Applied;
    return result;
}

editing::GizmoSnapshot BuildingPlacementTarget::edgeCubicBezierGizmo(
    const std::string& buildingId,
    const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
    int level, int replacingMemberInstanceId, const std::string& surfaceName) const {
    editing::GizmoSnapshot gizmo;
    gizmo.target = id_;
    gizmo.targetRevision = revision_;
    const BuildingEdgeCurvePreview preview =
        previewEdgeCubicBezier(buildingId, controlPoints, subdivisions,
                               replacingMemberInstanceId, surfaceName);
    gizmo.status = preview.status;
    gizmo.diagnostics = preview.diagnostics;
    if (!world_) return gizmo;
    const auto project = [&](double gx, double gy) {
        float ox = 0.f, oy = 0.f, xx = 0.f, xy = 0.f, yx = 0.f, yy = 0.f;
        world_->cellToWorldPlane(0, 0, ox, oy);
        world_->cellToWorldPlane(1, 0, xx, xy);
        world_->cellToWorldPlane(0, 1, yx, yy);
        const double px = ox + gx * (xx - ox) + gy * (yx - ox);
        const double py = oy + gx * (xy - oy) + gy * (yy - oy);
        const double elevation = static_cast<double>(level) * world_->getFloorHeight();
        if (!surfaceName.empty()) {
            auto hit = building::PlacementSystem::sampleSurface(
                *world_, surfaceName, static_cast<float>(px), static_cast<float>(py));
            if (hit.ok())
                return std::array<double, 3>{hit.value().worldX, hit.value().worldY,
                                             hit.value().worldZ};
        }
        return world_->getGridPlaneName() == "xz"
                   ? std::array<double, 3>{px, elevation, py}
                   : std::array<double, 3>{px, py, elevation};
    };
    const auto addLine = [&](const std::string& id, const std::array<double, 3>& a,
                             const std::array<double, 3>& b,
                             const std::array<double, 4>& color, bool dashed) {
        editing::GizmoPrimitive line;
        line.id = id;
        line.kind = "line";
        for (size_t axis = 0; axis < 3; ++axis) {
            line.position[axis] = (a[axis] + b[axis]) * 0.5;
            line.direction[axis] = b[axis] - a[axis];
        }
        line.length = std::sqrt(line.direction[0] * line.direction[0] +
                                line.direction[1] * line.direction[1] +
                                line.direction[2] * line.direction[2]);
        if (line.length > 0.0)
            for (double& component : line.direction) component /= line.length;
        line.color = color;
        line.dashed = dashed;
        gizmo.primitives.push_back(std::move(line));
    };
    const std::array<double, 4> accepted{0.2, 0.95, 0.35, 1.0};
    const std::array<double, 4> rejected{0.95, 0.2, 0.2, 1.0};
    const auto& pathColor = preview.status == EditorStatus::Applied ? accepted : rejected;
    for (size_t index = 0; index < controlPoints.size(); ++index) {
        editing::GizmoPrimitive handle;
        handle.id = "curve.control." + std::to_string(index);
        handle.kind = "sphere";
        handle.position = project(controlPoints[index].x, controlPoints[index].y);
        handle.radius = world_->getCellSize() * 0.15;
        handle.color = index == 0 || index + 1 == controlPoints.size()
                           ? std::array<double, 4>{0.2, 0.65, 1.0, 1.0}
                           : std::array<double, 4>{1.0, 0.7, 0.15, 1.0};
        gizmo.primitives.push_back(std::move(handle));
        if (index > 0)
            addLine("curve.tangent." + std::to_string(index - 1),
                    project(controlPoints[index - 1].x, controlPoints[index - 1].y),
                    project(controlPoints[index].x, controlPoints[index].y),
                    {0.65, 0.65, 0.7, 0.8}, true);
    }
    if (!preview.surfaceSamples.empty()) {
        for (size_t index = 1; index < preview.surfaceSamples.size(); ++index) {
            const auto& a = preview.surfaceSamples[index - 1];
            const auto& b = preview.surfaceSamples[index];
            addLine("curve.path." + std::to_string(index - 1),
                    {a.worldX, a.worldY, a.worldZ}, {b.worldX, b.worldY, b.worldZ},
                    pathColor, false);
        }
    } else {
        for (size_t index = 1; index < preview.sampledVertices.size(); ++index)
            addLine("curve.path." + std::to_string(index - 1),
                    project(preview.sampledVertices[index - 1].x,
                            preview.sampledVertices[index - 1].y),
                    project(preview.sampledVertices[index].x,
                            preview.sampledVertices[index].y),
                    pathColor, false);
    }
    return gizmo;
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeUpdateEdgeCubicBezier(
    int memberInstanceId, const std::vector<BuildingEdgeCurvePoint>& controlPoints,
    int subdivisions) const {
    if (!world_)
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.world-required",
                                              "Placement world is unavailable");
    auto currentGroup = world_->edgeCurveGroupForInstance(memberInstanceId);
    if (!currentGroup.ok())
        return buildingError<DomainOperation>(EditorStatus::NotFound,
                                              "editor.building.edge-curve-group-not-found",
                                              "Instance does not belong to a curve group");
    std::unique_ptr<building::PlacementWorld> draftWorld = world_->cloneState();
    for (int instanceId : currentGroup.value().instanceIds)
        if (!building::PlacementSystem::removeBuilding(draftWorld.get(), instanceId))
            return buildingError<DomainOperation>(
                EditorStatus::Failed, "editor.building.edge-curve-draft-delete-failed",
                "Curve update could not create an isolated draft world");
    BuildingPlacementTarget draftTarget(id_, std::move(draftWorld), revision_);
    auto desiredOperation = draftTarget.makeEdgeCubicBezier(
        currentGroup.value().buildingId, controlPoints, subdivisions);
    if (!desiredOperation.ok()) return desiredOperation;
    auto desired = parseEdgeCurve(desiredOperation.value().payload);
    if (!desired.ok())
        return buildingError<DomainOperation>(EditorStatus::Failed,
                                              "editor.building.edge-curve-encoding-failed",
                                              "Updated edge curve could not be encoded");
    desired.value().group.id = currentGroup.value().id;
    if (!currentGroup.value().surfaceProviderName.empty()) {
        std::vector<building::PlacementSystem::EdgeCurvePoint> runtimeControls;
        for (const BuildingEdgeCurvePoint& point : controlPoints)
            runtimeControls.push_back(
                {static_cast<float>(point.x), static_cast<float>(point.y)});
        auto surface = building::PlacementSystem::sampleEdgeCurveSurface(
            *world_, currentGroup.value().surfaceProviderName, runtimeControls, subdivisions);
        if (!surface.ok())
            return buildingError<DomainOperation>(
                EditorStatus::Rejected, "editor.building.edge-curve-surface-rejected",
                "Updated curve could not be sampled on its committed surface provider");
        desired.value().group.surfaceProviderName = surface.value().providerName;
        desired.value().group.surfaceId = surface.value().surfaceId;
        desired.value().group.surfaceRevision = surface.value().surfaceRevision;
        desired.value().group.surfaceSamples = surface.value().samples;
    }
    std::vector<BuildingInstanceSnapshot> beforeInstances;
    for (int instanceId : currentGroup.value().instanceIds)
        beforeInstances.push_back(snapshot(world_->buildings().at(instanceId)));
    DomainOperation operation = std::move(desiredOperation).takeValue();
    operation.type = "building.edge-curve.replace.v1";
    operation.inverseType = operation.type;
    operation.payload = edgeCurveValue(desired.value().group, desired.value().instances);
    operation.inverse = edgeCurveValue(currentGroup.value(), beforeInstances);
    operation.hasInverse = true;
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

EditorResult<BuildingEdgeCurvePoint> BuildingPlacementTarget::curveLogicalPointFromWorld(
    double worldX, double worldY, double worldZ) const {
    if (!world_ || !std::isfinite(worldX) || !std::isfinite(worldY) ||
        !std::isfinite(worldZ))
        return buildingError<BuildingEdgeCurvePoint>(
            EditorStatus::Rejected, "editor.building.curve-world-point-invalid",
            "Curve drag requires a finite world-space point and an available world");
    float ox = 0.f, oy = 0.f, xx = 0.f, xy = 0.f, yx = 0.f, yy = 0.f;
    world_->cellToWorldPlane(0, 0, ox, oy);
    world_->cellToWorldPlane(1, 0, xx, xy);
    world_->cellToWorldPlane(0, 1, yx, yy);
    const double bxX = xx - ox;
    const double bxY = xy - oy;
    const double byX = yx - ox;
    const double byY = yy - oy;
    const double determinant = bxX * byY - bxY * byX;
    if (std::abs(determinant) < 1e-9)
        return buildingError<BuildingEdgeCurvePoint>(
            EditorStatus::Unsupported, "editor.building.curve-grid-basis-singular",
            "Curve drag requires an invertible affine grid basis");
    const double planeX = worldX;
    const double planeY = world_->getGridPlaneName() == "xz" ? worldZ : worldY;
    const double dx = planeX - ox;
    const double dy = planeY - oy;
    BuildingEdgeCurvePoint point;
    point.x = (dx * byY - dy * byX) / determinant;
    point.y = (bxX * dy - bxY * dx) / determinant;
    return eve::editing::applied<BuildingEdgeCurvePoint>(point);
}

EditorResult<DomainOperation> BuildingPlacementTarget::makeRemove(int instanceId) const {
    auto current = instance(instanceId);
    if (!current.ok()) return buildingError<DomainOperation>(current.code(), "editor.building.instance-not-found",
                                                              "Placed building instance was not found");
    return eve::editing::applied<DomainOperation>(instanceOperation(
        id_, "building.instance.delete.v4", "building.instance.set.v4", current.value(), current.value()));
}

EditorResult<void> BuildingPlacementTarget::applyDomainOperation(const DomainOperation& operation) {
    if (!world_)
        return buildingError<void>(EditorStatus::Rejected, "editor.building.world-required",
                                   "Placement world is unavailable");
    if (operation.target != TargetId(id_))
        return buildingError<void>(EditorStatus::Rejected, "editor.building.target-mismatch",
                                   "Building operation targets another world");
    const bool areaSet = operation.type == "building.area.set.v1";
    const bool areaDelete = operation.type == "building.area.delete.v1";
    const bool edgeCurveSet = operation.type == "building.edge-curve.set.v1";
    const bool edgeCurveDelete = operation.type == "building.edge-curve.delete.v1";
    const bool edgeCurveReplace = operation.type == "building.edge-curve.replace.v1";
    if (edgeCurveSet || edgeCurveDelete || edgeCurveReplace) {
        auto parsedCurve = parseEdgeCurve(operation.payload);
        if (!parsedCurve.ok())
            return buildingError<void>(parsedCurve.code(),
                                       "editor.building.invalid-edge-curve-payload",
                                       "Building edge curve operation payload is invalid");
        std::unique_ptr<IDomainOperationTarget> candidate = cloneDomainState();
        auto* buildingCandidate = dynamic_cast<BuildingPlacementTarget*>(candidate.get());
        if (!buildingCandidate)
            return buildingError<void>(EditorStatus::Failed,
                                       "editor.building.edge-curve-staging-unavailable",
                                       "Building edge curve operation could not create a staging world");
        if (edgeCurveSet || edgeCurveReplace) {
            if (edgeCurveReplace) {
                auto current = buildingCandidate->world_->edgeCurveGroup(
                    parsedCurve.value().group.id);
                if (!current.ok())
                    return buildingError<void>(
                        EditorStatus::NotFound,
                        "editor.building.edge-curve-replace-source-not-found",
                        "Building edge curve replacement source was not found");
                for (int instanceId : current.value().instanceIds)
                    if (!building::PlacementSystem::removeBuilding(buildingCandidate->world_,
                                                                   instanceId))
                        return buildingError<void>(
                            EditorStatus::Rejected,
                            "editor.building.edge-curve-replace-delete-rejected",
                            "Building edge curve replacement could not remove its source");
            }
            std::vector<building::PlacedBuilding> members;
            for (const BuildingInstanceSnapshot& instance : parsedCurve.value().instances)
                members.push_back(runtime(instance));
            auto restored = building::PlacementSystem::restoreEdgeCurveGroupExact(
                buildingCandidate->world_, parsedCurve.value().group, members);
            if (!restored.ok())
                return buildingError<void>(EditorStatus::Rejected,
                                           "editor.building.edge-curve-atomic-rejected",
                                           "Building edge curve restore was rejected without mutation");
        } else {
            for (int instanceId : parsedCurve.value().group.instanceIds) {
                if (!buildingCandidate->world_->hasBuilding(instanceId) ||
                    !building::PlacementSystem::removeBuilding(buildingCandidate->world_,
                                                               instanceId))
                    return buildingError<void>(EditorStatus::Rejected,
                                               "editor.building.edge-curve-delete-rejected",
                                               "Building edge curve delete was rejected without mutation");
            }
        }
        return commitDomainState(std::move(candidate));
    }
    if (areaSet || areaDelete) {
        auto parsedArea = parseInstances(operation.payload);
        if (!parsedArea.ok())
            return buildingError<void>(parsedArea.code(), "editor.building.invalid-area-payload",
                                       "Building area operation payload is invalid");
        std::unique_ptr<IDomainOperationTarget> candidate = cloneDomainState();
        auto* buildingCandidate = dynamic_cast<BuildingPlacementTarget*>(candidate.get());
        if (!buildingCandidate)
            return buildingError<void>(EditorStatus::Failed,
                                       "editor.building.area-staging-unavailable",
                                       "Building area operation could not create a staging world");
        for (const BuildingInstanceSnapshot& item : parsedArea.value()) {
            DomainOperation single = instanceOperation(
                id_, areaSet ? "building.instance.set.v4" : "building.instance.delete.v4",
                areaSet ? "building.instance.delete.v4" : "building.instance.set.v4", item,
                item);
            auto applied = buildingCandidate->applyDomainOperation(single);
            if (!applied.ok())
                return buildingError<void>(applied.code(), "editor.building.area-atomic-rejected",
                                           "Building area operation was rejected without mutation");
        }
        return commitDomainState(std::move(candidate));
    }
    auto parsed = parseInstance(operation.payload);
    if (!parsed.ok())
        return buildingError<void>(parsed.code(), "editor.building.invalid-instance",
                                   "Building operation payload is invalid");
    if (operation.type == "building.instance.delete.v1" ||
        operation.type == "building.instance.delete.v2" ||
        operation.type == "building.instance.delete.v3" ||
        operation.type == "building.instance.delete.v4") {
        if (!world_->hasBuilding(parsed.value().instanceId))
            return buildingError<void>(EditorStatus::NotFound, "editor.building.instance-not-found",
                                       "Placed building instance was not found");
        if (!building::PlacementSystem::removeBuilding(world_, parsed.value().instanceId))
            return buildingError<void>(EditorStatus::Failed, "editor.building.remove-failed",
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
                return buildingError<void>(EditorStatus::Rejected,
                                           "editor.building." + (reason.empty() ? "restore-failed" : reason),
                                           "PlacementSystem rejected exact instance restore: " + reason);
        } else {
            const auto current = world_->buildings().find(desired.instanceId);
            const building::BuildingDefinition* desiredDefinition =
                building::BuildingRegistry::find(desired.buildingId);
            if (!desiredDefinition)
                return buildingError<void>(EditorStatus::NotFound,
                                           "editor.building.definition-not-found",
                                           "Desired building definition was not found");
            if (current->second.placementKind != desired.placementKind ||
                desired.placementKind != desiredDefinition->placementKind)
                return buildingError<void>(EditorStatus::Rejected,
                                           "editor.building.placement-domain-mismatch",
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
                return buildingError<void>(EditorStatus::Rejected,
                                           "editor.building." +
                                               (reason.empty() ? "set-rejected" : reason),
                                           "Building set preflight rejected without mutation: " +
                                               reason);
            if (current->second.buildingId != desired.buildingId) {
                auto replaced = building::PlacementSystem::replaceBuildingResult(
                    world_, desired.instanceId, desired.buildingId);
                if (!replaced.ok())
                    return buildingError<void>(EditorStatus::Rejected,
                                               "editor.building.replace-failed",
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
                return buildingError<void>(EditorStatus::Rejected, "editor.building.move-failed",
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
        return buildingError<void>(EditorStatus::Unsupported, "editor.building.operation-unsupported",
                                   "Building placement operation is unsupported: " + operation.type);
    }
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> BuildingPlacementTarget::cloneDomainState() const {
    if (!world_) return {};
    return std::unique_ptr<IDomainOperationTarget>(
        new BuildingPlacementTarget(id_, world_->cloneState(), revision_));
}

EditorResult<void> BuildingPlacementTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* buildingCandidate = dynamic_cast<BuildingPlacementTarget*>(candidate.get());
    if (!world_ || !buildingCandidate || !buildingCandidate->ownedWorld_ ||
        buildingCandidate->targetId() != targetId())
        return buildingError<void>(EditorStatus::Conflict, "editor.building.candidate-mismatch",
                                   "Building candidate cannot be published to this target");
    world_->swapState(*buildingCandidate->ownedWorld_);
    ++revision_;
    dirty_.include(0, 0);
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

BuildingEdgeCurveDragSession::BuildingEdgeCurveDragSession(
    BuildingPlacementTarget* target, std::string buildingId, int memberInstanceId,
    std::vector<BuildingEdgeCurvePoint> controlPoints, int subdivisions,
    std::string surfaceName)
    : target_(target),
      buildingId_(std::move(buildingId)),
      memberInstanceId_(memberInstanceId),
      controls_(std::move(controlPoints)),
      subdivisions_(subdivisions),
      surfaceName_(std::move(surfaceName)) {}

EditorResult<BuildingEdgeCurveDragPreview> BuildingEdgeCurveDragSession::previewCurrent() const {
    if (!target_)
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, "editor.building.curve-drag-target-required",
            "Curve drag target is unavailable");
    BuildingEdgeCurveDragPreview result;
    result.controlPointIndex = activeControlPointIndex_;
    result.curve = target_->previewEdgeCubicBezier(
        buildingId_, controls_, subdivisions_, memberInstanceId_, surfaceName_);
    result.gizmo = target_->edgeCubicBezierGizmo(
        buildingId_, controls_, subdivisions_, 0, memberInstanceId_, surfaceName_);
    result.status = result.curve.status;
    result.diagnostics = result.curve.diagnostics;
    return eve::editing::applied<BuildingEdgeCurveDragPreview>(std::move(result));
}

EditorResult<BuildingEdgeCurveDragPreview> BuildingEdgeCurveDragSession::beginDrag(
    double rayOriginX, double rayOriginY, double rayOriginZ, double rayDirectionX,
    double rayDirectionY, double rayDirectionZ) {
    cancelDrag();
    if (!target_ || memberInstanceId_ <= 0 || controls_.size() != 4)
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, "editor.building.curve-drag-state-invalid",
            "Curve drag requires an existing group member and four controls");
    const double directionLength = std::sqrt(rayDirectionX * rayDirectionX +
                                             rayDirectionY * rayDirectionY +
                                             rayDirectionZ * rayDirectionZ);
    if (!std::isfinite(directionLength) || directionLength < 1e-9)
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, "editor.building.curve-drag-ray-invalid",
            "Curve drag requires a finite non-zero pointer ray");
    const std::array<double, 3> origin{rayOriginX, rayOriginY, rayOriginZ};
    const std::array<double, 3> direction{rayDirectionX / directionLength,
                                          rayDirectionY / directionLength,
                                          rayDirectionZ / directionLength};
    const editing::GizmoSnapshot gizmo = target_->edgeCubicBezierGizmo(
        buildingId_, controls_, subdivisions_, 0, memberInstanceId_, surfaceName_);
    double closest = std::numeric_limits<double>::infinity();
    for (const editing::GizmoPrimitive& primitive : gizmo.primitives) {
        if (primitive.kind != "sphere" || primitive.id.rfind("curve.control.", 0) != 0)
            continue;
        const std::array<double, 3> offset{origin[0] - primitive.position[0],
                                           origin[1] - primitive.position[1],
                                           origin[2] - primitive.position[2]};
        const double b = offset[0] * direction[0] + offset[1] * direction[1] +
                         offset[2] * direction[2];
        const double c = offset[0] * offset[0] + offset[1] * offset[1] +
                         offset[2] * offset[2] - primitive.radius * primitive.radius;
        const double discriminant = b * b - c;
        if (discriminant < 0.0) continue;
        const double nearHit = -b - std::sqrt(discriminant);
        const double farHit = -b + std::sqrt(discriminant);
        const double hit = nearHit >= 0.0 ? nearHit : farHit;
        if (hit < 0.0 || hit >= closest) continue;
        closest = hit;
        activeControlPointIndex_ = std::stoi(primitive.id.substr(14));
        dragPlanePoint_ = primitive.position;
    }
    if (activeControlPointIndex_ < 0)
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::NotFound, "editor.building.curve-control-not-hit",
            "Pointer ray did not hit a curve control handle");
    dragPlaneNormal_ = direction;
    baseRevision_ = target_->revision();
    dragging_ = true;
    lastPreviewValid_ = gizmo.status == EditorStatus::Applied;
    return previewCurrent();
}

EditorResult<BuildingEdgeCurveDragPreview> BuildingEdgeCurveDragSession::updateDrag(
    double rayOriginX, double rayOriginY, double rayOriginZ, double rayDirectionX,
    double rayDirectionY, double rayDirectionZ) {
    if (!dragging_ || !target_)
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, "editor.building.curve-drag-not-active",
            "Curve drag has not begun");
    if (target_->revision() != baseRevision_) {
        cancelDrag();
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::Conflict, "editor.building.curve-drag-stale",
            "Curve world changed while the control handle was being dragged");
    }
    const double length = std::sqrt(rayDirectionX * rayDirectionX +
                                    rayDirectionY * rayDirectionY +
                                    rayDirectionZ * rayDirectionZ);
    if (!std::isfinite(length) || length < 1e-9)
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, "editor.building.curve-drag-ray-invalid",
            "Curve drag requires a finite non-zero pointer ray");
    const std::array<double, 3> origin{rayOriginX, rayOriginY, rayOriginZ};
    const std::array<double, 3> direction{rayDirectionX / length, rayDirectionY / length,
                                          rayDirectionZ / length};
    const double denominator = direction[0] * dragPlaneNormal_[0] +
                               direction[1] * dragPlaneNormal_[1] +
                               direction[2] * dragPlaneNormal_[2];
    if (std::abs(denominator) < 1e-9)
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, "editor.building.curve-drag-ray-parallel",
            "Pointer ray is parallel to the curve drag plane");
    const double distance = ((dragPlanePoint_[0] - origin[0]) * dragPlaneNormal_[0] +
                             (dragPlanePoint_[1] - origin[1]) * dragPlaneNormal_[1] +
                             (dragPlanePoint_[2] - origin[2]) * dragPlaneNormal_[2]) /
                            denominator;
    if (distance < 0.0)
        return buildingError<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, "editor.building.curve-drag-behind-camera",
            "Curve drag plane is behind the pointer ray origin");
    auto logical = target_->curveLogicalPointFromWorld(
        origin[0] + direction[0] * distance, origin[1] + direction[1] * distance,
        origin[2] + direction[2] * distance);
    if (!logical.ok())
        return buildingError<BuildingEdgeCurveDragPreview>(
            logical.code(), "editor.building.curve-drag-projection-failed",
            "Pointer ray could not be projected to logical curve coordinates");
    controls_[static_cast<size_t>(activeControlPointIndex_)] = logical.value();
    auto preview = previewCurrent();
    if (preview.ok()) lastPreviewValid_ = preview.value().status == EditorStatus::Applied;
    return preview;
}

EditorResult<DomainOperation> BuildingEdgeCurveDragSession::finishDrag() {
    if (!dragging_ || !target_)
        return buildingError<DomainOperation>(EditorStatus::Rejected,
                                              "editor.building.curve-drag-not-active",
                                              "Curve drag has not begun");
    if (target_->revision() != baseRevision_) {
        cancelDrag();
        return buildingError<DomainOperation>(EditorStatus::Conflict,
                                              "editor.building.curve-drag-stale",
                                              "Curve world changed before drag commit");
    }
    if (!lastPreviewValid_)
        return buildingError<DomainOperation>(
            EditorStatus::Rejected, "editor.building.curve-drag-preview-invalid",
            "The last curve drag preview is not valid for commit");
    auto operation = target_->makeUpdateEdgeCubicBezier(memberInstanceId_, controls_,
                                                         subdivisions_);
    if (!operation.ok()) return operation;
    cancelDrag();
    return operation;
}

void BuildingEdgeCurveDragSession::cancelDrag() {
    dragging_ = false;
    activeControlPointIndex_ = -1;
    lastPreviewValid_ = false;
}

}  // namespace eve::building_editing
