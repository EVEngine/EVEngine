#include "building/BuildingSnapshot.h"
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include "building/BuildingSnapshotInternal.h"
#include "building/BuildingTypes.h"

namespace eve::building::codec {
namespace {
template <class T>
Result<T> failure(StatusCode status, std::string rule, std::string message) {
    return Result<T>::failure(
        Status::failure(status, Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(message), {},
                                           {{"rule", std::move(rule)}}, "building.codec")));
}
template <class T>
Result<T> success(T value) {
    return Result<T>::success(std::move(value));
}
}  // namespace
Value stringArray(const std::vector<std::string>& values) {
    Value::Array result;
    for (const std::string& value : values) result.emplace_back(value);
    return result;
}

Value numberArray(const std::vector<double>& values) {
    Value::Array result;
    for (double value : values) result.emplace_back(value);
    return result;
}

std::vector<std::string> parseStringArray(const Value& value, bool& valid) {
    std::vector<std::string> result;
    const auto*              values = value.getIf<Value::Array>();
    if (!values) {
        valid = false;
        return result;
    }
    for (const Value& item : *values) {
        const auto* text = item.getIf<std::string>();
        if (!text) {
            valid = false;
            return {};
        }
        result.push_back(*text);
    }
    return result;
}

Value instanceValue(const BuildingInstanceSnapshot& instance) {
    Value::Object properties;
    for (const auto& [key, value] : instance.properties) properties[key] = value;
    Value::Array garrison;
    for (const BuildingGarrisonMemberRecord& member : instance.garrison)
        garrison.emplace_back(
            Value::Object{{"id", member.id}, {"type", member.type}, {"tags", stringArray(member.tags)}});
    return Value::Object{{"instanceId", int64_t{instance.instanceId}},
                         {"buildingId", instance.buildingId},
                         {"placementKind", instance.placementKind},
                         {"edgeX", int64_t{instance.edgeX}},
                         {"edgeY", int64_t{instance.edgeY}},
                         {"edgeAxis", instance.edgeAxis},
                         {"cornerX", int64_t{instance.cornerX}},
                         {"cornerY", int64_t{instance.cornerY}},
                         {"freeRadius", instance.freeRadius},
                         {"freeHalfWidth", instance.freeHalfWidth},
                         {"freeHalfHeight", instance.freeHalfHeight},
                         {"freeFootprintVertices", numberArray(instance.freeFootprintVertices)},
                         {"cellX", int64_t{instance.cellX}},
                         {"cellY", int64_t{instance.cellY}},
                         {"level", int64_t{instance.level}},
                         {"worldX", instance.worldX},
                         {"worldY", instance.worldY},
                         {"elevation", instance.elevation},
                         {"surfaceId", instance.surfaceId},
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
                         {"channel", instance.channel},
                         {"properties", std::move(properties)},
                         {"tags", stringArray(instance.tags)},
                         {"garrison", std::move(garrison)},
                         {"garrisonRevision", static_cast<int64_t>(instance.garrisonRevision)}};
}

const Value* field(const Value& value, const char* key) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

Result<BuildingInstanceSnapshot> parseInstance(const Value& value) {
    const auto integer = [&](const char* key) -> const int64_t* {
        const Value* entry = field(value, key);
        return entry ? entry->getIf<int64_t>() : nullptr;
    };
    std::map<std::string, double> numbers;
    for (const auto& key : value.keys()) {
        const auto* entry = value.find(key);
        if (entry->isNumeric()) {
            const double n = entry->isInt64() ? double(entry->asInt()) : entry->asDouble();
            if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max())
                return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "building.invalid-number",
                                                         "Non-finite or overflowing number");
            numbers[key] = n;
        }
    }
    for (const char* key :
         {"instanceId", "cellX", "cellY", "edgeX", "edgeY", "cornerX", "cornerY", "level", "surfaceSampleCount"}) {
        if (const auto* entry = field(value, key);
            entry && (!entry->isInt64() || entry->asInt() < std::numeric_limits<int>::min() ||
                      entry->asInt() > std::numeric_limits<int>::max()))
            return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "building.invalid-integer",
                                                     "Integer field out of range");
    }
    const auto number = [&](const char* key) -> const double* {
        auto it = numbers.find(key);
        return it == numbers.end() ? nullptr : &it->second;
    };
    const auto string = [&](const char* key) -> const std::string* {
        const Value* entry = field(value, key);
        return entry ? entry->getIf<std::string>() : nullptr;
    };
    const auto* instanceId       = integer("instanceId");
    const auto* buildingId       = string("buildingId");
    const auto* cellX            = integer("cellX");
    const auto* cellY            = integer("cellY");
    const auto* worldX           = number("worldX");
    const auto* worldY           = number("worldY");
    const auto* elevation        = number("elevation");
    const auto* rotation         = number("rotationDegrees");
    const auto* channel          = string("channel");
    const auto* propertiesValue  = field(value, "properties");
    const auto* tagsValue        = field(value, "tags");
    const auto* garrisonValue    = field(value, "garrison");
    const auto* garrisonRevision = integer("garrisonRevision");
    const auto* properties       = propertiesValue ? propertiesValue->getIf<Value::Object>() : nullptr;
    const auto* garrison         = garrisonValue ? garrisonValue->getIf<Value::Array>() : nullptr;
    if (!instanceId || *instanceId <= 0 || !buildingId || buildingId->empty() || !cellX || !cellY || !worldX ||
        !worldY || !elevation || !rotation || !channel || !properties || !tagsValue || !garrison || !garrisonRevision ||
        *garrisonRevision < 0)
        return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-instance",
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
    if (const auto* freeHalfWidth = number("freeHalfWidth")) result.freeHalfWidth = *freeHalfWidth;
    if (const auto* freeHalfHeight = number("freeHalfHeight")) result.freeHalfHeight = *freeHalfHeight;
    if (const Value* polygon = field(value, "freeFootprintVertices")) {
        const auto* vertices = polygon->getIf<Value::Array>();
        if (!vertices)
            return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-free-footprint",
                                                     "Free footprint vertices must be a numeric array");
        for (const Value& vertex : *vertices) {
            if (!vertex.isNumeric())
                return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-free-footprint",
                                                         "Free footprint vertices must contain only numbers");
            const double coordinate = vertex.isInt64() ? double(vertex.asInt()) : vertex.asDouble();
            if (!std::isfinite(coordinate) || std::abs(coordinate) > std::numeric_limits<float>::max())
                return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "building.invalid-polygon",
                                                         "Invalid polygon coordinate");
            result.freeFootprintVertices.push_back(coordinate);
        }
    }
    result.cellX = static_cast<int>(*cellX);
    result.cellY = static_cast<int>(*cellY);
    if (const auto* level = integer("level")) result.level = static_cast<int>(*level);
    result.worldX    = *worldX;
    result.worldY    = *worldY;
    result.elevation = *elevation;
    if (const auto* surfaceId = string("surfaceId")) result.surfaceId = *surfaceId;
    if (const auto* surfaceRevision = integer("surfaceRevision")) {
        if (*surfaceRevision < 0)
            return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-surface-revision",
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
    if (result.placementKind != "cell" && result.placementKind != "edge" && result.placementKind != "corner" &&
        result.placementKind != "free")
        return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-placement-kind",
                                                 "Placement kind must be cell, edge, corner, or free");
    if (result.placementKind == "edge" && result.edgeAxis != "horizontal" && result.edgeAxis != "vertical")
        return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-edge-axis",
                                                 "Edge axis must be horizontal or vertical");
    result.rotationDegrees  = *rotation;
    result.channel          = *channel;
    result.garrisonRevision = static_cast<std::uint64_t>(*garrisonRevision);
    for (const auto& [key, property] : *properties) {
        const auto* text = property.getIf<std::string>();
        if (!text)
            return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-property",
                                                     "Building properties must be strings");
        result.properties[key] = *text;
    }
    bool valid  = true;
    result.tags = parseStringArray(*tagsValue, valid);
    if (!valid)
        return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-tags",
                                                 "Building tags must be strings");
    std::set<std::string> memberIds;
    for (const Value& memberValue : *garrison) {
        const auto* idValue    = field(memberValue, "id");
        const auto* typeValue  = field(memberValue, "type");
        const auto* memberTags = field(memberValue, "tags");
        const auto* id         = idValue ? idValue->getIf<std::string>() : nullptr;
        const auto* type       = typeValue ? typeValue->getIf<std::string>() : nullptr;
        if (!id || id->empty() || !memberIds.insert(*id).second || !type || type->empty() || !memberTags)
            return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-garrison",
                                                     "Garrison members require unique ids, type and tags");
        auto tags = parseStringArray(*memberTags, valid);
        if (!valid)
            return failure<BuildingInstanceSnapshot>(StatusCode::Rejected, "editor.building.invalid-garrison-tags",
                                                     "Garrison member tags must be strings");
        result.garrison.push_back({*id, *type, std::move(tags)});
    }
    return success<BuildingInstanceSnapshot>(std::move(result));
}

Result<std::vector<BuildingInstanceSnapshot>> parseInstances(const Value& value) {
    const auto* values = value.getIf<Value::Array>();
    if (!values || values->empty())
        return failure<std::vector<BuildingInstanceSnapshot>>(
            StatusCode::Rejected, "editor.building.invalid-area-payload",
            "Building area payload must contain at least one instance");
    std::vector<BuildingInstanceSnapshot> result;
    std::set<int>                         ids;
    result.reserve(values->size());
    for (const Value& item : *values) {
        auto parsed = parseInstance(item);
        if (!parsed.ok())
            return failure<std::vector<BuildingInstanceSnapshot>>(parsed.code(),
                                                                  "editor.building.invalid-area-instance",
                                                                  "Building area payload contains an invalid instance");
        if (!ids.insert(parsed.value().instanceId).second)
            return failure<std::vector<BuildingInstanceSnapshot>>(
                StatusCode::Rejected, "editor.building.duplicate-area-instance",
                "Building area payload contains duplicate instance ids");
        result.push_back(std::move(parsed).takeValue());
    }
    return success<std::vector<BuildingInstanceSnapshot>>(std::move(result));
}

BuildingInstanceSnapshot snapshot(const building::PlacedBuilding& placed) {
    BuildingInstanceSnapshot result;
    result.instanceId     = placed.instanceId;
    result.buildingId     = placed.buildingId;
    result.placementKind  = placed.placementKind;
    result.edgeX          = placed.edge.x;
    result.edgeY          = placed.edge.y;
    result.edgeAxis       = placed.edge.axis == building::EdgeAxis::Horizontal ? "horizontal" : "vertical";
    result.cornerX        = placed.corner.x;
    result.cornerY        = placed.corner.y;
    result.freeRadius     = placed.freeRadius;
    result.freeHalfWidth  = placed.freeHalfWidth;
    result.freeHalfHeight = placed.freeHalfHeight;
    for (float coordinate : placed.freeFootprintVertices) result.freeFootprintVertices.push_back(coordinate);
    result.cellX                  = placed.originCellX;
    result.cellY                  = placed.originCellY;
    result.level                  = placed.level;
    result.worldX                 = placed.worldX;
    result.worldY                 = placed.worldY;
    result.elevation              = placed.elevation;
    result.surfaceId              = placed.surfaceId;
    result.surfaceRevision        = placed.surfaceRevision;
    result.surfaceNormalX         = placed.surfaceNormalX;
    result.surfaceNormalY         = placed.surfaceNormalY;
    result.surfaceNormalZ         = placed.surfaceNormalZ;
    result.surfaceTangentX        = placed.surfaceTangentX;
    result.surfaceTangentY        = placed.surfaceTangentY;
    result.surfaceTangentZ        = placed.surfaceTangentZ;
    result.surfaceSampleCount     = placed.surfaceSampleCount;
    result.surfaceMaxSlopeDegrees = placed.surfaceMaxSlopeDegrees;
    result.surfaceHeightDelta     = placed.surfaceHeightDelta;
    result.rotationDegrees        = placed.rotationDeg;
    result.channel                = placed.channel;
    result.properties.insert(placed.props.begin(), placed.props.end());
    result.tags = placed.tags;
    for (const building::GarrisonMember& member : placed.garrison)
        result.garrison.push_back({member.id, member.type, member.tags});
    result.garrisonRevision = placed.garrisonRevision.value();
    return result;
}

building::PlacedBuilding runtime(const BuildingInstanceSnapshot& value) {
    building::PlacedBuilding result;
    result.instanceId     = value.instanceId;
    result.buildingId     = value.buildingId;
    result.placementKind  = value.placementKind;
    result.edge           = {value.edgeX, value.edgeY,
                   value.edgeAxis == "vertical" ? building::EdgeAxis::Vertical : building::EdgeAxis::Horizontal};
    result.corner         = {value.cornerX, value.cornerY};
    result.freeRadius     = static_cast<float>(value.freeRadius);
    result.freeHalfWidth  = static_cast<float>(value.freeHalfWidth);
    result.freeHalfHeight = static_cast<float>(value.freeHalfHeight);
    for (double coordinate : value.freeFootprintVertices)
        result.freeFootprintVertices.push_back(static_cast<float>(coordinate));
    result.originCellX            = value.cellX;
    result.originCellY            = value.cellY;
    result.level                  = value.level;
    result.worldX                 = static_cast<float>(value.worldX);
    result.worldY                 = static_cast<float>(value.worldY);
    result.elevation              = static_cast<float>(value.elevation);
    result.surfaceId              = value.surfaceId;
    result.surfaceRevision        = value.surfaceRevision;
    result.surfaceNormalX         = static_cast<float>(value.surfaceNormalX);
    result.surfaceNormalY         = static_cast<float>(value.surfaceNormalY);
    result.surfaceNormalZ         = static_cast<float>(value.surfaceNormalZ);
    result.surfaceTangentX        = static_cast<float>(value.surfaceTangentX);
    result.surfaceTangentY        = static_cast<float>(value.surfaceTangentY);
    result.surfaceTangentZ        = static_cast<float>(value.surfaceTangentZ);
    result.surfaceSampleCount     = value.surfaceSampleCount;
    result.surfaceMaxSlopeDegrees = static_cast<float>(value.surfaceMaxSlopeDegrees);
    result.surfaceHeightDelta     = static_cast<float>(value.surfaceHeightDelta);
    result.rotationDeg            = static_cast<float>(value.rotationDegrees);
    result.channel                = value.channel;
    result.props.insert(value.properties.begin(), value.properties.end());
    result.tags = value.tags;
    for (const BuildingGarrisonMemberRecord& member : value.garrison)
        result.garrison.push_back({member.id, member.type, member.tags});
    result.garrisonRevision = eve::Revision(value.garrisonRevision);
    return result;
}


Value edgeCurveValue(const building::EdgeCurveGroup& group, const std::vector<BuildingInstanceSnapshot>& instances) {
    std::vector<double> controls;
    for (const building::EdgeCurveControlPoint& point : group.controlPoints) {
        controls.push_back(point.x);
        controls.push_back(point.y);
    }
    Value::Array values;
    for (const BuildingInstanceSnapshot& instance : instances) values.push_back(instanceValue(instance));
    std::vector<double> surfaceSamples;
    for (const building::EdgeCurveSurfaceSample& sample : group.surfaceSamples) {
        surfaceSamples.insert(surfaceSamples.end(), {sample.worldX, sample.worldY, sample.worldZ, sample.normalX,
                                                     sample.normalY, sample.normalZ});
    }
    return Value::Object{{"groupId", static_cast<int64_t>(group.id.value)},
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

Result<EdgeCurvePayload> parseEdgeCurve(const Value& value) {
    const Value* groupIdValue      = field(value, "groupId");
    const Value* buildingIdValue   = field(value, "buildingId");
    const Value* levelValue        = field(value, "level");
    const Value* subdivisionsValue = field(value, "subdivisions");
    const Value* controlsValue     = field(value, "controlPoints");
    const Value* instancesValue    = field(value, "instances");
    const auto*  groupId           = groupIdValue ? groupIdValue->getIf<int64_t>() : nullptr;
    const auto*  buildingId        = buildingIdValue ? buildingIdValue->getIf<std::string>() : nullptr;
    const auto*  level             = levelValue ? levelValue->getIf<int64_t>() : nullptr;
    const auto*  subdivisions      = subdivisionsValue ? subdivisionsValue->getIf<int64_t>() : nullptr;
    const auto*  controls          = controlsValue ? controlsValue->getIf<Value::Array>() : nullptr;
    if (!groupId || *groupId <= 0 || !buildingId || buildingId->empty() || !level ||
        *level < std::numeric_limits<int>::min() || *level > std::numeric_limits<int>::max() || !subdivisions ||
        *subdivisions < 2 || *subdivisions > 4096 || !controls || controls->size() != 8 || !instancesValue)
        return failure<EdgeCurvePayload>(StatusCode::Rejected, "editor.building.invalid-edge-curve",
                                         "Edge curve payload is incomplete");
    auto instances = parseInstances(*instancesValue);
    if (!instances.ok())
        return failure<EdgeCurvePayload>(instances.code(), "editor.building.invalid-edge-curve-instances",
                                         "Edge curve instances are invalid");
    EdgeCurvePayload result;
    result.group.id           = {static_cast<std::uint64_t>(*groupId)};
    result.group.buildingId   = *buildingId;
    result.group.level        = static_cast<int>(*level);
    result.group.subdivisions = static_cast<int>(*subdivisions);
    if (const Value* provider = field(value, "surfaceProviderName"))
        if (const auto* text = provider->getIf<std::string>()) result.group.surfaceProviderName = *text;
    if (const Value* surface = field(value, "surfaceId"))
        if (const auto* text = surface->getIf<std::string>()) result.group.surfaceId = *text;
    if (const Value* revision = field(value, "surfaceRevision")) {
        const auto* integer = revision->getIf<int64_t>();
        if (!integer || *integer < 0)
            return failure<EdgeCurvePayload>(StatusCode::Rejected,
                                             "editor.building.invalid-edge-curve-surface-revision",
                                             "Edge curve surface revision is invalid");
        result.group.surfaceRevision = static_cast<std::uint64_t>(*integer);
    }
    if (const Value* sampleValue = field(value, "surfaceSamples")) {
        const auto* samples = sampleValue->getIf<Value::Array>();
        if (!samples || samples->size() % 6 != 0)
            return failure<EdgeCurvePayload>(StatusCode::Rejected, "editor.building.invalid-edge-curve-surface-samples",
                                             "Edge curve surface samples must contain position and normal sextuples");
        for (size_t index = 0; index < samples->size(); index += 6) {
            std::array<float, 6> frame{};
            for (size_t component = 0; component < 6; ++component) {
                const auto& number = (*samples)[index + component];
                if (!number.isNumeric())
                    return failure<EdgeCurvePayload>(StatusCode::Rejected,
                                                     "editor.building.invalid-edge-curve-surface-samples",
                                                     "Edge curve surface samples must be numeric");
                frame[component] = static_cast<float>(number.isInt64() ? double(number.asInt()) : number.asDouble());
                if (!std::isfinite(frame[component]))
                    return failure<EdgeCurvePayload>(StatusCode::Rejected, "building.invalid-curve-frame",
                                                     "Non-finite curve frame");
            }
            result.group.surfaceSamples.push_back({frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]});
        }
    }
    for (size_t index = 0; index < controls->size(); index += 2) {
        const auto& x = (*controls)[index];
        const auto& y = (*controls)[index + 1];
        if (!x.isNumeric() || !y.isNumeric())
            return failure<EdgeCurvePayload>(StatusCode::Rejected, "editor.building.invalid-edge-curve-controls",
                                             "Edge curve controls must be numbers");
        result.group.controlPoints.push_back({static_cast<float>(x.isInt64() ? double(x.asInt()) : x.asDouble()),
                                              static_cast<float>(y.isInt64() ? double(y.asInt()) : y.asDouble())});
    }
    for (const auto& point : result.group.controlPoints)
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return failure<EdgeCurvePayload>(StatusCode::Rejected, "building.invalid-curve-controls",
                                             "Non-finite curve controls");
    result.instances = std::move(instances).takeValue();
    for (const BuildingInstanceSnapshot& instance : result.instances)
        result.group.instanceIds.push_back(instance.instanceId);
    return success<EdgeCurvePayload>(std::move(result));
}


}  // namespace eve::building::codec
