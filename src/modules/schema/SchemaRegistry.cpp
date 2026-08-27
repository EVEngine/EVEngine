#include "schema/SchemaRegistry.h"

#include "common/Json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::schema {
namespace {

using JsonValue = eve::json::Value;
using SchemaVersions = std::map<int, SchemaDefinition>;

std::unordered_map<std::string, SchemaVersions>& schemas() {
    static std::unordered_map<std::string, SchemaVersions> value;
    return value;
}

using MigrationKey = std::pair<std::string, int>;

struct MigrationStep {
    int               toVersion = 0;
    MigrationFunction function;
};

std::map<MigrationKey, MigrationStep>& migrationSteps() {
    static std::map<MigrationKey, MigrationStep> value;
    return value;
}

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

void addError(std::vector<ValidationError>& out, std::string path, std::string code, std::string message) {
    out.push_back({std::move(path), std::move(code), std::move(message)});
}

bool hasType(JsonValue value, ValueType type) {
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

std::string pointerSegment(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if (character == '~') result += "~0";
        else if (character == '/') result += "~1";
        else result += character;
    }
    return result;
}

bool readStrictString(JsonValue value, std::string& output) {
    if (!value.isString()) return false;
    output = value.asString();
    return true;
}

bool readStrictBool(JsonValue value, bool& output) {
    if (!value.isBool()) return false;
    output = value.asBool();
    return true;
}

bool readInteger(JsonValue value, int& output) {
    if (!value.isNumber()) return false;
    const double number = value.asDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max()))
        return false;
    output = static_cast<int>(number);
    return true;
}

bool parseNode(JsonValue value, SchemaNode& output, std::string& error, const std::string& path,
               bool allowFieldMetadata = false);

bool parseField(JsonValue value, FieldDefinition& output, std::string& error, const std::string& path) {
    if (!value.isObject()) {
        error = path + " must be an object";
        return false;
    }

    output = FieldDefinition{};
    const JsonValue name = value.get("name");
    if (!readStrictString(name, output.name) || output.name.empty()) {
        error = path + "/name must be a non-empty string";
        return false;
    }
    if (!parseNode(value, output, error, path, true)) return false;

    if (value.has("required")) {
        if (!readStrictBool(value.get("required"), output.required)) {
            error = path + "/required must be a boolean";
            return false;
        }
    }
    if (value.has("title") && !readStrictString(value.get("title"), output.title)) {
        error = path + "/title must be a string";
        return false;
    }
    if (value.has("description") && !readStrictString(value.get("description"), output.description)) {
        error = path + "/description must be a string";
        return false;
    }
    if (value.has("reference") && !readStrictString(value.get("reference"), output.reference)) {
        error = path + "/reference must be a string";
        return false;
    }
    if (value.has("defaultJson")) {
        if (!readStrictString(value.get("defaultJson"), output.defaultJson)) {
            error = path + "/defaultJson must be a string";
            return false;
        }
        std::string defaultError;
        const auto  defaultDocument = eve::json::Document::parse(output.defaultJson, &defaultError);
        if (!defaultDocument.valid()) {
            error = path + "/defaultJson is not valid JSON: " + defaultError;
            return false;
        }
    }
    return true;
}

bool parseFieldArray(JsonValue fields, std::vector<FieldDefinition>& output, std::string& error,
                     const std::string& path) {
    if (!fields.isArray()) {
        error = path + " must be an array";
        return false;
    }
    output.clear();
    output.reserve(fields.size());
    for (size_t index = 0; index < fields.size(); ++index) {
        FieldDefinition field;
        if (!parseField(fields.at(index), field, error, path + "/" + std::to_string(index))) return false;
        output.push_back(std::move(field));
    }
    return true;
}

bool parseNode(JsonValue value, SchemaNode& output, std::string& error, const std::string& path,
               bool allowFieldMetadata) {
    if (!value.isObject()) {
        error = path + " must be an object";
        return false;
    }

    static const std::set<std::string> nodeKeys = {
        "additionalProperties", "discriminator", "discriminatorMapping", "elementType", "enum",
        "fields", "itemSchema", "items", "maxItems", "maxLength", "maximum", "minItems",
        "minLength", "minimum", "oneOf", "ref", "refVersion", "referenceVersion", "schemaRef",
        "type", "union"};
    static const std::set<std::string> fieldMetadataKeys = {
        "defaultJson", "description", "name", "reference", "required", "title"};
    for (const auto& key : value.keys()) {
        if (!nodeKeys.contains(key) && (!allowFieldMetadata || !fieldMetadataKeys.contains(key))) {
            error = path + "/" + pointerSegment(key) + " is not supported by Eve Schema v1";
            return false;
        }
    }

    output = SchemaNode{};
    if (value.has("type")) {
        if (!value.get("type").isString()) {
            error = path + "/type must be a string";
            return false;
        }
        const auto parsedType = valueTypeFromString(value.getString("type"));
        if (!parsedType) {
            error = "unknown type at " + path;
            return false;
        }
        output.type = *parsedType;
    }

    if (value.has("elementType")) {
        if (!value.get("elementType").isString()) {
            error = path + "/elementType must be a string";
            return false;
        }
        const auto parsedType = valueTypeFromString(value.getString("elementType"));
        if (!parsedType) {
            error = "unknown elementType at " + path;
            return false;
        }
        output.elementType = *parsedType;
    }

    const bool hasRef       = value.has("ref");
    const bool hasSchemaRef = value.has("schemaRef");
    if (hasRef && hasSchemaRef) {
        std::string directRef;
        std::string aliasRef;
        if (!readStrictString(value.get("ref"), directRef) || !readStrictString(value.get("schemaRef"), aliasRef) ||
            directRef != aliasRef) {
            error = path + "/ref conflicts with schemaRef";
            return false;
        }
    }
    if (hasRef || hasSchemaRef) {
        const JsonValue ref = value.get(hasRef ? "ref" : "schemaRef");
        if (!readStrictString(ref, output.ref) || output.ref.empty()) {
            error = path + "/ref must be a non-empty string";
            return false;
        }
        if (output.ref.find('#') != std::string::npos) {
            error = path + "/ref only supports whole-schema references; fragments are not in Eve Schema";
            return false;
        }
    }
    const bool hasRefVersion       = value.has("refVersion");
    const bool hasReferenceVersion = value.has("referenceVersion");
    if (hasRefVersion && hasReferenceVersion) {
        int directVersion = 0;
        int aliasVersion  = 0;
        if (!readInteger(value.get("refVersion"), directVersion) ||
            !readInteger(value.get("referenceVersion"), aliasVersion) || directVersion != aliasVersion) {
            error = path + "/refVersion conflicts with referenceVersion";
            return false;
        }
    }
    if (hasRefVersion || hasReferenceVersion) {
        const JsonValue refVersion = value.get(hasRefVersion ? "refVersion" : "referenceVersion");
        if (!readInteger(refVersion, output.refVersion) || output.refVersion <= 0) {
            error = path + "/refVersion must be a positive integer";
            return false;
        }
    }

    auto readNumberConstraint = [&](const char* name, std::optional<double>& target) {
        if (!value.has(name)) return true;
        const JsonValue constraint = value.get(name);
        if (!constraint.isNumber() || !std::isfinite(constraint.asDouble())) {
            error = path + "/" + name + " must be a finite number";
            return false;
        }
        target = constraint.asDouble();
        return true;
    };
    if (!readNumberConstraint("minimum", output.minimum) || !readNumberConstraint("maximum", output.maximum))
        return false;
    if (output.minimum && output.maximum && *output.minimum > *output.maximum) {
        error = path + "/minimum must not exceed maximum";
        return false;
    }

    auto readLengthConstraint = [&](const char* name, std::optional<int>& target) {
        if (!value.has(name)) return true;
        int parsed = 0;
        if (!readInteger(value.get(name), parsed) || parsed < 0) {
            error = path + "/" + name + " must be a non-negative integer";
            return false;
        }
        target = parsed;
        return true;
    };
    if (!readLengthConstraint("minLength", output.minLength) ||
        !readLengthConstraint("maxLength", output.maxLength) ||
        !readLengthConstraint("minItems", output.minItems) ||
        !readLengthConstraint("maxItems", output.maxItems))
        return false;
    if (output.minLength && output.maxLength && *output.minLength > *output.maxLength) {
        error = path + "/minLength must not exceed maxLength";
        return false;
    }
    if (output.minItems && output.maxItems && *output.minItems > *output.maxItems) {
        error = path + "/minItems must not exceed maxItems";
        return false;
    }

    if (value.has("enum")) {
        const JsonValue enumValues = value.get("enum");
        if (!enumValues.isArray()) {
            error = path + "/enum must be an array";
            return false;
        }
        for (size_t index = 0; index < enumValues.size(); ++index) {
            std::string entry;
            if (!readStrictString(enumValues.at(index), entry)) {
                error = path + "/enum supports string values only";
                return false;
            }
            output.enumValues.push_back(std::move(entry));
        }
    }

    const bool hasItems = value.has("items");
    const bool hasItemSchema = value.has("itemSchema");
    if (hasItems && hasItemSchema) {
        error = path + " cannot define both items and itemSchema";
        return false;
    }
    if (hasItems || hasItemSchema) {
        if (output.type != ValueType::Array) {
            error = path + "/items is only valid for an array node";
            return false;
        }
        auto item = std::make_shared<SchemaNode>();
        if (!parseNode(value.get(hasItems ? "items" : "itemSchema"), *item, error, path + "/items")) return false;
        output.itemSchema = std::move(item);
    }

    const bool hasUnion = value.has("union");
    const bool hasOneOf = value.has("oneOf");
    if (hasUnion && hasOneOf) {
        error = path + " cannot define both union and oneOf";
        return false;
    }
    if (hasUnion || hasOneOf) {
        const JsonValue alternatives = value.get(hasUnion ? "union" : "oneOf");
        if (!alternatives.isArray() || alternatives.size() == 0) {
            error = path + "/union must be a non-empty array";
            return false;
        }
        output.variants.reserve(alternatives.size());
        for (size_t index = 0; index < alternatives.size(); ++index) {
            SchemaNode alternative;
            if (!parseNode(alternatives.at(index), alternative, error,
                           path + "/union/" + std::to_string(index)))
                return false;
            output.variants.push_back(std::move(alternative));
        }
    }

    if (value.has("discriminator")) {
        if (!readStrictString(value.get("discriminator"), output.discriminator) ||
            output.discriminator.empty()) {
            error = path + "/discriminator must be a non-empty string";
            return false;
        }
    }
    if (value.has("discriminatorMapping")) {
        const JsonValue mapping = value.get("discriminatorMapping");
        if (!mapping.isObject()) {
            error = path + "/discriminatorMapping must be an object";
            return false;
        }
        for (const std::string& key : mapping.keys()) {
            const JsonValue mapped = mapping.get(key.c_str());
            std::string      target;
            if (!readStrictString(mapped, target)) {
                int mappingIndex = 0;
                if (!readInteger(mapped, mappingIndex) || mappingIndex < 0) {
                    error = path + "/discriminatorMapping values must be refs, indexes, or strings";
                    return false;
                }
                target = std::to_string(mappingIndex);
            }
            output.discriminatorMapping.emplace(key, std::move(target));
        }
    }
    if (!output.variants.empty() && output.type != ValueType::Any && output.type != ValueType::Object) {
        error = path + "/union type must be any or object in Eve Schema";
        return false;
    }

    if (value.has("fields") || value.has("additionalProperties")) {
        if (output.type != ValueType::Object) {
            error = path + "/fields or additionalProperties requires type object";
            return false;
        }
        auto nested = std::make_shared<SchemaDefinition>();
        nested->version = 1;
        if (value.has("additionalProperties")) {
            if (!readStrictBool(value.get("additionalProperties"), nested->additionalProperties)) {
                error = path + "/additionalProperties must be a boolean";
                return false;
            }
        }
        if (value.has("fields") &&
            !parseFieldArray(value.get("fields"), nested->fields, error, path + "/fields"))
            return false;
        output.objectSchema = std::move(nested);
    }
    return true;
}

bool parseDefinition(const std::string& json, SchemaDefinition& definition, std::string* error) {
    std::string parseError;
    const auto  document = eve::json::Document::parse(json, &parseError);
    const JsonValue root = document.root();
    if (!document.valid() || !root.isObject()) {
        if (error) *error = parseError.empty() ? "schema must be a JSON object" : parseError;
        return false;
    }

    static const std::set<std::string> definitionKeys = {
        "additionalProperties", "description", "fields", "id", "schemaVersion", "title", "version"};
    for (const auto& key : root.keys()) {
        if (!definitionKeys.contains(key)) {
            if (error) *error = "/" + pointerSegment(key) + " is not supported by Eve Schema v1";
            return false;
        }
    }

    definition = SchemaDefinition{};
    const bool hasSchemaVersion = root.has("schemaVersion");
    const bool hasLegacyVersion = root.has("version");
    int        schemaVersion = 1;
    if (hasSchemaVersion && !readInteger(root.get("schemaVersion"), schemaVersion)) {
        if (error) *error = "schemaVersion must be an integer";
        return false;
    }
    if (!hasSchemaVersion && hasLegacyVersion && !readInteger(root.get("version"), schemaVersion)) {
        if (error) *error = "version must be an integer";
        return false;
    }
    if (hasSchemaVersion && hasLegacyVersion) {
        int legacyVersion = 0;
        if (!readInteger(root.get("version"), legacyVersion) || legacyVersion != schemaVersion) {
            if (error) *error = "schemaVersion conflicts with legacy version";
            return false;
        }
    }

    if (root.has("id") && !readStrictString(root.get("id"), definition.id)) {
        if (error) *error = "id must be a string";
        return false;
    }
    definition.version = schemaVersion;
    if (root.has("title") && !readStrictString(root.get("title"), definition.title)) {
        if (error) *error = "title must be a string";
        return false;
    }
    if (root.has("description") && !readStrictString(root.get("description"), definition.description)) {
        if (error) *error = "description must be a string";
        return false;
    }
    if (root.has("additionalProperties") &&
        !readStrictBool(root.get("additionalProperties"), definition.additionalProperties)) {
        if (error) *error = "additionalProperties must be a boolean";
        return false;
    }
    if (root.has("fields")) {
        std::string fieldError;
        if (!parseFieldArray(root.get("fields"), definition.fields, fieldError, "/fields")) {
            if (error) *error = std::move(fieldError);
            return false;
        }
    }
    return true;
}

bool validateNodeDefinition(const SchemaNode& node, const std::string& path, std::string& error,
                            std::unordered_set<const SchemaDefinition*>& activeDefinitions);

bool validateDefinition(const SchemaDefinition& definition, const std::string& path, std::string& error,
                        std::unordered_set<const SchemaDefinition*>& activeDefinitions) {
    if (!activeDefinitions.insert(&definition).second) {
        error = path + " contains an inline schema cycle";
        return false;
    }
    std::unordered_set<std::string> names;
    for (const auto& field : definition.fields) {
        if (field.name.empty() || !names.insert(field.name).second) {
            error = path + " field names must be non-empty and unique";
            activeDefinitions.erase(&definition);
            return false;
        }
        if (!validateNodeDefinition(field, path + "/fields/" + pointerSegment(field.name), error,
                                    activeDefinitions)) {
            activeDefinitions.erase(&definition);
            return false;
        }
    }
    activeDefinitions.erase(&definition);
    return true;
}

bool validateNodeDefinition(const SchemaNode& node, const std::string& path, std::string& error,
                            std::unordered_set<const SchemaDefinition*>& activeDefinitions) {
    if (node.refVersion < 0) {
        error = path + "/refVersion must not be negative";
        return false;
    }
    if (node.ref.empty() && node.refVersion != 0) {
        error = path + "/refVersion requires ref";
        return false;
    }
    if (!node.ref.empty() &&
        (node.type != ValueType::Any || node.elementType != ValueType::Any || node.objectSchema ||
         node.itemSchema || !node.variants.empty() || !node.discriminator.empty() ||
         !node.discriminatorMapping.empty() || !node.enumValues.empty() || node.minimum || node.maximum ||
         node.minLength || node.maxLength || node.minItems || node.maxItems)) {
        error = path + " cannot combine ref with type, constraints, fields, items, or union";
        return false;
    }
    if (node.type != ValueType::Array &&
        (node.elementType != ValueType::Any || node.itemSchema || node.minItems || node.maxItems)) {
        error = path + " array item constraints require type array";
        return false;
    }
    if (node.type != ValueType::String && (node.enumValues.size() > 0 || node.minLength || node.maxLength)) {
        error = path + " string constraints require type string";
        return false;
    }
    if (node.type != ValueType::Number && node.type != ValueType::Integer &&
        (node.minimum || node.maximum)) {
        error = path + " numeric constraints require type number or integer";
        return false;
    }
    if (node.type != ValueType::Object && node.objectSchema) {
        error = path + " object fields require type object";
        return false;
    }
    if (!node.variants.empty() &&
        (node.objectSchema || node.itemSchema || node.elementType != ValueType::Any || !node.enumValues.empty() ||
         node.minimum || node.maximum || node.minLength || node.maxLength || node.minItems || node.maxItems)) {
        error = path + " union cannot combine with object, array, scalar, or length constraints";
        return false;
    }
    if (!node.variants.empty() && node.discriminator.empty() && !node.discriminatorMapping.empty()) {
        error = path + " discriminatorMapping requires discriminator";
        return false;
    }
    if (!node.variants.empty() && !node.discriminator.empty()) {
        for (const auto& [value, target] : node.discriminatorMapping) {
            if (value.empty() || target.empty()) {
                error = path + "/discriminatorMapping keys and values must not be empty";
                return false;
            }
            size_t mappingIndex = 0;
            const auto [end, parseError] =
                std::from_chars(target.data(), target.data() + target.size(), mappingIndex);
            if (parseError == std::errc{}) {
                if (end != target.data() + target.size() || mappingIndex >= node.variants.size()) {
                    error = path + "/discriminatorMapping points outside the union";
                    return false;
                }
            } else {
                const bool found = std::any_of(node.variants.begin(), node.variants.end(),
                                               [&](const SchemaNode& variant) { return variant.ref == target; });
                if (!found) {
                    error = path + "/discriminatorMapping ref is not a union alternative";
                    return false;
                }
            }
        }
    } else if (!node.discriminatorMapping.empty()) {
        error = path + "/discriminatorMapping requires a union";
        return false;
    }
    if (node.variants.empty() && !node.discriminator.empty()) {
        error = path + "/discriminator requires a union";
        return false;
    }
    if (node.objectSchema &&
        !validateDefinition(*node.objectSchema, path + "/fields", error, activeDefinitions))
        return false;
    if (node.itemSchema &&
        !validateNodeDefinition(*node.itemSchema, path + "/items", error, activeDefinitions))
        return false;
    for (size_t index = 0; index < node.variants.size(); ++index) {
        if (!validateNodeDefinition(node.variants[index], path + "/union/" + std::to_string(index), error,
                                    activeDefinitions))
            return false;
    }
    return true;
}

SchemaRegistrationStatus registerDefinition(const SchemaDefinition& definition, bool replaceExisting,
                                             std::string* error) {
    if (definition.id.empty()) {
        if (error) *error = "schema id must not be empty";
        return SchemaRegistrationStatus::Invalid;
    }
    if (definition.version <= 0) {
        if (error) *error = "schema version must be positive";
        return SchemaRegistrationStatus::Invalid;
    }
    std::string validationError;
    std::unordered_set<const SchemaDefinition*> activeDefinitions;
    if (!validateDefinition(definition, "schema", validationError, activeDefinitions)) {
        if (error) *error = validationError;
        return SchemaRegistrationStatus::Invalid;
    }

    auto& versions = schemas()[definition.id];
    const auto  it = versions.find(definition.version);
    if (it != versions.end()) {
        if (!replaceExisting) {
            if (error) {
                *error = "schema '" + definition.id + "' version " + std::to_string(definition.version) +
                         " is already registered";
            }
            return SchemaRegistrationStatus::Conflict;
        }
        it->second = definition;
        return SchemaRegistrationStatus::Replaced;
    }
    versions.emplace(definition.version, definition);
    return SchemaRegistrationStatus::Registered;
}

const SchemaDefinition* referencedSchema(const SchemaNode& node) {
    if (node.ref.empty()) return nullptr;
    if (node.refVersion > 0) return SchemaRegistry::resolve(node.ref, node.refVersion);
    return SchemaRegistry::find(node.ref);
}

void validateNode(const SchemaNode& node, JsonValue value, const std::string& path,
                  std::vector<ValidationError>& errors, std::set<std::string>& activeRefs);

void validateObject(const SchemaDefinition& schema, JsonValue value, const std::string& path,
                    std::vector<ValidationError>& errors, std::set<std::string>& activeRefs) {
    if (!value.isObject()) {
        addError(errors, path, "type", "expected object");
        return;
    }
    std::unordered_set<std::string> known;
    for (const auto& field : schema.fields) {
        known.insert(field.name);
        const JsonValue fieldValue = value.get(field.name.c_str());
        const std::string fieldPath = path + "/" + pointerSegment(field.name);
        if (!fieldValue) {
            if (field.required) addError(errors, fieldPath, "required", "required field is missing");
            continue;
        }
        validateNode(field, fieldValue, fieldPath, errors, activeRefs);
    }
    if (!schema.additionalProperties) {
        auto keys = value.keys();
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys) {
            if (!known.contains(key))
                addError(errors, path + "/" + pointerSegment(key), "additional_property", "field is not declared");
        }
    }
}

bool variantMatchesDiscriminator(const SchemaNode& variant, const std::string& discriminator,
                                 const std::string& value) {
    const SchemaDefinition* definition = variant.objectSchema.get();
    if (!definition && !variant.ref.empty()) definition = referencedSchema(variant);
    if (!definition) return false;
    for (const auto& field : definition->fields) {
        if (field.name == discriminator &&
            std::find(field.enumValues.begin(), field.enumValues.end(), value) != field.enumValues.end())
            return true;
    }
    return false;
}

void validateUnionValue(const SchemaNode& node, JsonValue value, const std::string& path,
                        std::vector<ValidationError>& errors, std::set<std::string>& activeRefs) {
    std::vector<size_t> candidates;
    if (!node.discriminator.empty()) {
        if (!value.isObject()) {
            addError(errors, path, "type", "discriminator requires an object");
            return;
        }
        const JsonValue discriminatorValue = value.get(node.discriminator.c_str());
        if (!discriminatorValue.isString()) {
            addError(errors, path + "/" + pointerSegment(node.discriminator), "discriminator",
                     "discriminator value must be a string");
            return;
        }
        const std::string valueText = discriminatorValue.asString();
        const auto mapping = node.discriminatorMapping.find(valueText);
        if (mapping != node.discriminatorMapping.end()) {
            size_t index = 0;
            const auto [end, parseError] =
                std::from_chars(mapping->second.data(), mapping->second.data() + mapping->second.size(), index);
            if (parseError == std::errc{} && end == mapping->second.data() + mapping->second.size() &&
                index < node.variants.size()) {
                candidates.push_back(index);
            } else {
                for (size_t indexValue = 0; indexValue < node.variants.size(); ++indexValue)
                    if (node.variants[indexValue].ref == mapping->second) candidates.push_back(indexValue);
            }
        } else {
            for (size_t index = 0; index < node.variants.size(); ++index) {
                if (variantMatchesDiscriminator(node.variants[index], node.discriminator, valueText))
                    candidates.push_back(index);
            }
        }
        if (candidates.size() != 1) {
            addError(errors, path + "/" + pointerSegment(node.discriminator),
                     candidates.empty() ? "discriminator" : "discriminator_ambiguous",
                     candidates.empty() ? "discriminator value is not supported"
                                        : "discriminator matches more than one union alternative");
            return;
        }
        validateNode(node.variants[candidates.front()], value, path, errors, activeRefs);
        return;
    }

    for (size_t index = 0; index < node.variants.size(); ++index) {
        std::vector<ValidationError> candidateErrors;
        validateNode(node.variants[index], value, path, candidateErrors, activeRefs);
        if (candidateErrors.empty()) return;
    }
    addError(errors, path, "union", "value does not match any union alternative");
}

void validateNode(const SchemaNode& node, JsonValue value, const std::string& path,
                  std::vector<ValidationError>& errors, std::set<std::string>& activeRefs) {
    if (!node.ref.empty()) {
        const auto* referenced = referencedSchema(node);
        if (!referenced) {
            addError(errors, path, "schema_ref_not_found", "referenced schema is not registered");
            return;
        }
        const std::string key = referenced->id + "@" + std::to_string(referenced->version);
        if (!activeRefs.insert(key).second) {
            addError(errors, path, "schema_ref_cycle", "schema reference cycle detected");
            return;
        }
        validateObject(*referenced, value, path, errors, activeRefs);
        activeRefs.erase(key);
        return;
    }
    if (!node.variants.empty()) {
        validateUnionValue(node, value, path, errors, activeRefs);
        return;
    }
    if (!hasType(value, node.type)) {
        addError(errors, path, "type", "expected " + std::string(valueTypeName(node.type)));
        return;
    }
    if (node.type == ValueType::Number || node.type == ValueType::Integer) {
        const double number = value.asDouble();
        if (node.minimum && number < *node.minimum) addError(errors, path, "minimum", "value is below minimum");
        if (node.maximum && number > *node.maximum) addError(errors, path, "maximum", "value exceeds maximum");
    }
    if (node.type == ValueType::String) {
        const std::string text = value.asString();
        if (node.minLength && static_cast<int>(text.size()) < *node.minLength)
            addError(errors, path, "min_length", "string is shorter than minLength");
        if (node.maxLength && static_cast<int>(text.size()) > *node.maxLength)
            addError(errors, path, "max_length", "string is longer than maxLength");
        if (!node.enumValues.empty() &&
            std::find(node.enumValues.begin(), node.enumValues.end(), text) == node.enumValues.end())
            addError(errors, path, "enum", "value is not in the allowed set");
    }
    if (node.type == ValueType::Object && node.objectSchema)
        validateObject(*node.objectSchema, value, path, errors, activeRefs);
    if (node.type != ValueType::Array) return;
    if (node.minItems && static_cast<int>(value.size()) < *node.minItems)
        addError(errors, path, "min_items", "array has fewer than minItems");
    if (node.maxItems && static_cast<int>(value.size()) > *node.maxItems)
        addError(errors, path, "max_items", "array has more than maxItems");
    if (node.itemSchema) {
        for (size_t index = 0; index < value.size(); ++index)
            validateNode(*node.itemSchema, value.at(index), path + "/" + std::to_string(index), errors, activeRefs);
    } else if (node.elementType != ValueType::Any) {
        for (size_t index = 0; index < value.size(); ++index) {
            if (!hasType(value.at(index), node.elementType))
                addError(errors, path + "/" + std::to_string(index), "element_type",
                         "expected " + std::string(valueTypeName(node.elementType)));
        }
    }
}

bool migrationReaches(const std::string& schemaId, int start, int target) {
    std::set<int> visited;
    int          current = start;
    while (visited.insert(current).second) {
        if (current == target) return true;
        const auto it = migrationSteps().find(MigrationKey{schemaId, current});
        if (it == migrationSteps().end()) return false;
        current = it->second.toVersion;
    }
    return true;
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

const char* schemaRegistrationStatusName(SchemaRegistrationStatus status) {
    switch (status) {
        case SchemaRegistrationStatus::Registered: return "registered";
        case SchemaRegistrationStatus::Replaced: return "replaced";
        case SchemaRegistrationStatus::Conflict: return "conflict";
        case SchemaRegistrationStatus::Invalid: return "invalid";
    }
    return "invalid";
}

static eve::Result<SchemaRegistrationStatus> registrationResult(SchemaRegistrationStatus status, std::string error,
                                                                 std::string path = {}) {
    if (status == SchemaRegistrationStatus::Conflict)
        return eve::Result<SchemaRegistrationStatus>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, error.empty() ? "schema version is already registered" : std::move(error),
            std::move(path), {}, "schema"));
    if (status == SchemaRegistrationStatus::Invalid)
        return eve::Result<SchemaRegistrationStatus>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, error.empty() ? "invalid schema definition" : std::move(error),
            std::move(path), {}, "schema"));
    return eve::Result<SchemaRegistrationStatus>::success(
        status, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<SchemaRegistrationStatus> SchemaRegistry::registerSchema(const SchemaDefinition& definition) {
    std::string error;
    return registrationResult(registerDefinition(definition, true, &error), std::move(error), definition.id);
}

eve::Result<SchemaRegistrationStatus> SchemaRegistry::registerFromJson(const std::string& json) {
    SchemaDefinition definition;
    std::string error;
    if (!parseDefinition(json, definition, &error))
        return eve::Result<SchemaRegistrationStatus>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, error.empty() ? "invalid schema JSON" : error, {}, {}, "schema"));
    return registrationResult(registerDefinition(definition, true, &error), std::move(error), definition.id);
}

const SchemaDefinition* SchemaRegistry::find(const std::string& id) {
    const auto it = schemas().find(id);
    return it == schemas().end() || it->second.empty() ? nullptr : &it->second.rbegin()->second;
}

eve::Result<SchemaRegistrationStatus> SchemaRegistry::registerVersioned(const SchemaDefinition& definition) {
    std::string error;
    return registrationResult(registerDefinition(definition, false, &error), std::move(error), definition.id);
}

eve::Result<SchemaRegistrationStatus> SchemaRegistry::registerFromJsonVersioned(const std::string& json) {
    SchemaDefinition definition;
    std::string error;
    if (!parseDefinition(json, definition, &error))
        return eve::Result<SchemaRegistrationStatus>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, error.empty() ? "invalid schema JSON" : std::move(error), {}, {}, "schema"));
    return registerVersioned(definition);
}

const SchemaDefinition* SchemaRegistry::resolve(const std::string& schemaId, int schemaVersion) {
    const auto schemaIt = schemas().find(schemaId);
    if (schemaIt == schemas().end()) return nullptr;
    const auto versionIt = schemaIt->second.find(schemaVersion);
    return versionIt == schemaIt->second.end() ? nullptr : &versionIt->second;
}

std::vector<int> SchemaRegistry::versions(const std::string& schemaId) {
    std::vector<int> result;
    const auto        it = schemas().find(schemaId);
    if (it == schemas().end()) return result;
    result.reserve(it->second.size());
    for (const auto& [version, definition] : it->second) {
        (void)definition;
        result.push_back(version);
    }
    return result;
}

eve::Result<void> SchemaRegistry::remove(const std::string& id) {
    const bool removed = schemas().erase(id) != 0;
    for (auto it = migrationSteps().begin(); it != migrationSteps().end();) {
        if (it->first.first == id) it = migrationSteps().erase(it);
        else ++it;
    }
    if (!removed)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "schema id is not registered", id, {}, "schema"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SchemaRegistry::remove(const std::string& id, int schemaVersion) {
    const auto schemaIt = schemas().find(id);
    if (schemaIt == schemas().end())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "schema id is not registered", id, {}, "schema"));
    const bool removed = schemaIt->second.erase(schemaVersion) != 0;
    if (schemaIt->second.empty()) schemas().erase(schemaIt);
    for (auto it = migrationSteps().begin(); it != migrationSteps().end();) {
        if (it->first.first == id &&
            (it->first.second == schemaVersion || it->second.toVersion == schemaVersion))
            it = migrationSteps().erase(it);
        else ++it;
    }
    if (!removed)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "schema version is not registered", id, {}, "schema"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void SchemaRegistry::clear() {
    schemas().clear();
    migrationSteps().clear();
}

int SchemaRegistry::count() { return static_cast<int>(schemas().size()); }

int SchemaRegistry::versionCount() {
    int result = 0;
    for (const auto& [id, versions] : schemas()) {
        (void)id;
        result += static_cast<int>(versions.size());
    }
    return result;
}

std::vector<std::string> SchemaRegistry::ids() {
    std::vector<std::string> result;
    result.reserve(schemas().size());
    for (const auto& item : schemas()) result.push_back(item.first);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<ValidationError> SchemaRegistry::validate(const std::string& schemaId, const std::string& json) {
    const auto* schema = find(schemaId);
    std::vector<ValidationError> errors;
    if (!schema) {
        addError(errors, "", "schema_not_found", "schema '" + schemaId + "' is not registered");
        return errors;
    }
    return validate(schemaId, schema->version, json);
}

std::vector<ValidationError> SchemaRegistry::validate(const std::string& schemaId, int schemaVersion,
                                                       const std::string& json) {
    std::vector<ValidationError> errors;
    const auto*                  schema = resolve(schemaId, schemaVersion);
    if (!schema) {
        addError(errors, "", "schema_version_not_found",
                 "schema '" + schemaId + "' version " + std::to_string(schemaVersion) + " is not registered");
        return errors;
    }
    std::string parseError;
    const auto  document = eve::json::Document::parse(json, &parseError);
    const JsonValue root = document.root();
    if (!document.valid()) {
        addError(errors, "", "invalid_json", parseError);
        return errors;
    }
    std::set<std::string> activeRefs;
    activeRefs.insert(schema->id + "@" + std::to_string(schema->version));
    validateObject(*schema, root, "", errors, activeRefs);
    return errors;
}

eve::Result<void> SchemaRegistry::registerMigration(const std::string& schemaId, int fromVersion, int toVersion,
                                                     MigrationFunction migration) {
    if (schemaId.empty() || fromVersion <= 0 || toVersion <= 0 || !migration)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "migration id, versions, and function are required");
    if (fromVersion == toVersion)
        return failure<void>(eve::DiagnosticCode::Conflict, "a self migration edge is a cycle", "schemaVersion");
    if (fromVersion > toVersion)
        return failure<void>(eve::DiagnosticCode::Unsupported,
                             "schema downgrade migrations are not supported", "schemaVersion");

    const MigrationKey key{schemaId, fromVersion};
    if (migrationSteps().contains(key))
        return failure<void>(eve::DiagnosticCode::Conflict, "a migration edge already exists", "schemaVersion");
    if (migrationReaches(schemaId, toVersion, fromVersion))
        return failure<void>(eve::DiagnosticCode::Conflict, "migration edge would create a cycle", "schemaVersion");

    try {
        auto candidate = migrationSteps();
        candidate.emplace(key, MigrationStep{toVersion, std::move(migration)});
        migrationSteps().swap(candidate);
    } catch (const std::exception& exception) {
        return failure<void>(eve::DiagnosticCode::Failed,
                             std::string("migration registration failed: ") + exception.what());
    } catch (...) {
        return failure<void>(eve::DiagnosticCode::Failed, "migration registration failed");
    }
    return eve::Result<void>::success();
}

eve::Result<SchemaCompatibility> SchemaRegistry::queryCompatibility(const std::string& schemaId, int fromVersion,
                                                                       int toVersion) {
    if (schemaId.empty() || fromVersion <= 0 || toVersion <= 0)
        return failure<SchemaCompatibility>(eve::DiagnosticCode::InvalidArgument,
                                             "schema id and versions must be positive", "schemaVersion");
    if (!resolve(schemaId, fromVersion) || !resolve(schemaId, toVersion))
        return failure<SchemaCompatibility>(eve::DiagnosticCode::UnknownVersion,
                                             "source or target schema version is not registered", "schemaVersion");
    if (fromVersion > toVersion)
        return failure<SchemaCompatibility>(eve::DiagnosticCode::Unsupported,
                                             "schema downgrade compatibility is not supported", "schemaVersion");

    SchemaCompatibility result;
    result.schemaId = schemaId;
    result.fromVersion = fromVersion;
    result.toVersion = toVersion;
    result.versions.push_back(fromVersion);
    if (fromVersion == toVersion) return eve::Result<SchemaCompatibility>::success(std::move(result));

    std::set<int> visited;
    int           current = fromVersion;
    while (current != toVersion) {
        if (current > toVersion)
            return failure<SchemaCompatibility>(eve::DiagnosticCode::Unsupported,
                                                 "migration chain would downgrade the payload", "schemaVersion");
        if (!visited.insert(current).second)
            return failure<SchemaCompatibility>(eve::DiagnosticCode::Conflict,
                                                 "migration chain contains a cycle", "schemaVersion");
        const auto it = migrationSteps().find(MigrationKey{schemaId, current});
        if (it == migrationSteps().end())
            return failure<SchemaCompatibility>(eve::DiagnosticCode::Unsupported,
                                                 "migration chain is missing an explicit edge", "schemaVersion");
        if (it->second.toVersion <= current)
            return failure<SchemaCompatibility>(eve::DiagnosticCode::Conflict,
                                                 "migration chain contains a non-forward edge", "schemaVersion");
        if (it->second.toVersion > toVersion)
            return failure<SchemaCompatibility>(eve::DiagnosticCode::Unsupported,
                                                 "migration edge overshoots the requested target", "schemaVersion");
        current = it->second.toVersion;
        if (!resolve(schemaId, current))
            return failure<SchemaCompatibility>(eve::DiagnosticCode::UnknownVersion,
                                                 "migration chain reaches an unregistered schema version",
                                                 "schemaVersion");
        result.versions.push_back(current);
    }
    return eve::Result<SchemaCompatibility>::success(std::move(result));
}

eve::Result<eve::Value> SchemaRegistry::migrate(const std::string& schemaId, int fromVersion, int toVersion,
                                                const eve::Value& input) {
    auto compatibility = queryCompatibility(schemaId, fromVersion, toVersion);
    if (!compatibility.ok()) return eve::Result<eve::Value>::failure(compatibility.status());

    auto inputJson = input.toJson();
    if (!inputJson.ok()) return eve::Result<eve::Value>::failure(inputJson.status());
    const auto sourceErrors = validate(schemaId, fromVersion, inputJson.value());
    if (!sourceErrors.empty())
        return failure<eve::Value>(eve::DiagnosticCode::ParseError,
                                   "migration input does not satisfy its source schema", sourceErrors.front().path);

    eve::Value current;
    try {
        current = input;
        const auto& path = compatibility.value().versions;
        std::vector<MigrationFunction> chain;
        chain.reserve(path.size() > 0 ? path.size() - 1 : 0);
        for (size_t index = 1; index < path.size(); ++index) {
            const auto step = migrationSteps().find(MigrationKey{schemaId, path[index - 1]});
            if (step == migrationSteps().end())
                return failure<eve::Value>(eve::DiagnosticCode::Unsupported,
                                           "migration chain changed while it was being evaluated", "schemaVersion");
            chain.push_back(step->second.function);
        }
        for (size_t index = 1; index < path.size(); ++index) {
            auto next = chain[index - 1](current);
            if (!next.ok()) return eve::Result<eve::Value>::failure(next.status());
            eve::Value candidate = std::move(next).takeValue();
            auto        candidateJson = candidate.toJson();
            if (!candidateJson.ok()) return eve::Result<eve::Value>::failure(candidateJson.status());
            const auto errors = validate(schemaId, path[index], candidateJson.value());
            if (!errors.empty())
                return failure<eve::Value>(eve::DiagnosticCode::ParseError,
                                           "migration output does not satisfy its target schema", errors.front().path);
            current = std::move(candidate);
        }
    } catch (const std::exception& exception) {
        return failure<eve::Value>(eve::DiagnosticCode::Failed,
                                   std::string("migration execution failed: ") + exception.what());
    } catch (...) {
        return failure<eve::Value>(eve::DiagnosticCode::Failed, "migration execution failed");
    }
    return eve::Result<eve::Value>::success(std::move(current));
}

eve::Result<std::string> SchemaRegistry::migrateJson(const std::string& schemaId, int fromVersion, int toVersion,
                                                      const std::string& json) {
    auto input = eve::Value::fromJson(json);
    if (!input.ok()) return eve::Result<std::string>::failure(input.status());
    auto migrated = migrate(schemaId, fromVersion, toVersion, input.value());
    if (!migrated.ok()) return eve::Result<std::string>::failure(migrated.status());
    return std::move(migrated).andThen([](eve::Value&& value) { return value.toJson(); });
}

}  // namespace eve::schema
