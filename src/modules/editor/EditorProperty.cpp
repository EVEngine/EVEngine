#include "editor/EditorProperty.h"

#include <algorithm>

namespace eve::editor {

const PropertyDescriptor* PropertySchema::find(const PropertyPath& path) const {
    auto found = std::find_if(properties.begin(), properties.end(),
                              [&](const PropertyDescriptor& property) { return property.path == path; });
    return found == properties.end() ? nullptr : &*found;
}

EditorResult<void> validatePropertyValue(const PropertyDescriptor& descriptor, const EditorValue& value) {
    auto rejected = [&](const char* rule, std::string message) {
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId(rule), std::move(message));
    };
    bool typeMatches = false;
    switch (descriptor.type) {
        case PropertyType::Bool: typeMatches = value.type() == EditorValue::Type::Bool; break;
        case PropertyType::Int: typeMatches = value.type() == EditorValue::Type::Integer; break;
        case PropertyType::Float:
            typeMatches = value.type() == EditorValue::Type::Integer || value.type() == EditorValue::Type::Number;
            break;
        case PropertyType::String:
        case PropertyType::Enum:
        case PropertyType::AssetRef:
        case PropertyType::ObjectRef:
        case PropertyType::ReadOnlyText: typeMatches = value.type() == EditorValue::Type::String; break;
        case PropertyType::Color:
        case PropertyType::Vec2:
        case PropertyType::Vec3:
        case PropertyType::Vec4: typeMatches = value.type() == EditorValue::Type::Array; break;
        case PropertyType::Transform:
        case PropertyType::Struct:
        case PropertyType::Map: typeMatches = value.type() == EditorValue::Type::Object; break;
        case PropertyType::Array: typeMatches = value.type() == EditorValue::Type::Array; break;
        case PropertyType::Action: typeMatches = value.type() == EditorValue::Type::Null; break;
    }
    if (!typeMatches)
        return rejected("editor.property.type-mismatch",
                        "Value type does not match property: " + descriptor.path.value());
    if (hasPropertyFlag(descriptor.flags, PropertyFlag::ReadOnly))
        return rejected("editor.property.read-only", "Property is read-only: " + descriptor.path.value());

    std::optional<double> numeric;
    if (const auto* integer = value.getIf<std::int64_t>()) numeric = static_cast<double>(*integer);
    if (const auto* number = value.getIf<double>()) numeric = *number;
    if (numeric && descriptor.numeric.minimum && *numeric < *descriptor.numeric.minimum)
        return rejected("editor.property.below-minimum", "Property value is below its minimum");
    if (numeric && descriptor.numeric.maximum && *numeric > *descriptor.numeric.maximum)
        return rejected("editor.property.above-maximum", "Property value is above its maximum");
    if (descriptor.type == PropertyType::Enum && !descriptor.enumItems.empty()) {
        const std::string& item = *value.getIf<std::string>();
        if (std::find(descriptor.enumItems.begin(), descriptor.enumItems.end(), item) == descriptor.enumItems.end())
            return rejected("editor.property.invalid-enum", "Property value is not a registered enum item");
    }
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
