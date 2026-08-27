#include "editor/EditorProperty.h"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace eve::editor {

const PropertyDescriptor* PropertySchema::find(const PropertyPath& path) const {
    auto found = std::find_if(properties.begin(), properties.end(),
                              [&](const PropertyDescriptor& property) { return property.path == path; });
    return found == properties.end() ? nullptr : &*found;
}

namespace {

property_access::PropertyKind convertKind(PropertyType type) {
    using Kind = property_access::PropertyKind;
    switch (type) {
        case PropertyType::Bool: return Kind::Bool;
        case PropertyType::Int: return Kind::Integer;
        case PropertyType::Float: return Kind::Number;
        case PropertyType::String: return Kind::String;
        case PropertyType::Enum: return Kind::Enum;
        case PropertyType::Color: return Kind::Color;
        case PropertyType::Vec2: return Kind::Vec2;
        case PropertyType::Vec3: return Kind::Vec3;
        case PropertyType::Vec4: return Kind::Vec4;
        case PropertyType::Transform:
        case PropertyType::Struct: return Kind::Struct;
        case PropertyType::AssetRef: return Kind::AssetRef;
        case PropertyType::ObjectRef: return Kind::ObjectRef;
        case PropertyType::Array: return Kind::Array;
        case PropertyType::Map: return Kind::Map;
        case PropertyType::Action: return Kind::Action;
        case PropertyType::ReadOnlyText: return Kind::ReadOnlyText;
    }
    return Kind::Auto;
}

property_access::PropertyFlag convertFlags(PropertyFlag flags) {
    using Flag = property_access::PropertyFlag;
    Flag result = Flag::None;
    if (hasPropertyFlag(flags, PropertyFlag::ReadOnly)) result = result | Flag::ReadOnly;
    if (hasPropertyFlag(flags, PropertyFlag::Advanced)) result = result | Flag::Advanced;
    if (hasPropertyFlag(flags, PropertyFlag::EditorOnly)) result = result | Flag::EditorOnly;
    if (hasPropertyFlag(flags, PropertyFlag::Runtime)) result = result | Flag::Runtime;
    if (hasPropertyFlag(flags, PropertyFlag::Transient)) result = result | Flag::Transient;
    if (hasPropertyFlag(flags, PropertyFlag::Dangerous)) result = result | Flag::Dangerous;
    if (hasPropertyFlag(flags, PropertyFlag::MultiEdit)) result = result | Flag::MultiEdit;
    return result;
}

const char *legacyRule(const std::string &code) {
    if (code == "property_access.property.type") return "editor.property.type-mismatch";
    if (code == "property_access.property.read-only") return "editor.property.read-only";
    if (code == "property_access.property.choice") return "editor.property.invalid-enum";
    if (code == "property_access.property.finite") return "editor.property.finite";
    if (code == "property_access.property.minimum") return "editor.property.below-minimum";
    if (code == "property_access.property.maximum") return "editor.property.above-maximum";
    return "editor.property.invalid-value";
}

template <class ResultValue>
EditorResult<ResultValue> editorValidationError(const property_access::WriteResult &validation,
                                                const PropertyDescriptor &descriptor) {
    EditorResult<ResultValue> result;
    result.status = EditorStatus::Rejected;
    result.diagnostics.push_back({RuleId(legacyRule(validation.code)), DiagnosticSeverity::Error,
                                  validation.message + ": " + descriptor.path.value()});
    return result;
}

}  // namespace

property_access::PropertyDescriptor toPresentationDescriptor(const PropertyDescriptor &source) {
    property_access::PropertyDescriptor result;
    result.path = source.path.value();
    result.displayName = source.displayNameKey;
    result.description = source.descriptionKey;
    result.category = source.category;
    result.kind = convertKind(source.type);
    result.flags = convertFlags(source.flags);
    result.defaultValue = toPresentationValue(source.defaultValue);
    result.numeric.minimum = source.numeric.minimum;
    result.numeric.maximum = source.numeric.maximum;
    result.numeric.step = source.numeric.step;
    result.numeric.units = source.numeric.units;
    result.numeric.precision = source.numeric.precision;
    result.choices = source.enumItems;
    result.presenterHint = source.presenterHint;
    return result;
}

eve::Value toPresentationValue(const EditorValue &value) {
    return std::visit(
        [](const auto &current) -> eve::Value {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<T, EditorValue::Array>) {
                eve::Value::Array result;
                result.reserve(current.size());
                for (const EditorValue &entry : current) result.push_back(toPresentationValue(entry));
                return result;
            } else if constexpr (std::is_same_v<T, EditorValue::Object>) {
                eve::Value::Object result;
                for (const auto &[key, entry] : current) result.emplace(key, toPresentationValue(entry));
                return result;
            } else {
                return eve::Value(current);
            }
        },
        value.storage());
}

EditorValue toEditorValue(const eve::Value &value) {
    return std::visit(
        [](const auto &current) -> EditorValue {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<T, eve::Value::Array>) {
                EditorValue::Array result;
                result.reserve(current.size());
                for (const eve::Value &entry : current) result.push_back(toEditorValue(entry));
                return result;
            } else if constexpr (std::is_same_v<T, eve::Value::Object>) {
                EditorValue::Object result;
                for (const auto &[key, entry] : current) result.emplace(key, toEditorValue(entry));
                return result;
            } else {
                return EditorValue(current);
            }
        },
        value.storage());
}

EditorResult<void> validatePropertyValue(const PropertyDescriptor &descriptor, const EditorValue &value) {
    const property_access::WriteResult validation =
        property_access::validatePropertyValue(toPresentationDescriptor(descriptor), toPresentationValue(value));
    if (!validation.accepted) return editorValidationError<void>(validation, descriptor);
    // Action payloads are an editor-specific compatibility rule. Presentation
    // buttons may carry a payload, while editor command intents historically
    // use null to represent an action invocation.
    if (descriptor.type == PropertyType::Action && value.type() != EditorValue::Type::Null)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.property.type-mismatch"),
                                         "Value type does not match property: " + descriptor.path.value());
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
