#include "schema/SchemaRegistry.h"

#include "common/Json.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace eve::schema {
namespace {

using eve::json::Value;

std::unordered_map<std::string, SchemaDefinition>& schemas() {
    static std::unordered_map<std::string, SchemaDefinition> value;
    return value;
}

void addError(std::vector<ValidationError>& out, std::string path, std::string code, std::string message) {
    out.push_back({std::move(path), std::move(code), std::move(message)});
}

bool hasType(Value value, ValueType type) {
    switch (type) {
        case ValueType::Any: return static_cast<bool>(value);
        case ValueType::Null: return value.isNull();
        case ValueType::Boolean: return value.isBool();
        case ValueType::Integer: return value.isNumber() && std::floor(value.asDouble()) == value.asDouble();
        case ValueType::Number: return value.isNumber();
        case ValueType::String: return value.isString();
        case ValueType::Object: return value.isObject();
        case ValueType::Array: return value.isArray();
    }
    return false;
}

bool parseField(Value value, FieldDefinition& out, std::string& error) {
    if (!value.isObject()) {
        error = "each field must be an object";
        return false;
    }
    out.name = value.getString("name");
    if (out.name.empty()) {
        error = "field name must not be empty";
        return false;
    }
    const auto type = valueTypeFromString(value.getString("type", "any"));
    if (!type) {
        error = "unknown type for field '" + out.name + "'";
        return false;
    }
    out.type               = *type;
    const auto elementType = valueTypeFromString(value.getString("elementType", "any"));
    if (!elementType) {
        error = "unknown elementType for field '" + out.name + "'";
        return false;
    }
    out.elementType = *elementType;
    out.required    = value.getBool("required", false);
    out.title       = value.getString("title");
    out.description = value.getString("description");
    out.reference   = value.getString("reference");
    out.defaultJson = value.getString("defaultJson");
    out.enumValues  = value.getStringArray("enum");
    if (value.has("minimum")) out.minimum = value.getDouble("minimum");
    if (value.has("maximum")) out.maximum = value.getDouble("maximum");
    if (value.has("minLength")) out.minLength = value.getInt("minLength");
    if (value.has("maxLength")) out.maxLength = value.getInt("maxLength");
    if (out.minimum && out.maximum && *out.minimum > *out.maximum) {
        error = "minimum exceeds maximum for field '" + out.name + "'";
        return false;
    }
    if (out.minLength && *out.minLength < 0) {
        error = "minLength must be non-negative for field '" + out.name + "'";
        return false;
    }
    if (out.maxLength && (*out.maxLength < 0 || (out.minLength && *out.maxLength < *out.minLength))) {
        error = "invalid maxLength for field '" + out.name + "'";
        return false;
    }
    return true;
}

void validateField(const FieldDefinition& field, Value value, const std::string& path,
                   std::vector<ValidationError>& errors) {
    if (!hasType(value, field.type)) {
        addError(errors, path, "type", "expected " + std::string(valueTypeName(field.type)));
        return;
    }
    if (field.type == ValueType::Number || field.type == ValueType::Integer) {
        const double number = value.asDouble();
        if (field.minimum && number < *field.minimum) addError(errors, path, "minimum", "value is below minimum");
        if (field.maximum && number > *field.maximum) addError(errors, path, "maximum", "value exceeds maximum");
    }
    if (field.type == ValueType::String) {
        const std::string text = value.asString();
        if (field.minLength && static_cast<int>(text.size()) < *field.minLength)
            addError(errors, path, "min_length", "string is shorter than minLength");
        if (field.maxLength && static_cast<int>(text.size()) > *field.maxLength)
            addError(errors, path, "max_length", "string is longer than maxLength");
        if (!field.enumValues.empty() &&
            std::find(field.enumValues.begin(), field.enumValues.end(), text) == field.enumValues.end())
            addError(errors, path, "enum", "value is not in the allowed set");
    }
    if (field.type == ValueType::Array && field.elementType != ValueType::Any) {
        for (size_t i = 0; i < value.size(); ++i) {
            if (!hasType(value.at(i), field.elementType))
                addError(errors, path + "/" + std::to_string(i), "element_type",
                         "expected " + std::string(valueTypeName(field.elementType)));
        }
    }
}

}  // namespace

std::optional<ValueType> valueTypeFromString(const std::string& name) {
    if (name == "any") return ValueType::Any;
    if (name == "null") return ValueType::Null;
    if (name == "boolean" || name == "bool") return ValueType::Boolean;
    if (name == "integer" || name == "int") return ValueType::Integer;
    if (name == "number" || name == "float") return ValueType::Number;
    if (name == "string") return ValueType::String;
    if (name == "object") return ValueType::Object;
    if (name == "array") return ValueType::Array;
    return std::nullopt;
}

const char* valueTypeName(ValueType type) {
    switch (type) {
        case ValueType::Any: return "any";
        case ValueType::Null: return "null";
        case ValueType::Boolean: return "boolean";
        case ValueType::Integer: return "integer";
        case ValueType::Number: return "number";
        case ValueType::String: return "string";
        case ValueType::Object: return "object";
        case ValueType::Array: return "array";
    }
    return "any";
}

bool SchemaRegistry::registerSchema(const SchemaDefinition& definition, std::string* error) {
    if (definition.id.empty()) {
        if (error) *error = "schema id must not be empty";
        return false;
    }
    if (definition.version <= 0) {
        if (error) *error = "schema version must be positive";
        return false;
    }
    std::unordered_set<std::string> names;
    for (const auto& field : definition.fields) {
        if (field.name.empty() || !names.insert(field.name).second) {
            if (error) *error = "field names must be non-empty and unique";
            return false;
        }
        if (field.minimum && field.maximum && *field.minimum > *field.maximum) {
            if (error) *error = "field minimum must not exceed maximum";
            return false;
        }
        if ((field.minLength && *field.minLength < 0) ||
            (field.maxLength && (*field.maxLength < 0 || (field.minLength && *field.maxLength < *field.minLength)))) {
            if (error) *error = "field length constraints are invalid";
            return false;
        }
    }
    schemas()[definition.id] = definition;
    return true;
}

bool SchemaRegistry::registerFromJson(const std::string& json, std::string* error) {
    std::string parseError;
    auto        document = eve::json::Document::parse(json, &parseError);
    const Value root     = document.root();
    if (!document.valid() || !root.isObject()) {
        if (error) *error = parseError.empty() ? "schema must be a JSON object" : parseError;
        return false;
    }
    SchemaDefinition definition;
    definition.id                   = root.getString("id");
    definition.version              = root.getInt("version", 1);
    definition.title                = root.getString("title");
    definition.description          = root.getString("description");
    definition.additionalProperties = root.getBool("additionalProperties", true);
    const Value fields              = root.get("fields");
    if (fields && !fields.isArray()) {
        if (error) *error = "fields must be an array";
        return false;
    }
    for (size_t i = 0; i < fields.size(); ++i) {
        FieldDefinition field;
        std::string     fieldError;
        if (!parseField(fields.at(i), field, fieldError)) {
            if (error) *error = fieldError;
            return false;
        }
        definition.fields.push_back(std::move(field));
    }
    return registerSchema(definition, error);
}

const SchemaDefinition* SchemaRegistry::find(const std::string& id) {
    const auto it = schemas().find(id);
    return it == schemas().end() ? nullptr : &it->second;
}

bool SchemaRegistry::remove(const std::string& id) { return schemas().erase(id) != 0; }
void SchemaRegistry::clear() { schemas().clear(); }
int  SchemaRegistry::count() { return static_cast<int>(schemas().size()); }

std::vector<std::string> SchemaRegistry::ids() {
    std::vector<std::string> result;
    result.reserve(schemas().size());
    for (const auto& item : schemas()) result.push_back(item.first);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<ValidationError> SchemaRegistry::validate(const std::string& schemaId, const std::string& json) {
    std::vector<ValidationError> errors;
    const auto*                  schema = find(schemaId);
    if (!schema) {
        addError(errors, "", "schema_not_found", "schema '" + schemaId + "' is not registered");
        return errors;
    }
    std::string parseError;
    auto        document = eve::json::Document::parse(json, &parseError);
    const Value root     = document.root();
    if (!document.valid()) {
        addError(errors, "", "invalid_json", parseError);
        return errors;
    }
    if (!root.isObject()) {
        addError(errors, "", "root_type", "expected object");
        return errors;
    }
    std::unordered_set<std::string> known;
    for (const auto& field : schema->fields) {
        known.insert(field.name);
        const Value value = root.get(field.name.c_str());
        if (!value) {
            if (field.required) addError(errors, "/" + field.name, "required", "required field is missing");
            continue;
        }
        validateField(field, value, "/" + field.name, errors);
    }
    if (!schema->additionalProperties) {
        for (const auto& key : root.keys()) {
            if (known.find(key) == known.end())
                addError(errors, "/" + key, "additional_property", "field is not declared");
        }
    }
    return errors;
}

}  // namespace eve::schema
