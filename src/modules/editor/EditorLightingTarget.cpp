#include "editor/EditorLightingTarget.h"

#include <cmath>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> lightingError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

PropertyDescriptor property(const char* path, const char* label, const char* category,
                            PropertyType type, EditorValue defaultValue) {
    PropertyDescriptor result;
    result.path = PropertyPath(path);
    result.displayNameKey = label;
    result.category = category;
    result.type = type;
    result.flags = PropertyFlag::Runtime;
    result.defaultValue = std::move(defaultValue);
    return result;
}

void numeric(PropertySchema& schema, const char* path, const char* label, const char* category,
             double value, double minimum, double maximum, double step = 0.01) {
    auto descriptor = property(path, label, category, PropertyType::Float, value);
    descriptor.numeric.minimum = minimum;
    descriptor.numeric.maximum = maximum;
    descriptor.numeric.step = step;
    schema.properties.push_back(std::move(descriptor));
}

PropertySchema lightSchema() {
    PropertySchema schema;
    schema.typeId = "graphics.light3d";
    auto type = property("light.type", "editor.light.type", "light", PropertyType::Enum, "point");
    type.enumItems = {"point", "dir"};
    schema.properties.push_back(std::move(type));
    schema.properties.push_back(property("light.enabled", "editor.light.enabled", "light", PropertyType::Bool, true));
    schema.properties.push_back(property("transform.position", "editor.light.position", "transform",
                                         PropertyType::Vec3, EditorValue::Array{0.0, 0.0, 0.0}));
    schema.properties.push_back(property("transform.direction", "editor.light.direction", "transform",
                                         PropertyType::Vec3, EditorValue::Array{0.4, 1.0, 0.3}));
    schema.properties.push_back(property("light.color", "editor.light.color", "light", PropertyType::Color,
                                         EditorValue::Array{1.0, 1.0, 1.0, 1.0}));
    numeric(schema, "light.intensity", "editor.light.intensity", "light", 1.0, 0.0, 100000.0);
    numeric(schema, "light.radius", "editor.light.radius", "light", 8.0, 0.001, 100000.0);
    schema.properties.push_back(property("shadow.cast", "editor.light.cast-shadow", "shadow",
                                         PropertyType::Bool, false));
    numeric(schema, "shadow.bias", "editor.light.shadow-bias", "shadow", 0.0, 0.0, 1.0, 0.0001);
    numeric(schema, "shadow.strength", "editor.light.shadow-strength", "shadow", 1.0, 0.0, 1.0);
    schema.properties.push_back(property("volumetric.enabled", "editor.light.volumetric", "volumetric",
                                         PropertyType::Bool, false));
    numeric(schema, "volumetric.intensity", "editor.light.volumetric-intensity", "volumetric",
            1.0, 0.0, 100.0);
    return schema;
}

PropertySchema environmentSchema() {
    PropertySchema schema;
    schema.typeId = "graphics.environment";
    auto mode = property("environment.mode", "editor.environment.mode", "environment",
                         PropertyType::Enum, "static");
    mode.enumItems = {"static", "daynight", "weather"};
    schema.properties.push_back(std::move(mode));
    schema.properties.push_back(property("environment.sky-color", "editor.environment.sky-color", "environment",
                                         PropertyType::Color, EditorValue::Array{0.4, 0.6, 1.0, 1.0}));
    schema.properties.push_back(property("environment.ambient-color", "editor.environment.ambient-color",
                                         "environment", PropertyType::Color,
                                         EditorValue::Array{0.12, 0.12, 0.14, 1.0}));
    numeric(schema, "environment.exposure", "editor.environment.exposure", "environment", 1.0, 0.05, 8.0);
    numeric(schema, "daynight.time", "editor.environment.time", "daynight", 12.0, 0.0, 24.0, 0.1);
    numeric(schema, "daynight.speed", "editor.environment.speed", "daynight", 1.0, 0.0, 1000.0, 0.1);
    schema.properties.push_back(property("daynight.paused", "editor.environment.paused", "daynight",
                                         PropertyType::Bool, false));
    numeric(schema, "daynight.turbidity", "editor.environment.turbidity", "daynight", 2.5, 1.5, 10.0);
    numeric(schema, "daynight.mie", "editor.environment.mie", "daynight", 1.0, 0.0, 4.0);
    schema.properties.push_back(property("daynight.skybox", "editor.environment.skybox", "daynight",
                                         PropertyType::Bool, true));
    auto preset = property("weather.preset", "editor.environment.weather-preset", "weather",
                           PropertyType::Enum, "clear");
    preset.enumItems = {"clear", "drizzle", "rain", "storm", "snow", "fog"};
    schema.properties.push_back(std::move(preset));
    numeric(schema, "weather.intensity", "editor.environment.weather-intensity", "weather", 0.0, 0.0, 1.0);
    numeric(schema, "weather.wind-speed", "editor.environment.wind-speed", "weather", 0.0, 0.0, 200.0);
    numeric(schema, "weather.wind-direction", "editor.environment.wind-direction", "weather", 0.0, -360.0, 360.0);
    schema.properties.push_back(property("weather.lightning", "editor.environment.lightning", "weather",
                                         PropertyType::Bool, false));
    schema.properties.push_back(property("weather.fog-color", "editor.environment.fog-color", "weather",
                                         PropertyType::Color, EditorValue::Array{0.5, 0.55, 0.65, 1.0}));
    numeric(schema, "weather.fog-density", "editor.environment.fog-density", "weather", 0.0, 0.0, 10.0);
    schema.properties.push_back(property("weather.environment-enabled", "editor.environment.weather-owned",
                                         "weather", PropertyType::Bool, false));
    return schema;
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

LightingPropertyTargetBase::LightingPropertyTargetBase(std::string id, std::string targetType,
                                                       PropertySchema schema)
    : id_(std::move(id)), targetType_(std::move(targetType)), schema_(std::move(schema)) {
    for (const PropertyDescriptor& descriptor : schema_.properties)
        values_[descriptor.path.value()] = descriptor.defaultValue;
}

TargetDescriptor LightingPropertyTargetBase::describe() const {
    TargetDescriptor result;
    result.id = TargetId(id_);
    result.type = targetType_;
    result.revision = revision_;
    result.capabilities = {CapabilityId("eve.editor.target.lighting-properties")};
    return result;
}

void* LightingPropertyTargetBase::queryCapability(const CapabilityId& capability) {
    return capability == CapabilityId("eve.editor.target.lighting-properties")
               ? static_cast<IPropertyProvider*>(this)
               : nullptr;
}

EditorResult<void> LightingPropertyTargetBase::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_))
        return lightingError<void>(EditorStatus::Rejected, "editor.lighting.target-mismatch",
                                   "Lighting operation targets another document");
    if (operation.type != "lighting.property.set.v1")
        return lightingError<void>(EditorStatus::Unsupported, "editor.lighting.operation-unsupported",
                                   "Lighting operation is unsupported: " + operation.type);
    const EditorValue* pathValue = field(operation.payload, "path");
    const EditorValue* assigned = field(operation.payload, "value");
    const auto* path = pathValue ? pathValue->getIf<std::string>() : nullptr;
    auto descriptor = path ? schema_.find(PropertyPath(*path)) : PropertyDescriptorLookup{};
    if (!descriptor || !assigned)
        return lightingError<void>(EditorStatus::Rejected, "editor.lighting.invalid-operation",
                                   "Lighting operation requires a known path and value");
    auto valid = validatePropertyValue(*descriptor, *assigned);
    if (!valid.accepted()) return valid;
    values_[*path] = *assigned;
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

eve::Result<eve::Revision> LightingPropertyTargetBase::currentRevision(
    const SelectionSnapshot& selection) const {
    if (!selectionMatches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Selection does not belong to this lighting target",
            "editor.lighting.selection", {}, "editor.LightingPropertyTargetBase"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema LightingPropertyTargetBase::schema(const SelectionSnapshot&) const { return schema_; }

PropertyReadResult LightingPropertyTargetBase::read(const SelectionSnapshot& selection,
                                                    const PropertyPath& path) const {
    if (!selectionMatches(selection)) return {};
    const auto found = values_.find(path.value());
    return found == values_.end() ? PropertyReadResult{}
                                  : PropertyReadResult{PropertyReadState::Value, found->second, {}};
}

EditorResult<DomainOperation> LightingPropertyTargetBase::makeSet(const SelectionSnapshot& selection,
                                                                  const PropertyPath& path,
                                                                  const EditorValue& assigned,
                                                                  PropertySetMode mode) const {
    if (!selectionMatches(selection))
        return lightingError<DomainOperation>(EditorStatus::Rejected, "editor.lighting.selection",
                                              "Selection does not belong to this lighting target");
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    if (mode != PropertySetMode::Absolute)
        return lightingError<DomainOperation>(EditorStatus::Unsupported, "editor.lighting.set-mode",
                                              "Lighting properties require absolute assignment");
    auto descriptor = schema_.find(path);
    if (!descriptor)
        return lightingError<DomainOperation>(EditorStatus::Unsupported, "editor.lighting.property-unsupported",
                                              "Lighting property is unknown: " + path.value());
    auto valid = validatePropertyValue(*descriptor, assigned);
    if (!valid.accepted()) {
        EditorResult<DomainOperation> failed;
        failed.status = valid.status;
        failed.diagnostics = std::move(valid.diagnostics);
        return failed;
    }
    const auto previous = values_.find(path.value());
    EditorValue::Object payload{{"path", path.value()}, {"value", assigned}};
    EditorValue::Object inverse{{"path", path.value()}, {"value", previous->second}};
    DomainOperation operation;
    operation.type = "lighting.property.set.v1";
    operation.target = TargetId(id_);
    operation.payload = EditorValue(std::move(payload));
    operation.inverse = EditorValue(std::move(inverse));
    operation.hasInverse = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "lighting:" + id_ + ":" + path.value();
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> LightingPropertyTargetBase::makeReset(const SelectionSnapshot& selection,
                                                                    const PropertyPath& path) const {
    auto descriptor = schema_.find(path);
    if (!descriptor)
        return lightingError<DomainOperation>(EditorStatus::Unsupported, "editor.lighting.property-unsupported",
                                              "Lighting property is unknown: " + path.value());
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

EditorValue LightingPropertyTargetBase::snapshotValue() const {
    EditorValue::Object properties;
    for (const auto& [path, assigned] : values_) properties[path] = assigned;
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"properties", std::move(properties)}};
}

EditorResult<void> LightingPropertyTargetBase::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionValue = field(snapshot, "schemaVersion");
    const EditorValue* propertiesValue = field(snapshot, "properties");
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* properties = propertiesValue ? propertiesValue->getIf<EditorValue::Object>() : nullptr;
    if (!version || *version != 1 || !properties)
        return lightingError<void>(EditorStatus::Rejected, "editor.lighting.invalid-snapshot",
                                   "Lighting snapshot requires schema version one and properties");
    std::map<std::string, EditorValue> candidate;
    for (const PropertyDescriptor& descriptor : schema_.properties)
        candidate[descriptor.path.value()] = descriptor.defaultValue;
    for (const auto& [path, assigned] : *properties) {
        auto descriptor = schema_.find(PropertyPath(path));
        if (!descriptor)
            return lightingError<void>(EditorStatus::Unsupported, "editor.lighting.snapshot-property",
                                       "Lighting snapshot contains an unknown property: " + path);
        auto valid = validatePropertyValue(*descriptor, assigned);
        if (!valid.accepted()) return valid;
        candidate[path] = assigned;
    }
    values_ = std::move(candidate);
    ++revision_;
    dirty_.clear();
    return EditorResult<void>::applied();
}

const EditorValue* LightingPropertyTargetBase::value(const std::string& path) const {
    const auto found = values_.find(path);
    return found == values_.end() ? nullptr : &found->second;
}

bool LightingPropertyTargetBase::selectionMatches(const SelectionSnapshot& selection) const {
    return selection.items.size() == 1 && selection.items.front().target == TargetId(id_);
}

Light3DDocumentTarget::Light3DDocumentTarget(std::string id)
    : LightingPropertyTargetBase(std::move(id), "light3d-document", lightSchema()) {}

std::vector<EditorDiagnostic> Light3DDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    const auto* type = value("light.type")->getIf<std::string>();
    const auto* direction = value("transform.direction")->getIf<EditorValue::Array>();
    const auto* castShadow = value("shadow.cast")->getIf<bool>();
    if (direction && direction->size() == 3) {
        double lengthSquared = 0.0;
        for (const EditorValue& component : *direction) {
            const auto* number = component.getIf<double>();
            if (number) lengthSquared += *number * *number;
        }
        if (lengthSquared <= 1e-12)
            diagnostics.push_back({RuleId("editor.light.zero-direction"), DiagnosticSeverity::Error,
                                   "Light direction cannot be zero"});
    }
    if (type && *type == "point" && castShadow && *castShadow)
        diagnostics.push_back({RuleId("editor.light.point-shadow-unsupported"), DiagnosticSeverity::Warning,
                               "The current renderer only selects directional shadow casters"});
    return diagnostics;
}

EnvironmentDocumentTarget::EnvironmentDocumentTarget(std::string id)
    : LightingPropertyTargetBase(std::move(id), "environment-document", environmentSchema()) {}

std::vector<EditorDiagnostic> EnvironmentDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    const auto* mode = value("environment.mode")->getIf<std::string>();
    const auto* lightning = value("weather.lightning")->getIf<bool>();
    const auto* preset = value("weather.preset")->getIf<std::string>();
    if (mode && *mode == "weather" && lightning && *lightning && preset && *preset != "storm")
        diagnostics.push_back({RuleId("editor.environment.lightning-preset"), DiagnosticSeverity::Warning,
                               "Lightning is enabled outside the storm preset"});
    const auto* weatherOwned = value("weather.environment-enabled")->getIf<bool>();
    if (mode && *mode == "daynight" && weatherOwned && *weatherOwned)
        diagnostics.push_back({RuleId("editor.environment.multiple-owners"), DiagnosticSeverity::Error,
                               "DayNight and Weather cannot both own the environment"});
    return diagnostics;
}

}  // namespace eve::editor
