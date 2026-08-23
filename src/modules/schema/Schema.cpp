#include "schema/Schema.h"

#include "schema/SchemaRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::schema {

Module_IMPL(Schema, new Schema());

bool Schema::registerJson(const std::string& json) {
    lastError_.clear();
    return SchemaRegistry::registerFromJson(json, &lastError_);
}

void Schema::clear() {
    SchemaRegistry::clear();
    lastError_.clear();
    validationErrors_.clear();
}

bool Schema::has(const std::string& id) const { return SchemaRegistry::find(id) != nullptr; }
int  Schema::getSchemaCount() const { return SchemaRegistry::count(); }

std::string Schema::getSchemaId(int index) const {
    const auto values = SchemaRegistry::ids();
    return index >= 0 && static_cast<size_t>(index) < values.size() ? values[index] : std::string{};
}

int Schema::getSchemaVersion(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value ? value->version : 0;
}

std::string Schema::getSchemaTitle(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value ? value->title : std::string{};
}

std::string Schema::getSchemaDescription(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value ? value->description : std::string{};
}

bool Schema::getSchemaAdditionalProperties(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value && value->additionalProperties;
}

std::string Schema::getLastError() const { return lastError_; }

const FieldDefinition* Schema::field(const std::string& id, int index) const {
    const auto* value = SchemaRegistry::find(id);
    if (!value || index < 0 || static_cast<size_t>(index) >= value->fields.size()) return nullptr;
    return &value->fields[static_cast<size_t>(index)];
}

int Schema::getFieldCount(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value ? static_cast<int>(value->fields.size()) : 0;
}

std::string Schema::getFieldName(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->name : std::string{};
}
std::string Schema::getFieldType(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? valueTypeName(value->type) : std::string{};
}
std::string Schema::getFieldElementType(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? valueTypeName(value->elementType) : std::string{};
}
bool Schema::getFieldRequired(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->required;
}
std::string Schema::getFieldTitle(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->title : std::string{};
}
std::string Schema::getFieldDescription(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->description : std::string{};
}
std::string Schema::getFieldReference(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->reference : std::string{};
}
std::string Schema::getFieldDefaultJson(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->defaultJson : std::string{};
}
bool Schema::getFieldHasMinimum(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->minimum.has_value();
}
float Schema::getFieldMinimum(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->minimum ? static_cast<float>(*value->minimum) : 0.f;
}
bool Schema::getFieldHasMaximum(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->maximum.has_value();
}
float Schema::getFieldMaximum(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->maximum ? static_cast<float>(*value->maximum) : 0.f;
}
int Schema::getFieldEnumCount(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? static_cast<int>(value->enumValues.size()) : 0;
}
std::string Schema::getFieldEnumValue(const std::string& id, int fieldIndex, int valueIndex) const {
    const auto* value = field(id, fieldIndex);
    if (!value || valueIndex < 0 || static_cast<size_t>(valueIndex) >= value->enumValues.size()) return {};
    return value->enumValues[static_cast<size_t>(valueIndex)];
}

bool Schema::validateJson(const std::string& schemaId, const std::string& json) {
    validationErrors_ = SchemaRegistry::validate(schemaId, json);
    return validationErrors_.empty();
}
int         Schema::getValidationErrorCount() const { return static_cast<int>(validationErrors_.size()); }
std::string Schema::getValidationErrorPath(int index) const {
    return index >= 0 && static_cast<size_t>(index) < validationErrors_.size()
               ? validationErrors_[static_cast<size_t>(index)].path
               : std::string{};
}
std::string Schema::getValidationErrorCode(int index) const {
    return index >= 0 && static_cast<size_t>(index) < validationErrors_.size()
               ? validationErrors_[static_cast<size_t>(index)].code
               : std::string{};
}
std::string Schema::getValidationErrorMessage(int index) const {
    return index >= 0 && static_cast<size_t>(index) < validationErrors_.size()
               ? validationErrors_[static_cast<size_t>(index)].message
               : std::string{};
}

void Schema::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Schema::create, false);
    expose(cls);
}

void Schema::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Schema::getName);
    cls.addFunc("registerJson", &Schema::registerJson);
    cls.addFunc("clear", &Schema::clear);
    cls.addFunc("has", &Schema::has);
    cls.addFunc("getSchemaCount", &Schema::getSchemaCount);
    cls.addFunc("getSchemaId", &Schema::getSchemaId);
    cls.addFunc("getSchemaVersion", &Schema::getSchemaVersion);
    cls.addFunc("getSchemaTitle", &Schema::getSchemaTitle);
    cls.addFunc("getSchemaDescription", &Schema::getSchemaDescription);
    cls.addFunc("getSchemaAdditionalProperties", &Schema::getSchemaAdditionalProperties);
    cls.addFunc("getLastError", &Schema::getLastError);
    cls.addFunc("getFieldCount", &Schema::getFieldCount);
    cls.addFunc("getFieldName", &Schema::getFieldName);
    cls.addFunc("getFieldType", &Schema::getFieldType);
    cls.addFunc("getFieldElementType", &Schema::getFieldElementType);
    cls.addFunc("getFieldRequired", &Schema::getFieldRequired);
    cls.addFunc("getFieldTitle", &Schema::getFieldTitle);
    cls.addFunc("getFieldDescription", &Schema::getFieldDescription);
    cls.addFunc("getFieldReference", &Schema::getFieldReference);
    cls.addFunc("getFieldDefaultJson", &Schema::getFieldDefaultJson);
    cls.addFunc("getFieldHasMinimum", &Schema::getFieldHasMinimum);
    cls.addFunc("getFieldMinimum", &Schema::getFieldMinimum);
    cls.addFunc("getFieldHasMaximum", &Schema::getFieldHasMaximum);
    cls.addFunc("getFieldMaximum", &Schema::getFieldMaximum);
    cls.addFunc("getFieldEnumCount", &Schema::getFieldEnumCount);
    cls.addFunc("getFieldEnumValue", &Schema::getFieldEnumValue);
    cls.addFunc("validateJson", &Schema::validateJson);
    cls.addFunc("getValidationErrorCount", &Schema::getValidationErrorCount);
    cls.addFunc("getValidationErrorPath", &Schema::getValidationErrorPath);
    cls.addFunc("getValidationErrorCode", &Schema::getValidationErrorCode);
    cls.addFunc("getValidationErrorMessage", &Schema::getValidationErrorMessage);
}

}  // namespace eve::schema
