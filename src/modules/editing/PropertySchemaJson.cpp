#include "editing/PropertySchemaJson.h"

#include "editing/EditingIds.h"

namespace eve::editing {
namespace {

const char* propertyTypeName(PropertyType type) {
    switch (type) {
        case PropertyType::Bool: return "boolean";
        case PropertyType::Int: return "integer";
        case PropertyType::Float: return "number";
        case PropertyType::String: return "string";
        case PropertyType::Enum: return "string";
        case PropertyType::Color: return "object";
        case PropertyType::Vec2: return "array";
        case PropertyType::Vec3: return "array";
        case PropertyType::Vec4: return "array";
        case PropertyType::Transform: return "object";
        case PropertyType::AssetRef: return "string";
        case PropertyType::ObjectRef: return "string";
        case PropertyType::Struct: return "object";
        case PropertyType::Array: return "array";
        case PropertyType::Map: return "object";
        case PropertyType::Action: return "object";
        case PropertyType::ReadOnlyText: return "string";
    }
    return "string";
}

Value propertyDescriptorToJsonSchema(const PropertyDescriptor& descriptor) {
    Value::Object node;
    node["type"]        = propertyTypeName(descriptor.type);
    node["title"]       = descriptor.displayNameKey;
    node["description"] = descriptor.descriptionKey;
    node["category"]    = descriptor.category;
    if (descriptor.type == PropertyType::Enum && !descriptor.enumItems.empty()) {
        Value::Array items;
        items.reserve(descriptor.enumItems.size());
        for (const std::string& item : descriptor.enumItems) items.emplace_back(item);
        node["enum"] = std::move(items);
    }
    if (descriptor.type == PropertyType::Vec2 || descriptor.type == PropertyType::Vec3 ||
        descriptor.type == PropertyType::Vec4) {
        node["items"]    = Value::Object{{"type", "number"}};
        const int count  = descriptor.type == PropertyType::Vec2 ? 2 : (descriptor.type == PropertyType::Vec3 ? 3 : 4);
        node["minItems"] = count;
        node["maxItems"] = count;
    }
    if (descriptor.numeric.minimum) node["minimum"] = *descriptor.numeric.minimum;
    if (descriptor.numeric.maximum) node["maximum"] = *descriptor.numeric.maximum;
    if (hasPropertyFlag(descriptor.flags, PropertyFlag::ReadOnly)) node["readOnly"] = true;
    return node;
}

}  // namespace

PropertySchema emptyObjectPropertySchema(std::string_view typeId) {
    PropertySchema schema;
    schema.typeId  = std::string(typeId);
    schema.version = 1;
    return schema;
}

Result<Value> propertySchemaToJsonSchema(const PropertySchema& schema, std::string_view schemaId) {
    if (schemaId.empty())
        return failed<Value>(Status::Rejected, RuleId("editing.schema.empty-id"),
                             "JSON Schema projection requires a non-empty schema id");

    if (schema.documentSchema.type() != Value::Type::Null) {
        if (!schema.documentSchema.getIf<Value::Object>() || !schema.properties.empty())
            return failed<Value>(Status::Rejected, RuleId("editing.schema.conflicting-shapes"),
                                 "Document schema must be an object and cannot coexist with flat properties");
        return applied<Value>(schema.documentSchema);
    }

    Value::Object root;
    root["$schema"] = "https://json-schema.org/draft/2020-12/schema";
    root["$id"]     = std::string(schemaId);
    root["type"]    = "object";
    root["typeId"]  = schema.typeId.empty() ? std::string(schemaId) : schema.typeId;
    root["version"] = static_cast<std::int64_t>(schema.version);

    Value::Object properties;
    Value::Array  required;
    for (const PropertyDescriptor& descriptor : schema.properties) {
        const std::string key = descriptor.path.value();
        if (key.empty())
            return failed<Value>(Status::Rejected, RuleId("editing.schema.empty-path"),
                                 "PropertySchema entries must use a non-empty path");
        properties.emplace(key, propertyDescriptorToJsonSchema(descriptor));
        if (!hasPropertyFlag(descriptor.flags, PropertyFlag::ReadOnly)) required.emplace_back(key);
    }
    root["properties"]           = std::move(properties);
    root["required"]             = std::move(required);
    root["additionalProperties"] = false;
    return applied<Value>(Value(std::move(root)));
}

}  // namespace eve::editing
