#include "physics_editing/PhysicsTarget.h"

#include <utility>

namespace eve::physics_editing {
namespace {

template <class T>
EditorResult<T> snapshotError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

EditorValue PhysicsColliderTarget::snapshotValue() const {
    EditorValue::Object properties;
    for (const auto& [path, value] : values_) properties[path] = value;
    EditorValue::Object root;
    root["schemaVersion"] = 1;
    root["dimensions"]    = dimensions_;
    root["properties"]    = EditorValue(std::move(properties));
    return EditorValue(std::move(root));
}

EditorResult<void> PhysicsColliderTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionValue    = field(snapshot, "schemaVersion");
    const EditorValue* dimensionsValue = field(snapshot, "dimensions");
    const EditorValue* propertiesValue = field(snapshot, "properties");
    const auto*        version         = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto*        dimensions      = dimensionsValue ? dimensionsValue->getIf<int64_t>() : nullptr;
    const auto*        properties      = propertiesValue ? propertiesValue->getIf<EditorValue::Object>() : nullptr;
    if (!version || *version != 1 || !dimensions || *dimensions != dimensions_ || !properties)
        return snapshotError<void>(EditorStatus::Rejected, "editor.physics.snapshot-format",
                                   "Collider snapshot version or dimensionality is incompatible");
    auto                 candidate   = defaults();
    const PropertySchema schemaValue = colliderSchema();
    for (const auto& [path, value] : *properties) {
        auto descriptor = schemaValue.find(PropertyPath(path));
        if (!descriptor)
            return snapshotError<void>(EditorStatus::Unsupported, "editor.physics.snapshot-property",
                                       "Collider snapshot contains unknown property: " + path);
        auto valid = validateAssignment(*descriptor, value);
        if (!valid.ok()) return valid;
        candidate[path] = value;
    }
    values_ = std::move(candidate);
    ++revision_;
    dirty_.clear();
    return eve::editing::applied<void>();
}

std::vector<EditorDiagnostic> PhysicsColliderTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    const auto                    text = [&](const char* path) -> std::string {
        const auto  found = values_.find(path);
        const auto* value = found == values_.end() ? nullptr : found->second.getIf<std::string>();
        return value ? *value : std::string{};
    };
    const auto number = [&](const char* path) -> double {
        const auto  found = values_.find(path);
        const auto* value = found == values_.end() ? nullptr : found->second.getIf<double>();
        return value ? *value : 0.0;
    };
    const std::string shape = text("shape.kind");
    if ((shape == "convex-hull" || shape == "triangle-mesh" || shape == "height-field" || shape == "polygon" ||
         shape == "chain") &&
        text("shape.asset").empty())
        diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.physics.shape-asset-required"),
            DiagnosticSeverity::Error, "Selected collider kind requires a shape asset"));
    if ((shape == "sphere" || shape == "circle" || shape == "capsule") && number("shape.radius") <= 0.0)
        diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.physics.radius-required"),
            DiagnosticSeverity::Error, "Round collider requires a positive radius"));
    if (shape == "capsule" && number("shape.capsule-height") < 2.0 * number("shape.radius"))
        diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.physics.capsule-height"),
            DiagnosticSeverity::Warning, "Capsule height is smaller than its diameter"));
    if (text("body.type") == "dynamic" && number("material.density") == 0.0)
        diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.physics.dynamic-zero-density"),
            DiagnosticSeverity::Warning, "Dynamic collider has zero density"));
    if ((shape == "triangle-mesh" || shape == "height-field") && text("body.type") != "static")
        diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.physics.static-complex-shape"),
            DiagnosticSeverity::Error, "Triangle-mesh and height-field colliders require a static body"));
    return diagnostics;
}

}  // namespace eve::physics_editing
