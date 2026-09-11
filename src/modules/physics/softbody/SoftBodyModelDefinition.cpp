#include "physics/softbody/SoftBodyModelDefinition.h"

#include "schema/SchemaRegistry.h"

#include <cmath>
#include <initializer_list>
#include <string>

namespace eve::physics {
namespace {
template <typename T>
eve::Result<T> invalid(std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

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

std::string_view surfaceName(SoftBodySurfaceSampling value) {
    switch (value) {
        case SoftBodySurfaceSampling::None: return "none";
        case SoftBodySurfaceSampling::Vertices: return "vertices";
        case SoftBodySurfaceSampling::Voxels: return "voxels";
    }
    return "";
}

std::string_view volumeName(SoftBodyVolumeSampling value) {
    switch (value) {
        case SoftBodyVolumeSampling::None: return "none";
        case SoftBodyVolumeSampling::Voxels: return "voxels";
    }
    return "";
}

}  // namespace

eve::Result<void> SoftBodyModelDefinition::validate() const {
    if (sourceMesh.empty()) return invalid<void>("sourceMesh must not be empty", "sourceMesh");
    if (surfaceName(surfaceSampling).empty())
        return invalid<void>("unsupported surface sampling mode", "surfaceSampling");
    if (volumeName(volumeSampling).empty())
        return invalid<void>("unsupported volume sampling mode", "volumeSampling");
    if (surfaceResolution < 2 || surfaceResolution > 128 || volumeResolution < 2 || volumeResolution > 128 ||
        shapeResolution < 2 || shapeResolution > 128)
        return invalid<void>("sampling resolutions must be in [2, 128]", "resolution");
    if (!std::isfinite(maxAnisotropy) || maxAnisotropy < 1.f || maxAnisotropy > 5.f)
        return invalid<void>("maxAnisotropy must be in [1, 5]", "maxAnisotropy");
    if (!std::isfinite(smoothing) || smoothing < 0.f || smoothing > 1.f)
        return invalid<void>("smoothing must be in [0, 1]", "smoothing");
    if (surfaceSampling == SoftBodySurfaceSampling::None && volumeSampling == SoftBodyVolumeSampling::None)
        return invalid<void>("at least one sampling mode must be enabled", "sampling");
    return eve::Result<void>::success();
}

eve::Result<eve::Value> SoftBodyModelDefinition::toValue() const {
    auto valid = validate();
    if (!valid) return eve::Result<eve::Value>::failure(valid.status());
    eve::Value::Object object;
    object["schema"]             = std::string(SchemaId);
    object["schemaVersion"]      = static_cast<std::int64_t>(SchemaVersion);
    object["sourceMesh"]         = sourceMesh;
    object["surfaceSampling"]    = std::string(surfaceName(surfaceSampling));
    object["surfaceResolution"]  = static_cast<std::int64_t>(surfaceResolution);
    object["volumeSampling"]     = std::string(volumeName(volumeSampling));
    object["volumeResolution"]   = static_cast<std::int64_t>(volumeResolution);
    object["shapeResolution"]    = static_cast<std::int64_t>(shapeResolution);
    object["maxAnisotropy"]      = maxAnisotropy;
    object["smoothing"]          = smoothing;
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::Result<SoftBodyModelDefinition> SoftBodyModelDefinition::fromValue(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object) return invalid<SoftBodyModelDefinition>("soft-body model must be an object", "softbodyModel");
    const std::initializer_list<std::string_view> names = {
        "schema",          "schemaVersion",     "sourceMesh",       "surfaceSampling", "surfaceResolution",
        "volumeSampling",  "volumeResolution", "shapeResolution", "maxAnisotropy",   "smoothing"};
    if (!hasExactFields(*object, names))
        return invalid<SoftBodyModelDefinition>("soft-body model fields do not match version 1", "softbodyModel");
    const auto* schema = field(*object, "schema");
    const auto* version = field(*object, "schemaVersion");
    const auto* mesh = field(*object, "sourceMesh");
    const auto* surface = field(*object, "surfaceSampling");
    const auto* volume = field(*object, "volumeSampling");
    if (!schema || !schema->isString() || schema->asString() != SchemaId)
        return invalid<SoftBodyModelDefinition>("unexpected soft-body model schema", "schema");
    if (!version || !version->isInt64() || version->asInt() != SchemaVersion)
        return invalid<SoftBodyModelDefinition>("unsupported soft-body model version", "schemaVersion");
    if (!mesh || !mesh->isString() || !surface || !surface->isString() || !volume || !volume->isString())
        return invalid<SoftBodyModelDefinition>("model string field has the wrong type", "softbodyModel");

    SoftBodyModelDefinition result;
    result.sourceMesh = mesh->asString();
    if (surface->asString() == "none") result.surfaceSampling = SoftBodySurfaceSampling::None;
    else if (surface->asString() == "vertices") result.surfaceSampling = SoftBodySurfaceSampling::Vertices;
    else if (surface->asString() == "voxels") result.surfaceSampling = SoftBodySurfaceSampling::Voxels;
    else return invalid<SoftBodyModelDefinition>("unsupported surface sampling mode", "surfaceSampling");
    if (volume->asString() == "none") result.volumeSampling = SoftBodyVolumeSampling::None;
    else if (volume->asString() == "voxels") result.volumeSampling = SoftBodyVolumeSampling::Voxels;
    else return invalid<SoftBodyModelDefinition>("unsupported volume sampling mode", "volumeSampling");

    auto integer = [&](std::string_view name, int& destination) -> bool {
        const auto* item = field(*object, name);
        if (!item || !item->isInt64() || item->asInt() < 0 || item->asInt() > 1000000) return false;
        destination = static_cast<int>(item->asInt());
        return true;
    };
    auto number = [&](std::string_view name, float& destination) -> bool {
        const auto* item = field(*object, name);
        if (!item || !item->isNumeric()) return false;
        const double parsed = item->isDouble() ? item->asDouble() : static_cast<double>(item->asInt());
        if (!std::isfinite(parsed)) return false;
        destination = static_cast<float>(parsed);
        return true;
    };
    if (!integer("surfaceResolution", result.surfaceResolution) ||
        !integer("volumeResolution", result.volumeResolution) || !integer("shapeResolution", result.shapeResolution) ||
        !number("maxAnisotropy", result.maxAnisotropy) || !number("smoothing", result.smoothing))
        return invalid<SoftBodyModelDefinition>("model numeric field has the wrong type", "softbodyModel");
    auto valid = result.validate();
    if (!valid) return eve::Result<SoftBodyModelDefinition>::failure(valid.status());
    return eve::Result<SoftBodyModelDefinition>::success(std::move(result));
}

eve::schema::SchemaDefinition SoftBodyModelDefinition::schemaDefinition() {
    using eve::schema::FieldDefinition;
    using eve::schema::ValueType;
    auto field = [](const char* name, ValueType type, const char* title, std::string defaultJson) {
        FieldDefinition result;
        result.name = name;
        result.type = type;
        result.required = true;
        result.title = title;
        result.defaultJson = std::move(defaultJson);
        return result;
    };
    auto resolution = [&](const char* name, const char* title, int value) {
        auto result = field(name, ValueType::Integer, title, std::to_string(value));
        result.minimum = 2;
        result.maximum = 128;
        return result;
    };
    eve::schema::SchemaDefinition schema;
    schema.id = std::string(SchemaId);
    schema.version = static_cast<int>(SchemaVersion);
    schema.title = "Soft body model";
    schema.description = "Mesh source and deterministic sampling settings used to cook a SoftBodyModel.";
    schema.additionalProperties = false;
    schema.fields = {field("schema", ValueType::String, "Schema", "\"physics:softbody-model\""),
                     resolution("schemaVersion", "Schema version", 1),
                     field("sourceMesh", ValueType::String, "Source mesh", "\"\""),
                     field("surfaceSampling", ValueType::String, "Surface sampling", "\"vertices\""),
                     resolution("surfaceResolution", "Surface resolution", 16),
                     field("volumeSampling", ValueType::String, "Volume sampling", "\"none\""),
                     resolution("volumeResolution", "Volume resolution", 16),
                     resolution("shapeResolution", "Shape resolution", 48),
                     field("maxAnisotropy", ValueType::Number, "Maximum anisotropy", "3.0"),
                     field("smoothing", ValueType::Number, "Smoothing", "0.25")};
    schema.fields[1].minimum = schema.fields[1].maximum = 1;
    schema.fields[8].minimum = 1;
    schema.fields[8].maximum = 5;
    schema.fields[9].minimum = 0;
    schema.fields[9].maximum = 1;
    return schema;
}

eve::Result<void> SoftBodyModelDefinition::ensureSchemaRegistered() {
    if (eve::schema::SchemaRegistry::resolve(std::string(SchemaId), static_cast<int>(SchemaVersion)))
        return eve::Result<void>::success();
    auto registered = eve::schema::SchemaRegistry::registerVersioned(schemaDefinition());
    if (!registered) return eve::Result<void>::failure(registered.status());
    return eve::Result<void>::success();
}

}  // namespace eve::physics
