#include "physics/destruction/GeometryCollectionSnapshot.h"

#include "schema/SchemaRegistry.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>

namespace eve::physics {
namespace {

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool hasExactFields(const eve::Value::Object& object, std::initializer_list<std::string_view> expected) {
    if (object.size() != expected.size()) return false;
    for (const auto name : expected)
        if (!object.contains(std::string(name))) return false;
    return true;
}

eve::Result<std::int64_t> integer64(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isInt64())
        return eve::Result<std::int64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot field must be an integer", std::string(name)));
    return eve::Result<std::int64_t>::success(value->asInt());
}

eve::Result<float> number(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isNumeric())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot field must be numeric", std::string(name)));
    const double parsed = value->isDouble() ? value->asDouble() : static_cast<double>(value->asInt());
    if (!std::isfinite(parsed) || parsed < -std::numeric_limits<float>::max() ||
        parsed > std::numeric_limits<float>::max())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot number must be a finite float",
            std::string(name)));
    return eve::Result<float>::success(static_cast<float>(parsed));
}

eve::Result<bool> boolean(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isBool())
        return eve::Result<bool>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot field must be boolean", std::string(name)));
    return eve::Result<bool>::success(value->asBool());
}

eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot> decodeBone(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot bone must be an object", "bones"));
    if (!hasExactFields(*object, {"state", "anchored", "clusterId", "fractureLevel", "halfExtentX", "halfExtentY",
                                  "halfExtentZ"}))
        return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot bone has unknown or missing fields", "bones"));
    auto state = integer64(*object, "state");
    if (!state)
        return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(state.status());
    if (state.value() < 0 || state.value() > 2)
        return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot bone state must be 0..2", "state"));
    auto anchored = boolean(*object, "anchored");
    if (!anchored)
        return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(anchored.status());
    auto clusterId = integer64(*object, "clusterId");
    if (!clusterId)
        return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(clusterId.status());
    auto fractureLevel = integer64(*object, "fractureLevel");
    if (!fractureLevel)
        return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(fractureLevel.status());
    auto hx = number(*object, "halfExtentX");
    if (!hx) return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(hx.status());
    auto hy = number(*object, "halfExtentY");
    if (!hy) return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(hy.status());
    auto hz = number(*object, "halfExtentZ");
    if (!hz) return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::failure(hz.status());
    GeometryCollectionInstanceSnapshot::BoneSnapshot bone;
    bone.state         = static_cast<std::uint8_t>(state.value());
    bone.anchored     = anchored.value();
    bone.clusterId    = static_cast<int>(clusterId.value());
    bone.fractureLevel = static_cast<int>(fractureLevel.value());
    bone.halfExtentX  = hx.value();
    bone.halfExtentY  = hy.value();
    bone.halfExtentZ  = hz.value();
    return eve::Result<GeometryCollectionInstanceSnapshot::BoneSnapshot>::success(bone);
}

eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot> decodeEdge(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot edge must be an object", "edges"));
    if (!hasExactFields(*object, {"boneA", "boneB", "strainThreshold", "accumulatedStrain", "broken"}))
        return eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot edge has unknown or missing fields", "edges"));
    auto a = integer64(*object, "boneA");
    if (!a) return eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot>::failure(a.status());
    auto b = integer64(*object, "boneB");
    if (!b) return eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot>::failure(b.status());
    auto threshold = number(*object, "strainThreshold");
    if (!threshold) return eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot>::failure(threshold.status());
    auto strain = number(*object, "accumulatedStrain");
    if (!strain) return eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot>::failure(strain.status());
    auto broken = boolean(*object, "broken");
    if (!broken) return eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot>::failure(broken.status());
    GeometryCollectionInstanceSnapshot::EdgeSnapshot edge;
    edge.boneA             = static_cast<int>(a.value());
    edge.boneB             = static_cast<int>(b.value());
    edge.strainThreshold   = threshold.value();
    edge.accumulatedStrain = strain.value();
    edge.broken            = broken.value();
    return eve::Result<GeometryCollectionInstanceSnapshot::EdgeSnapshot>::success(edge);
}

}  // namespace

eve::Result<void> GeometryCollectionInstanceSnapshot::validate() const {
    if (bones.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot requires at least one bone", "bones"));
    if (bones.size() > 4096 || edges.size() > 16384)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot exceeds size limits", "snapshot"));
    if (!std::isfinite(originX) || !std::isfinite(originY) || !std::isfinite(originZ))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot origin must be finite", "origin"));
    if (lastTick < 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot lastTick must be non-negative", "lastTick"));
    for (const auto& bone : bones) {
        if (bone.state > 2)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "instance-snapshot bone state must be 0..2", "bones"));
        if (bone.clusterId < 0 || bone.fractureLevel < 0)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "instance-snapshot cluster fields must be non-negative",
                "bones"));
        if (!(bone.halfExtentX > 0.f) || !(bone.halfExtentY > 0.f) || !(bone.halfExtentZ > 0.f) ||
            !std::isfinite(bone.halfExtentX) || !std::isfinite(bone.halfExtentY) || !std::isfinite(bone.halfExtentZ))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "instance-snapshot half-extents must be finite and positive",
                "bones"));
    }
    for (const auto& edge : edges) {
        if (edge.boneA < 0 || edge.boneB < 0 || static_cast<std::size_t>(edge.boneA) >= bones.size() ||
            static_cast<std::size_t>(edge.boneB) >= bones.size() || edge.boneA == edge.boneB)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "instance-snapshot edge indices invalid", "edges"));
        if (!std::isfinite(edge.strainThreshold) || edge.strainThreshold <= 0.f ||
            !std::isfinite(edge.accumulatedStrain) || edge.accumulatedStrain < 0.f)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "instance-snapshot edge strain values invalid", "edges"));
    }
    return eve::Result<void>::success();
}

eve::Result<eve::Value> GeometryCollectionInstanceSnapshot::toValue() const {
    auto valid = validate();
    if (!valid) return eve::Result<eve::Value>::failure(valid.status());
    eve::Value::Array boneValues;
    boneValues.reserve(bones.size());
    for (const auto& bone : bones) {
        eve::Value::Object object;
        object["state"]         = static_cast<std::int64_t>(bone.state);
        object["anchored"]      = bone.anchored;
        object["clusterId"]     = static_cast<std::int64_t>(bone.clusterId);
        object["fractureLevel"] = static_cast<std::int64_t>(bone.fractureLevel);
        object["halfExtentX"]   = static_cast<double>(bone.halfExtentX);
        object["halfExtentY"]   = static_cast<double>(bone.halfExtentY);
        object["halfExtentZ"]   = static_cast<double>(bone.halfExtentZ);
        boneValues.push_back(eve::Value(std::move(object)));
    }
    eve::Value::Array edgeValues;
    edgeValues.reserve(edges.size());
    for (const auto& edge : edges) {
        eve::Value::Object object;
        object["boneA"]             = static_cast<std::int64_t>(edge.boneA);
        object["boneB"]             = static_cast<std::int64_t>(edge.boneB);
        object["strainThreshold"]   = static_cast<double>(edge.strainThreshold);
        object["accumulatedStrain"] = static_cast<double>(edge.accumulatedStrain);
        object["broken"]            = edge.broken;
        edgeValues.push_back(eve::Value(std::move(object)));
    }
    eve::Value::Object object;
    object["schema"]             = std::string(SchemaId);
    object["schemaVersion"]      = static_cast<std::int64_t>(SchemaVersion);
    object["originX"]            = static_cast<double>(originX);
    object["originY"]            = static_cast<double>(originY);
    object["originZ"]            = static_cast<double>(originZ);
    object["lastTick"]           = lastTick;
    object["sleepBatchRevision"] = static_cast<std::int64_t>(sleepBatchRevision);
    object["bones"]              = eve::Value(std::move(boneValues));
    object["edges"]              = eve::Value(std::move(edgeValues));
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::schema::SchemaDefinition GeometryCollectionInstanceSnapshot::schemaDefinition() {
    using eve::schema::ValueType;
    eve::schema::SchemaDefinition schema;
    schema.id                   = std::string(SchemaId);
    schema.version              = static_cast<int>(SchemaVersion);
    schema.title                = "Geometry Collection Instance Snapshot";
    schema.description          = "Runtime bone/edge state for a geometry-collection instance.";
    schema.additionalProperties = false;
    auto makeField = [](std::string name, ValueType type, bool required) {
        eve::schema::FieldDefinition field;
        field.name     = std::move(name);
        field.type     = type;
        field.required = required;
        return field;
    };
    schema.fields = {
        makeField("schema", ValueType::String, true),
        makeField("schemaVersion", ValueType::Integer, true),
        makeField("originX", ValueType::Number, true),
        makeField("originY", ValueType::Number, true),
        makeField("originZ", ValueType::Number, true),
        makeField("lastTick", ValueType::Integer, true),
        makeField("sleepBatchRevision", ValueType::Integer, true),
        makeField("bones", ValueType::Array, true),
        makeField("edges", ValueType::Array, true),
    };
    return schema;
}

eve::Result<void> GeometryCollectionInstanceSnapshot::ensureSchemaRegistered() {
    if (eve::schema::SchemaRegistry::resolve(std::string(SchemaId), static_cast<int>(SchemaVersion)))
        return eve::Result<void>::success();
    auto registration = eve::schema::SchemaRegistry::registerVersioned(schemaDefinition());
    if (!registration.ok()) return eve::Result<void>::failure(registration.status());
    return eve::Result<void>::success();
}

eve::Result<GeometryCollectionInstanceSnapshot> GeometryCollectionInstanceSnapshot::fromValue(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return eve::Result<GeometryCollectionInstanceSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot must be an object", "snapshot"));
    if (!hasExactFields(*object, {"schema", "schemaVersion", "originX", "originY", "originZ", "lastTick",
                                  "sleepBatchRevision", "bones", "edges"}))
        return eve::Result<GeometryCollectionInstanceSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot has unknown or missing fields", "snapshot"));
    const eve::Value* schemaField = field(*object, "schema");
    if (!schemaField || !schemaField->isString() || schemaField->asString() != SchemaId)
        return eve::Result<GeometryCollectionInstanceSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot schema id mismatch", "schema"));
    auto version = integer64(*object, "schemaVersion");
    if (!version) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(version.status());
    if (version.value() != static_cast<std::int64_t>(SchemaVersion))
        return eve::Result<GeometryCollectionInstanceSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "instance-snapshot schema version is unsupported", "schemaVersion"));
    GeometryCollectionInstanceSnapshot snapshot;
    auto ox = number(*object, "originX");
    if (!ox) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(ox.status());
    auto oy = number(*object, "originY");
    if (!oy) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(oy.status());
    auto oz = number(*object, "originZ");
    if (!oz) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(oz.status());
    auto lastTick = integer64(*object, "lastTick");
    if (!lastTick) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(lastTick.status());
    auto sleepRev = integer64(*object, "sleepBatchRevision");
    if (!sleepRev) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(sleepRev.status());
    if (sleepRev.value() < 0)
        return eve::Result<GeometryCollectionInstanceSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "sleepBatchRevision must be non-negative", "sleepBatchRevision"));
    snapshot.originX            = ox.value();
    snapshot.originY            = oy.value();
    snapshot.originZ            = oz.value();
    snapshot.lastTick           = lastTick.value();
    snapshot.sleepBatchRevision = static_cast<std::uint64_t>(sleepRev.value());
    const eve::Value* bonesValue = field(*object, "bones");
    const auto* boneArray = bonesValue ? bonesValue->getIf<eve::Value::Array>() : nullptr;
    if (!boneArray)
        return eve::Result<GeometryCollectionInstanceSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot bones must be an array", "bones"));
    const eve::Value* edgesValue = field(*object, "edges");
    const auto* edgeArray = edgesValue ? edgesValue->getIf<eve::Value::Array>() : nullptr;
    if (!edgeArray)
        return eve::Result<GeometryCollectionInstanceSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "instance-snapshot edges must be an array", "edges"));
    snapshot.bones.reserve(boneArray->size());
    for (const auto& boneValue : *boneArray) {
        auto bone = decodeBone(boneValue);
        if (!bone) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(bone.status());
        snapshot.bones.push_back(bone.value());
    }
    snapshot.edges.reserve(edgeArray->size());
    for (const auto& edgeValue : *edgeArray) {
        auto edge = decodeEdge(edgeValue);
        if (!edge) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(edge.status());
        snapshot.edges.push_back(edge.value());
    }
    auto valid = snapshot.validate();
    if (!valid) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(valid.status());
    auto registered = ensureSchemaRegistered();
    if (!registered) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(registered.status());
    return eve::Result<GeometryCollectionInstanceSnapshot>::success(std::move(snapshot));
}

}  // namespace eve::physics
