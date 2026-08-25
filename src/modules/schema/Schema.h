#pragma once

#include "common/Module.h"
#include "schema/SchemaTypes.h"

#include <string>
#include <vector>

namespace eve::schema {

/** @brief Script-facing facade for runtime schema registration and validation. */
class EVENGINE_API Schema : public Module {
public:
    Module_REG(Schema);

    /** @brief Registers a schema encoded as JSON. */
    bool registerJson(const std::string& json);
    /** @brief Removes all registered schemas and cached validation errors. */
    void clear();
    /** @brief Returns whether a stable schema id is registered. */
    bool has(const std::string& id) const;
    /** @brief Returns the number of registered schemas. */
    int getSchemaCount() const;
    /** @brief Returns the id at a deterministic enumeration index. */
    std::string getSchemaId(int index) const;
    /** @brief Returns a schema version, or zero when absent. */
    int getSchemaVersion(const std::string& id) const;
    /** @brief Returns a schema title, or an empty string when absent. */
    std::string getSchemaTitle(const std::string& id) const;
    /** @brief Returns a schema description, or an empty string when absent. */
    std::string getSchemaDescription(const std::string& id) const;
    /** @brief Returns whether undeclared object members are accepted. */
    bool getSchemaAdditionalProperties(const std::string& id) const;
    /** @brief Returns the last registration error. */
    std::string getLastError() const;

    /** @brief Returns the number of fields in a schema. */
    int getFieldCount(const std::string& id) const;
    /** @brief Returns a field's stable name. */
    std::string getFieldName(const std::string& id, int index) const;
    /** @brief Returns a field's stable value type name. */
    std::string getFieldType(const std::string& id, int index) const;
    /** @brief Returns the element type name for an array field. */
    std::string getFieldElementType(const std::string& id, int index) const;
    /** @brief Returns whether a field must be present. */
    bool getFieldRequired(const std::string& id, int index) const;
    /** @brief Returns the short editor-facing field title. */
    std::string getFieldTitle(const std::string& id, int index) const;
    /** @brief Returns the editor-facing field description. */
    std::string getFieldDescription(const std::string& id, int index) const;
    /** @brief Returns the referenced definition/schema category. */
    std::string getFieldReference(const std::string& id, int index) const;
    /** @brief Returns the optional default value as JSON text. */
    std::string getFieldDefaultJson(const std::string& id, int index) const;
    /** @brief Returns whether a numeric minimum constraint is present. */
    bool getFieldHasMinimum(const std::string& id, int index) const;
    /** @brief Returns a numeric minimum, or zero when absent. */
    float getFieldMinimum(const std::string& id, int index) const;
    /** @brief Returns whether a numeric maximum constraint is present. */
    bool getFieldHasMaximum(const std::string& id, int index) const;
    /** @brief Returns a numeric maximum, or zero when absent. */
    float getFieldMaximum(const std::string& id, int index) const;
    /** @brief Returns the number of allowed string values. */
    int getFieldEnumCount(const std::string& id, int index) const;
    /** @brief Returns one allowed string value. */
    std::string getFieldEnumValue(const std::string& id, int fieldIndex, int valueIndex) const;

    /** @brief Validates JSON and caches structured failures for enumeration. */
    bool validateJson(const std::string& schemaId, const std::string& json);
    /** @brief Returns cached validation failure count. */
    int getValidationErrorCount() const;
    /** @brief Returns a failure's JSON Pointer-like path. */
    std::string getValidationErrorPath(int index) const;
    /** @brief Returns a stable machine-readable failure code. */
    std::string getValidationErrorCode(int index) const;
    /** @brief Returns a human-readable failure description. */
    std::string getValidationErrorMessage(int index) const;

private:
    const FieldDefinition*       field(const std::string& id, int index) const;
    std::string                  lastError_;
    std::vector<ValidationError> validationErrors_;
};

}  // namespace eve::schema
