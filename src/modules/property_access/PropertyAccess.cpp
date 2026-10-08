#include "property_access/PropertyAccess.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::property_access {

eve::OptionalRef<const PropertyDescriptor> PropertySchema::find(const std::string &path) const {
    const auto found = std::find_if(properties.begin(), properties.end(),
                                    [&path](const PropertyDescriptor &property) { return property.path == path; });
    return found == properties.end() ? eve::OptionalRef<const PropertyDescriptor>{}
                                     : eve::OptionalRef<const PropertyDescriptor>{std::cref(*found)};
}

WriteResult validateNumericComponent(const PropertyDescriptor &property, double number) {
    if (!std::isfinite(number))
        return WriteResult::reject("property_access.property.finite", "Numeric values must be finite");
    if (property.numeric.minimum && number < *property.numeric.minimum)
        return WriteResult::reject("property_access.property.minimum", "Value is below the minimum");
    if (property.numeric.maximum && number > *property.numeric.maximum)
        return WriteResult::reject("property_access.property.maximum", "Value is above the maximum");
    return WriteResult::success();
}

WriteResult validateArrayArity(PropertyKind kind, const Value::Array &items) {
    std::size_t expected = 0;
    switch (kind) {
        case PropertyKind::Vec2: expected = 2; break;
        case PropertyKind::Vec3: expected = 3; break;
        case PropertyKind::Vec4:
        case PropertyKind::Color: expected = 4; break;
        default: return WriteResult::success();
    }
    if (items.size() != expected)
        return WriteResult::reject("property_access.property.arity",
                                   "Composite property has the wrong component count");
    return WriteResult::success();
}

WriteResult validatePropertyValue(const PropertyDescriptor &property, const Value &value) {
    if (hasFlag(property.flags, PropertyFlag::ReadOnly))
        return WriteResult::reject("property_access.property.read-only", "Property is read-only");

    const Value::Type type       = value.type();
    bool              compatible = false;
    switch (property.kind) {
        case PropertyKind::Auto: compatible = true; break;
        case PropertyKind::Bool: compatible = type == Value::Type::Bool; break;
        case PropertyKind::Integer: compatible = type == Value::Type::Integer; break;
        case PropertyKind::Number: compatible = type == Value::Type::Number || type == Value::Type::Integer; break;
        case PropertyKind::String:
        case PropertyKind::Enum:
        case PropertyKind::AssetRef:
        case PropertyKind::ObjectRef:
        case PropertyKind::ReadOnlyText: compatible = type == Value::Type::String; break;
        case PropertyKind::Color:
        case PropertyKind::Vec2:
        case PropertyKind::Vec3:
        case PropertyKind::Vec4:
        case PropertyKind::Array: compatible = type == Value::Type::Array; break;
        case PropertyKind::Struct:
        case PropertyKind::Map: compatible = type == Value::Type::Object; break;
        case PropertyKind::Action: compatible = true; break;
    }
    if (!compatible)
        return WriteResult::reject("property_access.property.type", "Property value type does not match schema");

    if (property.kind == PropertyKind::Enum) {
        const auto *selected = value.getIf<std::string>();
        if (!selected ||
            std::find(property.choices.begin(), property.choices.end(), *selected) == property.choices.end())
            return WriteResult::reject("property_access.property.choice", "Value is not an allowed choice");
    }

    if (const auto *items = value.getIf<Value::Array>()) {
        if (const WriteResult arity = validateArrayArity(property.kind, *items); !arity.accepted)
            return arity;
        if (property.kind == PropertyKind::Color || property.kind == PropertyKind::Vec2 ||
            property.kind == PropertyKind::Vec3 || property.kind == PropertyKind::Vec4) {
            for (const Value &component : *items) {
                double number = 0.0;
                if (const auto *integer = component.getIf<std::int64_t>()) {
                    number = static_cast<double>(*integer);
                } else if (const auto *floating = component.getIf<double>()) {
                    number = *floating;
                } else {
                    return WriteResult::reject("property_access.property.type",
                                               "Composite components must be numeric");
                }
                if (const WriteResult componentResult = validateNumericComponent(property, number);
                    !componentResult.accepted)
                    return componentResult;
            }
            return WriteResult::success();
        }
    }

    double number    = 0.0;
    bool   hasNumber = false;
    if (const auto *integer = value.getIf<std::int64_t>()) {
        number    = static_cast<double>(*integer);
        hasNumber = true;
    } else if (const auto *floating = value.getIf<double>()) {
        number    = *floating;
        hasNumber = true;
    }
    if (hasNumber) return validateNumericComponent(property, number);
    return WriteResult::success();
}

}  // namespace eve::property_access
