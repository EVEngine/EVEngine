#include "physics/destruction/GeometryCollectionAsset.h"

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
            eve::DiagnosticCode::InvalidArgument, "geometry-collection field must be an integer", std::string(name)));
    return eve::Result<std::int64_t>::success(value->asInt());
}

eve::Result<float> number(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isNumeric())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection field must be numeric", std::string(name)));
    const double parsed = value->isDouble() ? value->asDouble() : static_cast<double>(value->asInt());
    if (!std::isfinite(parsed) || parsed < -std::numeric_limits<float>::max() ||
        parsed > std::numeric_limits<float>::max())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection number must be a finite float",
            std::string(name)));
    return eve::Result<float>::success(static_cast<float>(parsed));
}

eve::Result<bool> boolean(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isBool())
        return eve::Result<bool>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection field must be boolean", std::string(name)));
    return eve::Result<bool>::success(value->asBool());
}

eve::Result<GeometryCollectionBone> decodeBone(const eve::Value& value, std::uint32_t schemaVersion) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return eve::Result<GeometryCollectionBone>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection bone must be an object", "bones"));
    const bool v1 = schemaVersion == GeometryCollectionAsset::SchemaVersionV1;
    const bool v2 = schemaVersion == GeometryCollectionAsset::SchemaVersion;
    if (!v1 && !v2)
        return eve::Result<GeometryCollectionBone>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "geometry-collection bone schema version is unsupported", "bones"));
    if (v1 && !hasExactFields(*object, {"halfExtentX", "halfExtentY", "halfExtentZ", "localX", "localY", "localZ",
                                        "mass", "density", "friction", "restitution", "anchoredDefault"}))
        return eve::Result<GeometryCollectionBone>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection bone has unknown or missing fields", "bones"));
    if (v2 && !hasExactFields(*object, {"halfExtentX", "halfExtentY", "halfExtentZ", "localX", "localY", "localZ",
                                        "mass", "density", "friction", "restitution", "anchoredDefault", "clusterId",
                                        "fractureLevel"}))
        return eve::Result<GeometryCollectionBone>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection bone has unknown or missing fields", "bones"));
    GeometryCollectionBone bone;
    auto hx = number(*object, "halfExtentX");
    if (!hx) return eve::Result<GeometryCollectionBone>::failure(hx.status());
    auto hy = number(*object, "halfExtentY");
    if (!hy) return eve::Result<GeometryCollectionBone>::failure(hy.status());
    auto hz = number(*object, "halfExtentZ");
    if (!hz) return eve::Result<GeometryCollectionBone>::failure(hz.status());
    auto lx = number(*object, "localX");
    if (!lx) return eve::Result<GeometryCollectionBone>::failure(lx.status());
    auto ly = number(*object, "localY");
    if (!ly) return eve::Result<GeometryCollectionBone>::failure(ly.status());
    auto lz = number(*object, "localZ");
    if (!lz) return eve::Result<GeometryCollectionBone>::failure(lz.status());
    auto mass = number(*object, "mass");
    if (!mass) return eve::Result<GeometryCollectionBone>::failure(mass.status());
    auto density = number(*object, "density");
    if (!density) return eve::Result<GeometryCollectionBone>::failure(density.status());
    auto friction = number(*object, "friction");
    if (!friction) return eve::Result<GeometryCollectionBone>::failure(friction.status());
    auto restitution = number(*object, "restitution");
    if (!restitution) return eve::Result<GeometryCollectionBone>::failure(restitution.status());
    auto anchored = boolean(*object, "anchoredDefault");
    if (!anchored) return eve::Result<GeometryCollectionBone>::failure(anchored.status());
    bone.halfExtentX     = hx.value();
    bone.halfExtentY     = hy.value();
    bone.halfExtentZ     = hz.value();
    bone.localX          = lx.value();
    bone.localY          = ly.value();
    bone.localZ          = lz.value();
    bone.mass            = mass.value();
    bone.density         = density.value();
    bone.friction        = friction.value();
    bone.restitution     = restitution.value();
    bone.anchoredDefault = anchored.value();
    bone.clusterId       = 0;
    bone.fractureLevel   = 0;
    if (v2) {
        auto clusterId = integer64(*object, "clusterId");
        if (!clusterId) return eve::Result<GeometryCollectionBone>::failure(clusterId.status());
        auto fractureLevel = integer64(*object, "fractureLevel");
        if (!fractureLevel) return eve::Result<GeometryCollectionBone>::failure(fractureLevel.status());
        if (clusterId.value() < std::numeric_limits<int>::min() ||
            clusterId.value() > std::numeric_limits<int>::max() ||
            fractureLevel.value() < std::numeric_limits<int>::min() ||
            fractureLevel.value() > std::numeric_limits<int>::max())
            return eve::Result<GeometryCollectionBone>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "geometry-collection cluster fields out of int range", "bones"));
        bone.clusterId     = static_cast<int>(clusterId.value());
        bone.fractureLevel = static_cast<int>(fractureLevel.value());
    }
    return eve::Result<GeometryCollectionBone>::success(std::move(bone));
}

eve::Result<GeometryCollectionEdge> decodeEdge(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return eve::Result<GeometryCollectionEdge>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection edge must be an object", "edges"));
    if (!hasExactFields(*object, {"boneA", "boneB", "strainThreshold"}))
        return eve::Result<GeometryCollectionEdge>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection edge has unknown or missing fields", "edges"));
    auto a = integer64(*object, "boneA");
    if (!a) return eve::Result<GeometryCollectionEdge>::failure(a.status());
    auto b = integer64(*object, "boneB");
    if (!b) return eve::Result<GeometryCollectionEdge>::failure(b.status());
    auto threshold = number(*object, "strainThreshold");
    if (!threshold) return eve::Result<GeometryCollectionEdge>::failure(threshold.status());
    if (a.value() < std::numeric_limits<int>::min() || a.value() > std::numeric_limits<int>::max() ||
        b.value() < std::numeric_limits<int>::min() || b.value() > std::numeric_limits<int>::max())
        return eve::Result<GeometryCollectionEdge>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection edge bone index is out of int range", "edges"));
    GeometryCollectionEdge edge;
    edge.boneA           = static_cast<int>(a.value());
    edge.boneB           = static_cast<int>(b.value());
    edge.strainThreshold = threshold.value();
    return eve::Result<GeometryCollectionEdge>::success(edge);
}

eve::Value encodeBone(const GeometryCollectionBone& bone) {
    eve::Value::Object object;
    object["halfExtentX"]     = static_cast<double>(bone.halfExtentX);
    object["halfExtentY"]     = static_cast<double>(bone.halfExtentY);
    object["halfExtentZ"]     = static_cast<double>(bone.halfExtentZ);
    object["localX"]          = static_cast<double>(bone.localX);
    object["localY"]          = static_cast<double>(bone.localY);
    object["localZ"]          = static_cast<double>(bone.localZ);
    object["mass"]            = static_cast<double>(bone.mass);
    object["density"]         = static_cast<double>(bone.density);
    object["friction"]        = static_cast<double>(bone.friction);
    object["restitution"]     = static_cast<double>(bone.restitution);
    object["anchoredDefault"] = bone.anchoredDefault;
    object["clusterId"]       = static_cast<std::int64_t>(bone.clusterId);
    object["fractureLevel"]   = static_cast<std::int64_t>(bone.fractureLevel);
    return eve::Value(std::move(object));
}

eve::Value encodeEdge(const GeometryCollectionEdge& edge) {
    eve::Value::Object object;
    object["boneA"]           = static_cast<std::int64_t>(edge.boneA);
    object["boneB"]           = static_cast<std::int64_t>(edge.boneB);
    object["strainThreshold"] = static_cast<double>(edge.strainThreshold);
    return eve::Value(std::move(object));
}

}  // namespace

eve::Result<void> GeometryCollectionAsset::validate() const {
    if (bones.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "geometry-collection requires at least one bone",
                                                                 "bones"));
    if (bones.size() > 4096)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection exceeds the 4096-bone limit", "bones"));
    if (edges.size() > 16384)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection exceeds the 16384-edge limit", "edges"));
    const auto finitePositive = [](float value) { return std::isfinite(value) && value > 0.f; };
    const auto finiteNonNeg = [](float value) { return std::isfinite(value) && value >= 0.f; };
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const auto& bone = bones[i];
        if (!finitePositive(bone.halfExtentX) || !finitePositive(bone.halfExtentY) || !finitePositive(bone.halfExtentZ))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "bone half-extents must be finite and positive", "bones"));
        if (!std::isfinite(bone.localX) || !std::isfinite(bone.localY) || !std::isfinite(bone.localZ))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "bone local pose must be finite", "bones"));
        if (!finitePositive(bone.mass) || !finitePositive(bone.density))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "bone mass and density must be finite and positive", "bones"));
        if (!finiteNonNeg(bone.friction) || bone.friction > 1.f || !finiteNonNeg(bone.restitution) ||
            bone.restitution > 1.f)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "bone friction/restitution must be in [0, 1]", "bones"));
        if (bone.clusterId < 0 || bone.fractureLevel < 0)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "bone clusterId and fractureLevel must be non-negative",
                "bones"));
        (void)i;
    }
    for (const auto& edge : edges) {
        if (edge.boneA < 0 || edge.boneB < 0 || static_cast<std::size_t>(edge.boneA) >= bones.size() ||
            static_cast<std::size_t>(edge.boneB) >= bones.size() || edge.boneA == edge.boneB)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "edge bone indices must name two distinct bones", "edges"));
        if (!std::isfinite(edge.strainThreshold) || edge.strainThreshold <= 0.f)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "edge strainThreshold must be finite and positive", "edges"));
    }
    return eve::Result<void>::success();
}

eve::Result<eve::Value> GeometryCollectionAsset::toValue() const {
    auto valid = validate();
    if (!valid) return eve::Result<eve::Value>::failure(valid.status());
    eve::Value::Array boneValues;
    boneValues.reserve(bones.size());
    for (const auto& bone : bones) boneValues.push_back(encodeBone(bone));
    eve::Value::Array edgeValues;
    edgeValues.reserve(edges.size());
    for (const auto& edge : edges) edgeValues.push_back(encodeEdge(edge));
    eve::Value::Object object;
    object["schema"]        = std::string(SchemaId);
    object["schemaVersion"] = static_cast<std::int64_t>(SchemaVersion);
    object["bones"]         = eve::Value(std::move(boneValues));
    object["edges"]         = eve::Value(std::move(edgeValues));
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::schema::SchemaDefinition GeometryCollectionAsset::schemaDefinition() {
    using eve::schema::ValueType;
    eve::schema::SchemaDefinition schema;
    schema.id                   = std::string(SchemaId);
    schema.version              = static_cast<int>(SchemaVersion);
    schema.title                = "Geometry Collection Asset";
    schema.description =
        "Pre-authored bones (with cluster membership) and connection graph for Chaos-style destruction.";
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
        makeField("bones", ValueType::Array, true),
        makeField("edges", ValueType::Array, true),
    };
    return schema;
}

eve::Result<void> GeometryCollectionAsset::ensureSchemaRegistered() {
    if (eve::schema::SchemaRegistry::resolve(std::string(SchemaId), static_cast<int>(SchemaVersion)))
        return eve::Result<void>::success();
    auto registration = eve::schema::SchemaRegistry::registerVersioned(schemaDefinition());
    if (!registration.ok()) return eve::Result<void>::failure(registration.status());
    return eve::Result<void>::success();
}

eve::Result<GeometryCollectionAsset> GeometryCollectionAsset::fromValue(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return eve::Result<GeometryCollectionAsset>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection asset must be an object", "asset"));
    if (!hasExactFields(*object, {"schema", "schemaVersion", "bones", "edges"}))
        return eve::Result<GeometryCollectionAsset>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection asset has unknown or missing fields", "asset"));
    const eve::Value* schemaField = field(*object, "schema");
    if (!schemaField || !schemaField->isString() || schemaField->asString() != SchemaId)
        return eve::Result<GeometryCollectionAsset>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection schema id mismatch", "schema"));
    auto version = integer64(*object, "schemaVersion");
    if (!version) return eve::Result<GeometryCollectionAsset>::failure(version.status());
    if (version.value() != static_cast<std::int64_t>(SchemaVersion) &&
        version.value() != static_cast<std::int64_t>(SchemaVersionV1))
        return eve::Result<GeometryCollectionAsset>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "geometry-collection schema version is unsupported", "schemaVersion"));
    const auto schemaVersion = static_cast<std::uint32_t>(version.value());
    const eve::Value* bonesValue = field(*object, "bones");
    const auto* boneArray = bonesValue ? bonesValue->getIf<eve::Value::Array>() : nullptr;
    if (!boneArray)
        return eve::Result<GeometryCollectionAsset>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection bones must be an array", "bones"));
    const eve::Value* edgesValue = field(*object, "edges");
    const auto* edgeArray = edgesValue ? edgesValue->getIf<eve::Value::Array>() : nullptr;
    if (!edgeArray)
        return eve::Result<GeometryCollectionAsset>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection edges must be an array", "edges"));
    GeometryCollectionAsset asset;
    asset.bones.reserve(boneArray->size());
    for (const auto& boneValue : *boneArray) {
        auto bone = decodeBone(boneValue, schemaVersion);
        if (!bone) return eve::Result<GeometryCollectionAsset>::failure(bone.status());
        asset.bones.push_back(std::move(bone.value()));
    }
    asset.edges.reserve(edgeArray->size());
    for (const auto& edgeValue : *edgeArray) {
        auto edge = decodeEdge(edgeValue);
        if (!edge) return eve::Result<GeometryCollectionAsset>::failure(edge.status());
        asset.edges.push_back(edge.value());
    }
    auto valid = asset.validate();
    if (!valid) return eve::Result<GeometryCollectionAsset>::failure(valid.status());
    auto registered = ensureSchemaRegistered();
    if (!registered) return eve::Result<GeometryCollectionAsset>::failure(registered.status());
    return eve::Result<GeometryCollectionAsset>::success(std::move(asset));
}

eve::Result<GeometryCollectionAsset> GeometryCollectionAsset::makeWeldedBoxesFixture(float strainThreshold) {
    GeometryCollectionAsset asset;
    GeometryCollectionBone left;
    left.halfExtentX = 0.5f;
    left.halfExtentY = 0.5f;
    left.halfExtentZ = 0.5f;
    left.localX      = -0.55f;
    left.localY      = 1.0f;
    left.mass        = 2.f;
    GeometryCollectionBone right = left;
    right.localX                 = 0.55f;
    asset.bones.push_back(left);
    asset.bones.push_back(right);
    GeometryCollectionEdge edge;
    edge.boneA           = 0;
    edge.boneB           = 1;
    edge.strainThreshold = strainThreshold;
    asset.edges.push_back(edge);
    auto valid = asset.validate();
    if (!valid) return eve::Result<GeometryCollectionAsset>::failure(valid.status());
    return eve::Result<GeometryCollectionAsset>::success(std::move(asset));
}

}  // namespace eve::physics
