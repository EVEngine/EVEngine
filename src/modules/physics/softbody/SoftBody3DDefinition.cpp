#include "physics/softbody/SoftBody3DDefinition.h"

#include "schema/SchemaRegistry.h"

#include <cmath>
#include <initializer_list>
#include <limits>
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

eve::Result<int> integer(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isInt64()) return invalid<int>("soft-body field must be an integer", std::string(name));
    const auto parsed = value->asInt();
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
        return invalid<int>("soft-body integer is outside int range", std::string(name));
    return eve::Result<int>::success(static_cast<int>(parsed));
}

eve::Result<float> number(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isNumeric()) return invalid<float>("soft-body field must be numeric", std::string(name));
    const double parsed = value->isDouble() ? value->asDouble() : static_cast<double>(value->asInt());
    if (!std::isfinite(parsed) || parsed < -std::numeric_limits<float>::max() ||
        parsed > std::numeric_limits<float>::max())
        return invalid<float>("soft-body number must be a finite float", std::string(name));
    return eve::Result<float>::success(static_cast<float>(parsed));
}

eve::Result<bool> boolean(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isBool()) return invalid<bool>("soft-body field must be boolean", std::string(name));
    return eve::Result<bool>::success(value->asBool());
}

}  // namespace

eve::Result<void> SoftBody3DDefinition::validate() const {
    if (cols < 2 || rows < 2 || layers < 2)
        return invalid<void>("soft-body lattice dimensions must each be at least 2", "dimensions");
    const std::int64_t count = std::int64_t(cols) * std::int64_t(rows) * std::int64_t(layers);
    if (count > 1000000) return invalid<void>("soft-body exceeds the one-million-particle limit", "dimensions");
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!finite(spacing) || spacing <= 0.f) return invalid<void>("spacing must be finite and positive", "spacing");
    if (!finite(originX) || !finite(originY) || !finite(originZ))
        return invalid<void>("origin must contain finite coordinates", "origin");
    if (!finite(gravityX) || !finite(gravityY) || !finite(gravityZ))
        return invalid<void>("gravity must contain finite coordinates", "gravity");
    if (!finite(deformationResistance) || deformationResistance < 0.f || deformationResistance > 1.f)
        return invalid<void>("deformationResistance must be in [0, 1]", "deformationResistance");
    if (iterations < 1 || iterations > 32) return invalid<void>("iterations must be in [1, 32]", "iterations");
    if (!finite(damping) || damping < 0.f || damping > 1.f)
        return invalid<void>("damping must be in [0, 1]", "damping");
    if (!finite(particleRadius) || particleRadius <= 0.f)
        return invalid<void>("particleRadius must be finite and positive", "particleRadius");
    if (!finite(particleMass) || particleMass <= 0.f)
        return invalid<void>("particleMass must be finite and positive", "particleMass");
    if (!finite(plasticYield) || !finite(plasticCreep) || !finite(plasticRecovery) || !finite(maxDeformation) ||
        plasticYield < 0.f || plasticCreep < 0.f || plasticCreep > 1.f || plasticRecovery < 0.f ||
        plasticRecovery > 1.f || maxDeformation < 0.f)
        return invalid<void>("plasticity fields are outside their supported ranges", "plasticity");
    return eve::Result<void>::success();
}

eve::Result<eve::Value> SoftBody3DDefinition::toValue() const {
    auto valid = validate();
    if (!valid) return eve::Result<eve::Value>::failure(valid.status());
    eve::Value::Object object;
    object["schema"]                = std::string(SchemaId);
    object["schemaVersion"]         = static_cast<std::int64_t>(SchemaVersion);
    object["cols"]                  = static_cast<std::int64_t>(cols);
    object["rows"]                  = static_cast<std::int64_t>(rows);
    object["layers"]                = static_cast<std::int64_t>(layers);
    object["spacing"]               = spacing;
    object["originX"]               = originX;
    object["originY"]               = originY;
    object["originZ"]               = originZ;
    object["gravityX"]              = gravityX;
    object["gravityY"]              = gravityY;
    object["gravityZ"]              = gravityZ;
    object["deformationResistance"] = deformationResistance;
    object["iterations"]            = static_cast<std::int64_t>(iterations);
    object["damping"]               = damping;
    object["particleRadius"]        = particleRadius;
    object["particleMass"]          = particleMass;
    object["plasticYield"]          = plasticYield;
    object["plasticCreep"]          = plasticCreep;
    object["plasticRecovery"]       = plasticRecovery;
    object["maxDeformation"]        = maxDeformation;
    object["selfCollision"]         = selfCollision;
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::schema::SchemaDefinition SoftBody3DDefinition::schemaDefinition() {
    using eve::schema::FieldDefinition;
    using eve::schema::ValueType;
    auto fieldDefinition = [](const char* name, ValueType type, const char* title, const char* description,
                              std::string defaultJson) {
        FieldDefinition field;
        field.name        = name;
        field.type        = type;
        field.required    = true;
        field.title       = title;
        field.description = description;
        field.defaultJson = std::move(defaultJson);
        return field;
    };
    auto integerField = [&](const char* name, const char* title, const char* description, int value, double minimum,
                            double maximum) {
        auto field    = fieldDefinition(name, ValueType::Integer, title, description, std::to_string(value));
        field.minimum = minimum;
        field.maximum = maximum;
        return field;
    };
    auto numberField = [&](const char* name, const char* title, const char* description, float value, double minimum,
                           double maximum) {
        auto field    = fieldDefinition(name, ValueType::Number, title, description, std::to_string(value));
        field.minimum = minimum;
        field.maximum = maximum;
        return field;
    };

    SoftBody3DDefinition          defaults;
    eve::schema::SchemaDefinition schema;
    schema.id                   = std::string(SchemaId);
    schema.version              = static_cast<int>(SchemaVersion);
    schema.title                = "Volumetric soft body";
    schema.description          = "Validated lattice, solver, particle and plasticity creation parameters.";
    schema.additionalProperties = false;
    schema.fields               = {
        fieldDefinition("schema", ValueType::String, "Schema", "Stable schema identifier", "\"physics:softbody3d\""),
        integerField("schemaVersion", "Schema version", "Exact supported schema version", 1, 1, 1),
        integerField("cols", "Columns", "Particle count along local X", defaults.cols, 2, 1000000),
        integerField("rows", "Rows", "Particle count along local Y", defaults.rows, 2, 1000000),
        integerField("layers", "Layers", "Particle count along local Z", defaults.layers, 2, 1000000),
        numberField("spacing", "Spacing", "Rest distance in meters", defaults.spacing, 0.0001, 100000),
        numberField("originX", "Origin X", "Minimum rest X coordinate", defaults.originX, -100000, 100000),
        numberField("originY", "Origin Y", "Minimum rest Y coordinate", defaults.originY, -100000, 100000),
        numberField("originZ", "Origin Z", "Minimum rest Z coordinate", defaults.originZ, -100000, 100000),
        numberField("gravityX", "Gravity X", "World X acceleration", defaults.gravityX, -1000, 1000),
        numberField("gravityY", "Gravity Y", "World Y acceleration", defaults.gravityY, -1000, 1000),
        numberField("gravityZ", "Gravity Z", "World Z acceleration", defaults.gravityZ, -1000, 1000),
        numberField("deformationResistance", "Deformation resistance", "Shape-matching strength",
                    defaults.deformationResistance, 0, 1),
        integerField("iterations", "Iterations", "Constraint iterations per substep", defaults.iterations, 1, 32),
        numberField("damping", "Damping", "Velocity damping", defaults.damping, 0, 1),
        numberField("particleRadius", "Particle radius", "Collision radius in meters", defaults.particleRadius, 0.0001,
                    100000),
        numberField("particleMass", "Particle mass", "Particle mass in kilograms", defaults.particleMass, 0.0001,
                    100000),
        numberField("plasticYield", "Plastic yield", "Strain threshold for plastic deformation", defaults.plasticYield,
                    0, 1000),
        numberField("plasticCreep", "Plastic creep", "Permanent deformation rate", defaults.plasticCreep, 0, 1),
        numberField("plasticRecovery", "Plastic recovery", "Rest-shape recovery rate", defaults.plasticRecovery, 0, 1),
        numberField("maxDeformation", "Maximum deformation", "Maximum plastic displacement", defaults.maxDeformation, 0,
                    100000),
        fieldDefinition("selfCollision", ValueType::Boolean, "Self collision", "Resolve non-neighbor overlap",
                        defaults.selfCollision ? "true" : "false")};
    return schema;
}

eve::Result<void> SoftBody3DDefinition::ensureSchemaRegistered() {
    if (eve::schema::SchemaRegistry::resolve(std::string(SchemaId), static_cast<int>(SchemaVersion)))
        return eve::Result<void>::success();
    auto registration = eve::schema::SchemaRegistry::registerVersioned(schemaDefinition());
    if (!registration) return eve::Result<void>::failure(registration.status());
    return eve::Result<void>::success();
}

eve::Result<SoftBody3DDefinition> SoftBody3DDefinition::fromValue(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object) return invalid<SoftBody3DDefinition>("soft-body definition must be an object", "softbody3d");
    const std::initializer_list<std::string_view> fields = {"schema",
                                                            "schemaVersion",
                                                            "cols",
                                                            "rows",
                                                            "layers",
                                                            "spacing",
                                                            "originX",
                                                            "originY",
                                                            "originZ",
                                                            "gravityX",
                                                            "gravityY",
                                                            "gravityZ",
                                                            "deformationResistance",
                                                            "iterations",
                                                            "damping",
                                                            "particleRadius",
                                                            "particleMass",
                                                            "plasticYield",
                                                            "plasticCreep",
                                                            "plasticRecovery",
                                                            "maxDeformation",
                                                            "selfCollision"};
    if (!hasExactFields(*object, fields))
        return invalid<SoftBody3DDefinition>("soft-body definition has missing or unknown fields", "softbody3d");
    const eve::Value* schema = field(*object, "schema");
    if (!schema || !schema->isString() || schema->asString() != SchemaId)
        return invalid<SoftBody3DDefinition>("unexpected soft-body schema id", "schema");
    auto version = integer(*object, "schemaVersion");
    if (!version || version.value() != static_cast<int>(SchemaVersion))
        return invalid<SoftBody3DDefinition>("unsupported soft-body schema version", "schemaVersion");

    SoftBody3DDefinition result;
    auto                 readInt = [&](std::string_view name, int& target) -> eve::Result<void> {
        auto parsed = integer(*object, name);
        if (!parsed) return eve::Result<void>::failure(parsed.status());
        target = parsed.value();
        return eve::Result<void>::success();
    };
    auto readFloat = [&](std::string_view name, float& target) -> eve::Result<void> {
        auto parsed = number(*object, name);
        if (!parsed) return eve::Result<void>::failure(parsed.status());
        target = parsed.value();
        return eve::Result<void>::success();
    };
#define EVE_SOFTBODY_READ(reader, member)                                                \
    do {                                                                                 \
        auto parsed = reader(#member, result.member);                                    \
        if (!parsed) return eve::Result<SoftBody3DDefinition>::failure(parsed.status()); \
    } while (false)
    EVE_SOFTBODY_READ(readInt, cols);
    EVE_SOFTBODY_READ(readInt, rows);
    EVE_SOFTBODY_READ(readInt, layers);
    EVE_SOFTBODY_READ(readFloat, spacing);
    EVE_SOFTBODY_READ(readFloat, originX);
    EVE_SOFTBODY_READ(readFloat, originY);
    EVE_SOFTBODY_READ(readFloat, originZ);
    EVE_SOFTBODY_READ(readFloat, gravityX);
    EVE_SOFTBODY_READ(readFloat, gravityY);
    EVE_SOFTBODY_READ(readFloat, gravityZ);
    EVE_SOFTBODY_READ(readFloat, deformationResistance);
    EVE_SOFTBODY_READ(readInt, iterations);
    EVE_SOFTBODY_READ(readFloat, damping);
    EVE_SOFTBODY_READ(readFloat, particleRadius);
    EVE_SOFTBODY_READ(readFloat, particleMass);
    EVE_SOFTBODY_READ(readFloat, plasticYield);
    EVE_SOFTBODY_READ(readFloat, plasticCreep);
    EVE_SOFTBODY_READ(readFloat, plasticRecovery);
    EVE_SOFTBODY_READ(readFloat, maxDeformation);
#undef EVE_SOFTBODY_READ
    auto collision = boolean(*object, "selfCollision");
    if (!collision) return eve::Result<SoftBody3DDefinition>::failure(collision.status());
    result.selfCollision = collision.value();
    auto valid           = result.validate();
    if (!valid) return eve::Result<SoftBody3DDefinition>::failure(valid.status());
    return eve::Result<SoftBody3DDefinition>::success(std::move(result));
}

}  // namespace eve::physics
