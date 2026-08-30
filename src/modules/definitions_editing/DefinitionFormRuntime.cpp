#include "definitions_editing/DefinitionForm.h"

#include "editing/EditingValueJson.h"
#include "schema/SchemaTypes.h"

#include <cmath>

namespace eve::definitions_editing {
namespace {

PropertyType propertyType(const schema::FieldDefinition& field) {
    if (!field.enumValues.empty()) return PropertyType::Enum;
    if (!field.ref.empty() || !field.reference.empty()) return PropertyType::ObjectRef;
    switch (field.type) {
        case schema::ValueType::Boolean: return PropertyType::Bool;
        case schema::ValueType::Integer: return PropertyType::Int;
        case schema::ValueType::Number: return PropertyType::Float;
        case schema::ValueType::String: return PropertyType::String;
        case schema::ValueType::Array: return PropertyType::Array;
        case schema::ValueType::Object: return PropertyType::Struct;
        case schema::ValueType::Any:
        case schema::ValueType::Null: return PropertyType::ReadOnlyText;
    }
    return PropertyType::ReadOnlyText;
}

EditorValue defaultValue(const schema::FieldDefinition& field) {
    if (!field.defaultJson.empty()) {
        auto parsed = editorValueFromJson(field.defaultJson);
        if (parsed.value) return *parsed.value;
    }
    switch (field.type) {
        case schema::ValueType::Boolean: return false;
        case schema::ValueType::Integer: return int64_t{0};
        case schema::ValueType::Number: return 0.0;
        case schema::ValueType::String: return std::string{};
        case schema::ValueType::Array: return EditorValue::Array{};
        case schema::ValueType::Object: return EditorValue::Object{};
        case schema::ValueType::Any:
        case schema::ValueType::Null: return {};
    }
    return {};
}

EditorResult<DomainOperation> formError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<DomainOperation>::error(status, RuleId(rule), std::move(message));
}

}  // namespace

DefinitionSchemaFormTarget::DefinitionSchemaFormTarget(DefinitionDocument* document,
                                                       const schema::SchemaDefinition* schema)
    : document_(document), schema_(schema) {}

bool DefinitionSchemaFormTarget::matches(const SelectionSnapshot& selection) const {
    return document_ && selection.items.size() == 1 &&
           selection.items.front().target == TargetId(document_->targetId());
}

eve::Result<eve::Revision> DefinitionSchemaFormTarget::currentRevision(
    const SelectionSnapshot& selection) const {
    if (!matches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Definition form selection does not match its document",
            "editor.definition.form-selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(document_->revision()));
}

PropertySchema DefinitionSchemaFormTarget::schema(const SelectionSnapshot&) const {
    PropertySchema result;
    if (!schema_) return result;
    result.typeId = "definition." + schema_->id;
    result.version = static_cast<std::uint32_t>(schema_->version);
    for (const schema::FieldDefinition& field : schema_->fields) {
        PropertyDescriptor descriptor;
        descriptor.path = PropertyPath(field.name);
        descriptor.displayNameKey = field.title.empty() ? field.name : field.title;
        descriptor.descriptionKey = field.description;
        descriptor.category = "definition";
        descriptor.type = propertyType(field);
        descriptor.defaultValue = defaultValue(field);
        descriptor.numeric.minimum = field.minimum;
        descriptor.numeric.maximum = field.maximum;
        descriptor.enumItems = field.enumValues;
        descriptor.presenterHint = !field.ref.empty() || !field.reference.empty()
                                       ? "definition-reference-picker" : "schema-field";
        if (descriptor.type == PropertyType::ReadOnlyText) descriptor.flags = PropertyFlag::ReadOnly;
        if (field.required) descriptor.validators.push_back(RuleId("editor.definition.required"));
        result.properties.push_back(std::move(descriptor));
    }
    return result;
}

PropertyReadResult DefinitionSchemaFormTarget::read(const SelectionSnapshot& selection,
                                                     const PropertyPath& path) const {
    const auto descriptor = schema(selection).find(path);
    if (!matches(selection) || !descriptor) return {};
    auto parsed = editorValueFromJson(document_->json());
    const auto* object = parsed.value ? parsed.value->getIf<EditorValue::Object>() : nullptr;
    if (!object) return {PropertyReadState::Error, {}, parsed.diagnostics};
    const auto found = object->find(path.value());
    if (found == object->end()) return {PropertyReadState::Missing, {}, {}};
    if (descriptor->type == PropertyType::Float) {
        if (const auto* integer = found->second.getIf<int64_t>())
            return {PropertyReadState::Value, static_cast<double>(*integer), {}};
    }
    return {PropertyReadState::Value, found->second, {}};
}

EditorResult<DomainOperation> DefinitionSchemaFormTarget::makeSet(
    const SelectionSnapshot& selection, const PropertyPath& path, const EditorValue& value,
    PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    if (!matches(selection) || !schema_)
        return formError(EditorStatus::Rejected, "editor.definition.form-selection",
                         "Definition form selection does not match its document");
    auto descriptor = schema(selection).find(path);
    if (!descriptor)
        return formError(EditorStatus::Unsupported, "editor.definition.form-field",
                         "Definition schema does not contain field: " + path.value());
    if (hasPropertyFlag(descriptor->flags, PropertyFlag::ReadOnly))
        return formError(EditorStatus::Rejected, "editor.definition.form-read-only",
                         "Definition field is not directly editable");
    auto valid = validatePropertyValue(*descriptor, value);
    if (!valid.isAccepted()) {
        EditorResult<DomainOperation> result;
        result.status = valid.status; result.diagnostics = std::move(valid.diagnostics); return result;
    }
    return document_->makeSetField(path.value(), value);
}

EditorResult<DomainOperation> DefinitionSchemaFormTarget::makeReset(
    const SelectionSnapshot& selection, const PropertyPath& path) const {
    auto descriptor = schema(selection).find(path);
    if (!matches(selection) || !descriptor)
        return formError(EditorStatus::Unsupported, "editor.definition.form-field",
                         "Definition schema field is unavailable");
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

std::vector<EditorDiagnostic> DefinitionSchemaFormTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    if (!document_ || !schema_ || document_->type() != schema_->id || document_->version() != schema_->version) {
        diagnostics.push_back({RuleId("editor.definition.form-schema-mismatch"), DiagnosticSeverity::Error,
                               "Definition document and exact schema identity do not match"});
        return diagnostics;
    }
    auto parsed = editorValueFromJson(document_->json());
    const auto* object = parsed.value ? parsed.value->getIf<EditorValue::Object>() : nullptr;
    if (!object) {
        diagnostics.push_back({RuleId("editor.definition.form-object-required"), DiagnosticSeverity::Error,
                               "Schema-driven definition forms require an object payload"});
        return diagnostics;
    }
    const PropertySchema formSchema = schema({});
    for (const schema::FieldDefinition& field : schema_->fields) {
        const auto found = object->find(field.name);
        if (found == object->end()) {
            if (field.required)
                diagnostics.push_back({RuleId("editor.definition.required-field-missing"),
                                       DiagnosticSeverity::Error,
                                       "Required definition field is missing: " + field.name});
            continue;
        }
        auto descriptor = formSchema.find(PropertyPath(field.name));
        auto valid = validatePropertyValue(*descriptor, found->second);
        diagnostics.insert(diagnostics.end(), valid.diagnostics.begin(), valid.diagnostics.end());
    }
    return diagnostics;
}

}  // namespace eve::definitions_editing
