#include "graphics/WaterStyleConfig.h"

#include "common/Diagnostic.h"

#include <cmath>
#include <initializer_list>
#include <set>

namespace eve::graphics {
namespace {

template <class T>
Result<T> invalid(std::string message, std::string path, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "graphics.water"));
}

Result<void> invalid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {}, "graphics.water"));
}

const Value::Object* objectAt(const Value& value, const char* name) {
    const Value* field = value.find(name);
    return field ? field->getIf<Value::Object>() : nullptr;
}

bool exactFields(const Value::Object& object, std::initializer_list<const char*> fields) {
    if (object.size() != fields.size()) return false;
    for (const char* field : fields)
        if (!object.contains(field)) return false;
    return true;
}

bool numberAt(const Value::Object& object, const char* name, float& output) {
    const auto found = object.find(name);
    if (found == object.end() || (!found->second.isDouble() && !found->second.isInt64())) return false;
    output = static_cast<float>(found->second.isDouble() ? found->second.asDouble() : found->second.asInt());
    return std::isfinite(output);
}

bool integerAt(const Value::Object& object, const char* name, int& output) {
    const auto found = object.find(name);
    if (found == object.end() || !found->second.isInt64()) return false;
    output = static_cast<int>(found->second.asInt());
    return true;
}

bool boolAt(const Value::Object& object, const char* name, bool& output) {
    const auto found = object.find(name);
    if (found == object.end() || !found->second.isBool()) return false;
    output = found->second.asBool();
    return true;
}

bool vec3At(const Value::Object& object, const char* name, glm::vec3& output) {
    const auto found = object.find(name);
    if (found == object.end()) return false;
    const auto* values = found->second.getIf<Value::Array>();
    if (!values || values->size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        if (!(*values)[i].isDouble() && !(*values)[i].isInt64()) return false;
        output[i] = static_cast<float>((*values)[i].isDouble() ? (*values)[i].asDouble() : (*values)[i].asInt());
        if (!std::isfinite(output[i])) return false;
    }
    return true;
}

Value vec3Value(const glm::vec3& value) {
    return Value::array({value.x, value.y, value.z});
}

bool color(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           glm::all(glm::greaterThanEqual(value, glm::vec3(0.0F))) &&
           glm::all(glm::lessThanEqual(value, glm::vec3(8.0F)));
}

}  // namespace

Result<void> WaterStyleConfig::validate() const {
    if (!color(deepColor) || !color(shallowColor) || !color(foamColor) || !color(reflectionTint))
        return invalid("water colors must be finite and in [0, 8]", "$.colors");
    if (!std::isfinite(waveSpeed) || waveSpeed < -20.0F || waveSpeed > 20.0F ||
        !std::isfinite(waveAmplitude) || waveAmplitude < 0.0F || waveAmplitude > 4.0F ||
        !std::isfinite(waveScale) || waveScale < 0.01F || waveScale > 256.0F ||
        !std::isfinite(waveSharpness) || waveSharpness < 0.1F || waveSharpness > 8.0F ||
        !std::isfinite(rippleAmplitude) || rippleAmplitude < 0.0F || rippleAmplitude > 4.0F ||
        rippleCount < 0 || rippleCount > 8 || !std::isfinite(rippleInterval) || rippleInterval < 0.05F ||
        rippleInterval > 60.0F)
        return invalid("invalid wave or ripple parameter", "$.waves");
    if (!std::isfinite(depthDistance) || depthDistance <= 0.0F || depthDistance > 10000.0F ||
        !std::isfinite(opacity) || opacity < 0.0F || opacity > 1.0F)
        return invalid("invalid depth parameter", "$.depth");
    if (!std::isfinite(foamWidth) || foamWidth <= 0.0F || foamWidth > 1000.0F ||
        !std::isfinite(foamSoftness) || foamSoftness < 0.0F || foamSoftness > 1.0F ||
        !std::isfinite(foamStrength) || foamStrength < 0.0F || foamStrength > 4.0F)
        return invalid("invalid foam parameter", "$.foam");
    if (!std::isfinite(fresnelPower) || fresnelPower < 0.01F || fresnelPower > 32.0F ||
        !std::isfinite(reflectionIntensity) || reflectionIntensity < 0.0F || reflectionIntensity > 4.0F ||
        !std::isfinite(sunIntensity) || sunIntensity < 0.0F || sunIntensity > 8.0F ||
        !std::isfinite(screenSpaceReflectionStrength) || screenSpaceReflectionStrength < 0.0F ||
        screenSpaceReflectionStrength > 4.0F)
        return invalid("invalid lighting parameter", "$.lighting");
    if (!std::isfinite(refractionStrength) || refractionStrength < 0.0F || refractionStrength > 0.25F)
        return invalid("invalid refraction strength", "$.refraction.strength");
    if (!std::isfinite(causticsStrength) || causticsStrength < 0.0F || causticsStrength > 4.0F ||
        !std::isfinite(causticsScale) || causticsScale < 0.01F || causticsScale > 256.0F)
        return invalid("invalid caustics parameter", "$.caustics");
    return Result<void>::success();
}

Result<Value> WaterStyleConfig::toValue() const {
    auto valid = validate();
    if (!valid) return Result<Value>::failure(valid.status());
    Value::Object colors{{"deep", vec3Value(deepColor)}, {"shallow", vec3Value(shallowColor)},
                         {"foam", vec3Value(foamColor)}, {"reflectionTint", vec3Value(reflectionTint)}};
    Value::Object waves{{"speed", waveSpeed}, {"amplitude", waveAmplitude}, {"scale", waveScale},
                        {"sharpness", waveSharpness}, {"rippleAmplitude", rippleAmplitude},
                        {"rippleCount", static_cast<std::int64_t>(rippleCount)}, {"rippleInterval", rippleInterval}};
    Value::Object depth{{"distance", depthDistance}, {"opacity", opacity}};
    Value::Object foam{{"width", foamWidth}, {"softness", foamSoftness}, {"strength", foamStrength}};
    Value::Object lighting{{"fresnelPower", fresnelPower}, {"reflectionIntensity", reflectionIntensity},
                           {"sunIntensity", sunIntensity}, {"screenSpaceReflection", screenSpaceReflection},
                           {"screenSpaceReflectionStrength", screenSpaceReflectionStrength}};
    Value::Object refraction{{"strength", refractionStrength}};
    Value::Object caustics{{"strength", causticsStrength}, {"scale", causticsScale}};
    return Result<Value>::success(Value::object({
        {"schema", std::string(SchemaId)}, {"schemaVersion", static_cast<std::int64_t>(SchemaVersion)},
        {"unknownFields", "reject"}, {"colors", Value(std::move(colors))}, {"waves", Value(std::move(waves))},
        {"depth", Value(std::move(depth))}, {"foam", Value(std::move(foam))},
        {"lighting", Value(std::move(lighting))}, {"refraction", Value(std::move(refraction))},
        {"caustics", Value(std::move(caustics))},
    }));
}

Result<WaterStyleConfig> WaterStyleConfig::fromValue(const Value& value) {
    const auto* root = value.getIf<Value::Object>();
    if (!root || !exactFields(*root, {"schema", "schemaVersion", "unknownFields", "colors", "waves", "depth",
                                      "foam", "lighting", "refraction", "caustics"}))
        return invalid<WaterStyleConfig>("water definition requires exactly ten known fields", "$",
                                         DiagnosticCode::InvalidArgument);
    if (!root->at("schema").isString() || root->at("schema").asString() != SchemaId ||
        !root->at("schemaVersion").isInt64() || root->at("schemaVersion").asInt() != SchemaVersion)
        return invalid<WaterStyleConfig>("unsupported water schema/version", "$.schemaVersion", DiagnosticCode::UnknownVersion);
    if (!root->at("unknownFields").isString() || root->at("unknownFields").asString() != "reject")
        return invalid<WaterStyleConfig>("water v1 requires unknownFields=reject", "$.unknownFields");

    const auto* colors = objectAt(value, "colors");
    const auto* waves = objectAt(value, "waves");
    const auto* depth = objectAt(value, "depth");
    const auto* foam = objectAt(value, "foam");
    const auto* lighting = objectAt(value, "lighting");
    const auto* refraction = objectAt(value, "refraction");
    const auto* caustics = objectAt(value, "caustics");
    if (!colors || !waves || !depth || !foam || !lighting || !refraction || !caustics ||
        !exactFields(*colors, {"deep", "shallow", "foam", "reflectionTint"}) ||
        !exactFields(*waves, {"speed", "amplitude", "scale", "sharpness", "rippleAmplitude", "rippleCount", "rippleInterval"}) ||
        !exactFields(*depth, {"distance", "opacity"}) ||
        !exactFields(*foam, {"width", "softness", "strength"}) ||
        !exactFields(*lighting, {"fresnelPower", "reflectionIntensity", "sunIntensity", "screenSpaceReflection", "screenSpaceReflectionStrength"}) ||
        !exactFields(*refraction, {"strength"}) || !exactFields(*caustics, {"strength", "scale"}))
        return invalid<WaterStyleConfig>("invalid or unknown nested water field", "$", DiagnosticCode::InvalidArgument);

    WaterStyleConfig output;
    if (!vec3At(*colors, "deep", output.deepColor) || !vec3At(*colors, "shallow", output.shallowColor) ||
        !vec3At(*colors, "foam", output.foamColor) || !vec3At(*colors, "reflectionTint", output.reflectionTint) ||
        !numberAt(*waves, "speed", output.waveSpeed) || !numberAt(*waves, "amplitude", output.waveAmplitude) ||
        !numberAt(*waves, "scale", output.waveScale) || !numberAt(*waves, "sharpness", output.waveSharpness) ||
        !numberAt(*waves, "rippleAmplitude", output.rippleAmplitude) || !integerAt(*waves, "rippleCount", output.rippleCount) ||
        !numberAt(*waves, "rippleInterval", output.rippleInterval) || !numberAt(*depth, "distance", output.depthDistance) ||
        !numberAt(*depth, "opacity", output.opacity) || !numberAt(*foam, "width", output.foamWidth) ||
        !numberAt(*foam, "softness", output.foamSoftness) || !numberAt(*foam, "strength", output.foamStrength) ||
        !numberAt(*lighting, "fresnelPower", output.fresnelPower) ||
        !numberAt(*lighting, "reflectionIntensity", output.reflectionIntensity) ||
        !numberAt(*lighting, "sunIntensity", output.sunIntensity) ||
        !boolAt(*lighting, "screenSpaceReflection", output.screenSpaceReflection) ||
        !numberAt(*lighting, "screenSpaceReflectionStrength", output.screenSpaceReflectionStrength) ||
        !numberAt(*refraction, "strength", output.refractionStrength) ||
        !numberAt(*caustics, "strength", output.causticsStrength) ||
        !numberAt(*caustics, "scale", output.causticsScale))
        return invalid<WaterStyleConfig>("missing or invalid water value", "$", DiagnosticCode::InvalidArgument);
    auto valid = output.validate();
    if (!valid) return Result<WaterStyleConfig>::failure(valid.status());
    return Result<WaterStyleConfig>::success(std::move(output));
}

Result<WaterStyleConfig> WaterStyleConfig::fromJson(std::string_view json) {
    auto value = Value::fromJson(json);
    if (!value) return Result<WaterStyleConfig>::failure(value.status());
    return fromValue(value.value());
}

Result<std::string> WaterStyleConfig::toJson() const {
    auto value = toValue();
    if (!value) return Result<std::string>::failure(value.status());
    return value.value().toJson();
}

}  // namespace eve::graphics
