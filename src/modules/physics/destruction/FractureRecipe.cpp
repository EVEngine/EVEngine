#include "physics/destruction/FractureRecipe.h"

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
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe field must be an integer", std::string(name)));
    return eve::Result<std::int64_t>::success(value->asInt());
}

eve::Result<float> number(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isNumeric())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe field must be numeric", std::string(name)));
    const double parsed = value->isDouble() ? value->asDouble() : static_cast<double>(value->asInt());
    if (!std::isfinite(parsed) || parsed < -std::numeric_limits<float>::max() ||
        parsed > std::numeric_limits<float>::max())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe number must be a finite float", std::string(name)));
    return eve::Result<float>::success(static_cast<float>(parsed));
}

eve::Result<std::string> stringField(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isString())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe field must be a string", std::string(name)));
    return eve::Result<std::string>::success(value->asString());
}

eve::Result<std::uint64_t> u64Field(const eve::Value::Object& object, std::string_view name) {
    auto parsed = integer64(object, name);
    if (!parsed) return eve::Result<std::uint64_t>::failure(parsed.status());
    if (parsed.value() < 0)
        return eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe seed must be non-negative", std::string(name)));
    return eve::Result<std::uint64_t>::success(static_cast<std::uint64_t>(parsed.value()));
}

eve::Result<std::vector<float>> floatArray(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    const auto* array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array)
        return eve::Result<std::vector<float>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe field must be an array", std::string(name)));
    std::vector<float> out;
    out.reserve(array->size());
    for (const auto& item : *array) {
        if (!item.isNumeric())
            return eve::Result<std::vector<float>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "fracture-recipe array entries must be numeric",
                std::string(name)));
        const double parsed = item.isDouble() ? item.asDouble() : static_cast<double>(item.asInt());
        if (!std::isfinite(parsed))
            return eve::Result<std::vector<float>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "fracture-recipe array entries must be finite",
                std::string(name)));
        out.push_back(static_cast<float>(parsed));
    }
    return eve::Result<std::vector<float>>::success(std::move(out));
}

const char* modeName(FractureMode mode) {
    switch (mode) {
        case FractureMode::UniformVoronoi: return "UniformVoronoi";
        case FractureMode::ClusteredVoronoi: return "ClusteredVoronoi";
        case FractureMode::Radial: return "Radial";
        case FractureMode::Planar: return "Planar";
    }
    return "UniformVoronoi";
}

eve::Result<FractureMode> parseMode(const std::string& name) {
    if (name == "UniformVoronoi") return eve::Result<FractureMode>::success(FractureMode::UniformVoronoi);
    if (name == "ClusteredVoronoi") return eve::Result<FractureMode>::success(FractureMode::ClusteredVoronoi);
    if (name == "Radial") return eve::Result<FractureMode>::success(FractureMode::Radial);
    if (name == "Planar") return eve::Result<FractureMode>::success(FractureMode::Planar);
    return eve::Result<FractureMode>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "unknown fracture mode", "mode"));
}

}  // namespace

eve::Result<void> FractureRecipe::validate() const {
    if (siteCountMin < 1 || siteCountMax < siteCountMin || siteCountMax > 4096)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "siteCountMin/Max must satisfy 1 <= min <= max <= 4096",
            "siteCount"));
    if (clusterCount < 1 || clusterCount > 256)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "clusterCount must be in [1, 256]", "clusterCount"));
    if (!std::isfinite(clusterRadius) || clusterRadius <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "clusterRadius must be finite and positive", "clusterRadius"));
    if (radialPlanes < 0 || radialPlanes > 32 || radialSpokes < 1 || radialSpokes > 64)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "radialPlanes/Spokes are outside supported ranges", "radial"));
    if (planeNormals.size() % 3 != 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "planeNormals must contain xyz triples", "planeNormals"));
    if (planeOffsets.size() != planeNormals.size() / 3)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "planeOffsets must match planeNormals triple count", "planeOffsets"));
    for (float value : planeNormals) {
        if (!std::isfinite(value))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "planeNormals must be finite", "planeNormals"));
    }
    for (float value : planeOffsets) {
        if (!std::isfinite(value))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "planeOffsets must be finite", "planeOffsets"));
    }
    if (mode == FractureMode::Planar && planeOffsets.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Planar mode requires at least one plane", "planeOffsets"));
    if (randomStreamName.empty() || randomStreamName.size() > 128)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "randomStreamName must be a non-empty string up to 128 chars",
            "randomStreamName"));
    if (!std::isfinite(defaultStrainThreshold) || defaultStrainThreshold <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "defaultStrainThreshold must be finite and positive",
            "defaultStrainThreshold"));
    if (!std::isfinite(defaultDensity) || defaultDensity <= 0.f || !std::isfinite(defaultFriction) ||
        defaultFriction < 0.f || defaultFriction > 1.f || !std::isfinite(defaultRestitution) ||
        defaultRestitution < 0.f || defaultRestitution > 1.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "default material fields are outside supported ranges",
            "material"));
    if (!std::isfinite(minimumThickness) || minimumThickness <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "minimumThickness must be finite and positive",
            "minimumThickness"));
    if (maximumBones < 1 || maximumBones > 4096)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "maximumBones must be in [1, 4096]", "maximumBones"));
    if (interiorMaterialLogicalId.size() > 256)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "interiorMaterialLogicalId exceeds 256 chars",
            "interiorMaterialLogicalId"));
    return eve::Result<void>::success();
}

eve::Result<eve::Value> FractureRecipe::toValue() const {
    auto valid = validate();
    if (!valid) return eve::Result<eve::Value>::failure(valid.status());
    eve::Value::Array normals;
    normals.reserve(planeNormals.size());
    for (float value : planeNormals) normals.push_back(static_cast<double>(value));
    eve::Value::Array offsets;
    offsets.reserve(planeOffsets.size());
    for (float value : planeOffsets) offsets.push_back(static_cast<double>(value));
    eve::Value::Object object;
    object["schema"] = std::string(SchemaId);
    object["schemaVersion"] = static_cast<std::int64_t>(SchemaVersion);
    object["mode"] = std::string(modeName(mode));
    object["siteCountMin"] = static_cast<std::int64_t>(siteCountMin);
    object["siteCountMax"] = static_cast<std::int64_t>(siteCountMax);
    object["clusterCount"] = static_cast<std::int64_t>(clusterCount);
    object["clusterRadius"] = static_cast<double>(clusterRadius);
    object["radialPlanes"] = static_cast<std::int64_t>(radialPlanes);
    object["radialSpokes"] = static_cast<std::int64_t>(radialSpokes);
    object["planeNormals"] = eve::Value(std::move(normals));
    object["planeOffsets"] = eve::Value(std::move(offsets));
    object["interiorMaterialLogicalId"] = interiorMaterialLogicalId;
    object["randomStreamName"] = randomStreamName;
    object["seed"] = static_cast<std::int64_t>(seed);
    object["defaultStrainThreshold"] = static_cast<double>(defaultStrainThreshold);
    object["defaultDensity"] = static_cast<double>(defaultDensity);
    object["defaultFriction"] = static_cast<double>(defaultFriction);
    object["defaultRestitution"] = static_cast<double>(defaultRestitution);
    object["minimumThickness"] = static_cast<double>(minimumThickness);
    object["maximumBones"] = static_cast<std::int64_t>(maximumBones);
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::schema::SchemaDefinition FractureRecipe::schemaDefinition() {
    using eve::schema::ValueType;
    eve::schema::SchemaDefinition schema;
    schema.id = std::string(SchemaId);
    schema.version = static_cast<int>(SchemaVersion);
    schema.title = "Fracture Recipe";
    schema.description = "Deterministic cook recipe for geometry-collection pre-fracture.";
    schema.additionalProperties = false;
    auto makeField = [](std::string name, ValueType type, bool required) {
        eve::schema::FieldDefinition field;
        field.name = std::move(name);
        field.type = type;
        field.required = required;
        return field;
    };
    schema.fields = {
        makeField("schema", ValueType::String, true),
        makeField("schemaVersion", ValueType::Integer, true),
        makeField("mode", ValueType::String, true),
        makeField("siteCountMin", ValueType::Integer, true),
        makeField("siteCountMax", ValueType::Integer, true),
        makeField("clusterCount", ValueType::Integer, true),
        makeField("clusterRadius", ValueType::Number, true),
        makeField("radialPlanes", ValueType::Integer, true),
        makeField("radialSpokes", ValueType::Integer, true),
        makeField("planeNormals", ValueType::Array, true),
        makeField("planeOffsets", ValueType::Array, true),
        makeField("interiorMaterialLogicalId", ValueType::String, true),
        makeField("randomStreamName", ValueType::String, true),
        makeField("seed", ValueType::Integer, true),
        makeField("defaultStrainThreshold", ValueType::Number, true),
        makeField("defaultDensity", ValueType::Number, true),
        makeField("defaultFriction", ValueType::Number, true),
        makeField("defaultRestitution", ValueType::Number, true),
        makeField("minimumThickness", ValueType::Number, true),
        makeField("maximumBones", ValueType::Integer, true),
    };
    return schema;
}

eve::Result<void> FractureRecipe::ensureSchemaRegistered() {
    if (eve::schema::SchemaRegistry::resolve(std::string(SchemaId), static_cast<int>(SchemaVersion)))
        return eve::Result<void>::success();
    auto registration = eve::schema::SchemaRegistry::registerVersioned(schemaDefinition());
    if (!registration.ok()) return eve::Result<void>::failure(registration.status());
    return eve::Result<void>::success();
}

eve::Result<FractureRecipe> FractureRecipe::fromValue(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return eve::Result<FractureRecipe>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe must be an object", "recipe"));
    if (!hasExactFields(*object,
                        {"schema", "schemaVersion", "mode", "siteCountMin", "siteCountMax", "clusterCount",
                         "clusterRadius", "radialPlanes", "radialSpokes", "planeNormals", "planeOffsets",
                         "interiorMaterialLogicalId", "randomStreamName", "seed", "defaultStrainThreshold",
                         "defaultDensity", "defaultFriction", "defaultRestitution", "minimumThickness",
                         "maximumBones"}))
        return eve::Result<FractureRecipe>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe has unknown or missing fields", "recipe"));
    const eve::Value* schemaField = field(*object, "schema");
    if (!schemaField || !schemaField->isString() || schemaField->asString() != SchemaId)
        return eve::Result<FractureRecipe>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture-recipe schema id mismatch", "schema"));
    auto version = integer64(*object, "schemaVersion");
    if (!version) return eve::Result<FractureRecipe>::failure(version.status());
    if (version.value() != static_cast<std::int64_t>(SchemaVersion))
        return eve::Result<FractureRecipe>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "fracture-recipe schema version is unsupported", "schemaVersion"));
    auto modeString = stringField(*object, "mode");
    if (!modeString) return eve::Result<FractureRecipe>::failure(modeString.status());
    auto mode = parseMode(modeString.value());
    if (!mode) return eve::Result<FractureRecipe>::failure(mode.status());

    FractureRecipe recipe;
    recipe.mode = mode.value();
    auto siteMin = integer64(*object, "siteCountMin");
    if (!siteMin) return eve::Result<FractureRecipe>::failure(siteMin.status());
    auto siteMax = integer64(*object, "siteCountMax");
    if (!siteMax) return eve::Result<FractureRecipe>::failure(siteMax.status());
    auto clusters = integer64(*object, "clusterCount");
    if (!clusters) return eve::Result<FractureRecipe>::failure(clusters.status());
    auto clusterRadius = number(*object, "clusterRadius");
    if (!clusterRadius) return eve::Result<FractureRecipe>::failure(clusterRadius.status());
    auto radialPlanes = integer64(*object, "radialPlanes");
    if (!radialPlanes) return eve::Result<FractureRecipe>::failure(radialPlanes.status());
    auto radialSpokes = integer64(*object, "radialSpokes");
    if (!radialSpokes) return eve::Result<FractureRecipe>::failure(radialSpokes.status());
    auto normals = floatArray(*object, "planeNormals");
    if (!normals) return eve::Result<FractureRecipe>::failure(normals.status());
    auto offsets = floatArray(*object, "planeOffsets");
    if (!offsets) return eve::Result<FractureRecipe>::failure(offsets.status());
    auto interior = stringField(*object, "interiorMaterialLogicalId");
    if (!interior) return eve::Result<FractureRecipe>::failure(interior.status());
    auto stream = stringField(*object, "randomStreamName");
    if (!stream) return eve::Result<FractureRecipe>::failure(stream.status());
    auto seed = u64Field(*object, "seed");
    if (!seed) return eve::Result<FractureRecipe>::failure(seed.status());
    auto strain = number(*object, "defaultStrainThreshold");
    if (!strain) return eve::Result<FractureRecipe>::failure(strain.status());
    auto density = number(*object, "defaultDensity");
    if (!density) return eve::Result<FractureRecipe>::failure(density.status());
    auto friction = number(*object, "defaultFriction");
    if (!friction) return eve::Result<FractureRecipe>::failure(friction.status());
    auto restitution = number(*object, "defaultRestitution");
    if (!restitution) return eve::Result<FractureRecipe>::failure(restitution.status());
    auto thickness = number(*object, "minimumThickness");
    if (!thickness) return eve::Result<FractureRecipe>::failure(thickness.status());
    auto maxBones = integer64(*object, "maximumBones");
    if (!maxBones) return eve::Result<FractureRecipe>::failure(maxBones.status());

    auto clampInt = [](std::int64_t value) -> eve::Result<int> {
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            return eve::Result<int>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "integer field exceeds int range", "recipe"));
        return eve::Result<int>::success(static_cast<int>(value));
    };
    auto siteMinI = clampInt(siteMin.value());
    if (!siteMinI) return eve::Result<FractureRecipe>::failure(siteMinI.status());
    auto siteMaxI = clampInt(siteMax.value());
    if (!siteMaxI) return eve::Result<FractureRecipe>::failure(siteMaxI.status());
    auto clustersI = clampInt(clusters.value());
    if (!clustersI) return eve::Result<FractureRecipe>::failure(clustersI.status());
    auto radialPlanesI = clampInt(radialPlanes.value());
    if (!radialPlanesI) return eve::Result<FractureRecipe>::failure(radialPlanesI.status());
    auto radialSpokesI = clampInt(radialSpokes.value());
    if (!radialSpokesI) return eve::Result<FractureRecipe>::failure(radialSpokesI.status());
    auto maxBonesI = clampInt(maxBones.value());
    if (!maxBonesI) return eve::Result<FractureRecipe>::failure(maxBonesI.status());

    recipe.siteCountMin = siteMinI.value();
    recipe.siteCountMax = siteMaxI.value();
    recipe.clusterCount = clustersI.value();
    recipe.clusterRadius = clusterRadius.value();
    recipe.radialPlanes = radialPlanesI.value();
    recipe.radialSpokes = radialSpokesI.value();
    recipe.planeNormals = std::move(normals.value());
    recipe.planeOffsets = std::move(offsets.value());
    recipe.interiorMaterialLogicalId = std::move(interior.value());
    recipe.randomStreamName = std::move(stream.value());
    recipe.seed = seed.value();
    recipe.defaultStrainThreshold = strain.value();
    recipe.defaultDensity = density.value();
    recipe.defaultFriction = friction.value();
    recipe.defaultRestitution = restitution.value();
    recipe.minimumThickness = thickness.value();
    recipe.maximumBones = maxBonesI.value();

    auto valid = recipe.validate();
    if (!valid) return eve::Result<FractureRecipe>::failure(valid.status());
    auto registered = ensureSchemaRegistered();
    if (!registered) return eve::Result<FractureRecipe>::failure(registered.status());
    return eve::Result<FractureRecipe>::success(std::move(recipe));
}

}  // namespace eve::physics
