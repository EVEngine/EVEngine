#pragma once
// Private display projection: typed locations into a fresh owning source copy, never a second config schema.
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "property_access/PropertyAccess.h"
namespace eve::spawn_editor::detail {
using Segment   = std::variant<std::string, std::size_t>;
using FieldPath = std::vector<Segment>;
struct ArrayField {
    FieldPath   path;
    std::string label;
};
struct FieldProjection {
    std::vector<ArrayField>          arrays;
    Value                            source;
    property_access::PropertySchema  schema;
    std::map<std::string, FieldPath> paths;
};
inline std::string fieldKey(const FieldPath& path) {
    constexpr char hex[]  = "0123456789abcdef";
    std::string    result = "$field";
    for (const auto& segment : path) {
        result += '_';
        if (const auto* key = std::get_if<std::string>(&segment)) {
            result += 'k';
            for (const unsigned char ch : *key) {
                result += hex[ch >> 4];
                result += hex[ch & 15];
            }
        } else
            result += 'i' + std::to_string(std::get<std::size_t>(segment));
    }
    return result;
}
template <class Tree>
Tree* locateField(Tree& root, const FieldPath& path) {
    auto* value = &root;
    for (const auto& segment : path) {
        if (const auto* key = std::get_if<std::string>(&segment))
            value = value->find(*key);
        else {
            auto*      array = value->template getIf<Value::Array>();
            const auto index = std::get<std::size_t>(segment);
            value            = array && index < array->size() ? &(*array)[index] : nullptr;
        }
        if (!value) return nullptr;
    }
    return value;
}
inline void appendFields(FieldProjection& projection, const Value& value, FieldPath& path, const std::string& label,
                         const std::string& category, property_access::PropertyFlag flags) {
    if (const auto* object = value.getIf<Value::Object>(); object && !object->empty()) {
        for (const auto& [key, child] : *object) {
            path.emplace_back(key);
            appendFields(projection, child, path, label + "." + key, category, flags);
            path.pop_back();
        }
        return;
    }
    if (const auto* array = value.getIf<Value::Array>()) {
        projection.arrays.push_back({path, label});
        for (std::size_t i = 0; i < array->size(); ++i) {
            path.emplace_back(i);
            appendFields(projection, (*array)[i], path, label + "[" + std::to_string(i) + "]", category, flags);
            path.pop_back();
        }
        return;
    }
    property_access::PropertyDescriptor field;
    field.path        = path.size() == 1 ? std::get<std::string>(path.front()) : fieldKey(path);
    field.displayName = label;
    field.category    = category;
    field.flags       = flags;
    switch (value.type()) {
        case Value::Type::Bool: field.kind = property_access::PropertyKind::Bool; break;
        case Value::Type::Int64:
        case Value::Type::Double:
            field.kind = path.size() > 1 ? property_access::PropertyKind::String
                                         : (value.isInt64() ? property_access::PropertyKind::Integer
                                                            : property_access::PropertyKind::Number);
            break;
        case Value::Type::String: field.kind = property_access::PropertyKind::String; break;
        default:
            field.kind  = property_access::PropertyKind::ReadOnlyText;
            field.flags = field.flags | property_access::PropertyFlag::ReadOnly;
            break;
    }
    projection.paths.emplace(field.path, path);
    projection.schema.properties.push_back(std::move(field));
}
inline FieldProjection projectFields(const property_access::PropertySchema& rootSchema, Value source) {
    FieldProjection result;
    result.source         = std::move(source);
    result.schema.typeId  = rootSchema.typeId;
    result.schema.version = rootSchema.version;
    if (!result.source.isObject()) return result;
    for (const auto& [key, value] : *result.source.getIf<Value::Object>()) {
        const auto metadata = rootSchema.find(key);
        const auto flags    = metadata ? metadata->get().flags : property_access::PropertyFlag::EditorOnly;
        const auto label    = metadata ? metadata->get().displayName : key;
        FieldPath  path{key};
        appendFields(result, value, path, label, key, flags);
    }
    return result;
}
}  // namespace eve::spawn_editor::detail
