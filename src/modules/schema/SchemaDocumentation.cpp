#include "schema/SchemaRegistry.h"

#include <sstream>
#include <string_view>
#include <utility>

namespace eve::schema {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Value nodeContract(const SchemaNode& node);

eve::Value fieldContract(const FieldDefinition& field) {
    eve::Value::Object result;
    result.emplace("name", eve::Value(field.name));
    result.emplace("required", eve::Value(field.required));
    if (!field.title.empty()) result.emplace("title", eve::Value(field.title));
    if (!field.description.empty()) result.emplace("description", eve::Value(field.description));
    if (!field.reference.empty()) result.emplace("displayReference", eve::Value(field.reference));
    if (!field.defaultJson.empty()) result.emplace("defaultJson", eve::Value(field.defaultJson));
    result.emplace("schema", nodeContract(field));
    return eve::Value(std::move(result));
}

eve::Value definitionContract(const SchemaDefinition& definition) {
    eve::Value::Object result;
    result.emplace("additionalProperties", eve::Value(definition.additionalProperties));
    eve::Value::Array fields;
    fields.reserve(definition.fields.size());
    for (const auto& field : definition.fields) fields.push_back(fieldContract(field));
    result.emplace("fields", eve::Value(std::move(fields)));
    return eve::Value(std::move(result));
}

eve::Value nodeContract(const SchemaNode& node) {
    eve::Value::Object result;
    result.emplace("type", eve::Value(valueTypeName(node.type)));
    if (node.elementType != ValueType::Any)
        result.emplace("elementType", eve::Value(valueTypeName(node.elementType)));
    if (!node.ref.empty()) {
        result.emplace("ref", eve::Value(node.ref));
        if (node.refVersion > 0) result.emplace("refVersion", eve::Value(node.refVersion));
    }
    if (node.objectSchema) result.emplace("object", definitionContract(*node.objectSchema));
    if (node.itemSchema) result.emplace("items", nodeContract(*node.itemSchema));
    if (!node.variants.empty()) {
        eve::Value::Array variants;
        variants.reserve(node.variants.size());
        for (const auto& variant : node.variants) variants.push_back(nodeContract(variant));
        result.emplace("union", eve::Value(std::move(variants)));
    }
    if (!node.discriminator.empty()) result.emplace("discriminator", eve::Value(node.discriminator));
    if (!node.discriminatorMapping.empty()) {
        eve::Value::Object mapping;
        for (const auto& [value, target] : node.discriminatorMapping)
            mapping.emplace(value, eve::Value(target));
        result.emplace("discriminatorMapping", eve::Value(std::move(mapping)));
    }
    if (node.minimum) result.emplace("minimum", eve::Value(*node.minimum));
    if (node.maximum) result.emplace("maximum", eve::Value(*node.maximum));
    if (node.minLength) result.emplace("minLength", eve::Value(*node.minLength));
    if (node.maxLength) result.emplace("maxLength", eve::Value(*node.maxLength));
    if (node.minItems) result.emplace("minItems", eve::Value(*node.minItems));
    if (node.maxItems) result.emplace("maxItems", eve::Value(*node.maxItems));
    if (!node.enumValues.empty()) {
        eve::Value::Array values;
        for (const auto& value : node.enumValues) values.emplace_back(value);
        result.emplace("enum", eve::Value(std::move(values)));
    }
    return eve::Value(std::move(result));
}

std::string markdownEscape(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char character : text) {
        if (character == '|') result += "\\|";
        else if (character == '\n') result += ' ';
        else result += character;
    }
    return result;
}

void appendFieldDocumentation(std::ostringstream& output, const FieldDefinition& field,
                              const std::string& parentPath, int depth) {
    const std::string path = parentPath + "/" + field.name;
    output << std::string(static_cast<size_t>(depth), ' ') << "- `" << markdownEscape(path) << "` — `"
           << valueTypeName(field.type) << "`, " << (field.required ? "required" : "optional");
    if (!field.ref.empty()) {
        output << ", ref=`" << markdownEscape(field.ref);
        if (field.refVersion > 0) output << "@" << field.refVersion;
        output << '`';
    }
    if (!field.discriminator.empty())
        output << ", discriminator=`" << markdownEscape(field.discriminator) << '`';
    if (!field.description.empty()) output << " — " << markdownEscape(field.description);
    output << '\n';
    if (field.objectSchema) {
        for (const auto& nested : field.objectSchema->fields)
            appendFieldDocumentation(output, nested, path, depth + 2);
    }
    if (field.itemSchema && field.itemSchema->objectSchema) {
        output << std::string(static_cast<size_t>(depth + 2), ' ') << "- array item shape:\n";
        for (const auto& nested : field.itemSchema->objectSchema->fields)
            appendFieldDocumentation(output, nested, path + "[]", depth + 4);
    }
}

}  // namespace

eve::Result<std::string> SchemaRegistry::generateDocumentation(const std::string& schemaId,
                                                                int schemaVersion) {
    const auto* schema = resolve(schemaId, schemaVersion);
    if (!schema)
        return failure<std::string>(eve::DiagnosticCode::UnknownVersion,
                                    "schema version is not registered", "schemaVersion");

    std::ostringstream output;
    output << "# " << markdownEscape(schema->id) << " (v" << schema->version << ")\n\n";
    output << "Language: Eve Schema v1\n\n";
    output << (schema->additionalProperties ? "Unknown object fields: allow\n\n"
                                            : "Unknown object fields: reject\n\n");
    if (!schema->title.empty()) output << "Title: " << markdownEscape(schema->title) << "\n\n";
    if (!schema->description.empty()) output << markdownEscape(schema->description) << "\n\n";
    output << "## Fields\n\n";
    for (const auto& field : schema->fields) appendFieldDocumentation(output, field, {}, 0);
    if (schema->fields.empty()) output << "(none)\n";
    output << "\nGenerated contract: `SchemaRegistry::generateBindingContract`\n";
    return eve::Result<std::string>::success(output.str());
}

eve::Result<std::string> SchemaRegistry::generateBindingContract(const std::string& schemaId,
                                                                  int schemaVersion) {
    const auto* schema = resolve(schemaId, schemaVersion);
    if (!schema)
        return failure<std::string>(eve::DiagnosticCode::UnknownVersion,
                                    "schema version is not registered", "schemaVersion");
    eve::Value::Object result;
    result.emplace("language", eve::Value("eve-schema-v1"));
    result.emplace("schemaId", eve::Value(schema->id));
    result.emplace("schemaVersion", eve::Value(schema->version));
    result.emplace("unknownFields", eve::Value(schema->additionalProperties ? "allow" : "reject"));
    result.emplace("root", definitionContract(*schema));
    auto encoded = eve::Value(std::move(result)).toJson();
    if (!encoded.ok()) return eve::Result<std::string>::failure(encoded.status());
    return eve::Result<std::string>::success(std::move(encoded).takeValue());
}

}  // namespace eve::schema
