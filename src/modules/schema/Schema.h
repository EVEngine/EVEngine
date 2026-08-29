#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "schema/SchemaTypes.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>
#include <vector>

namespace eve::schema {

/** @brief Script-facing facade for runtime schema registration and validation. */
class EVENGINE_API Schema : public Module {
public:
    Module_REG(Schema);

    /** @brief Creates an empty facade over the process schema registry. */
    Schema();

    /** @brief Registers or replaces a schema encoded as JSON.
     * @return Success, or a structured parse/validation/conflict failure.
     */
    [[nodiscard]] eve::Result<void> registerJson(const std::string& json);
    /** @brief Registers a schema reflected from a script class or instance.
     *
     * Class-level `</ ... />` attributes supply `id`, `version`, `title`,
     * `description` and `additionalProperties`. Each non-method member becomes
     * a field: its default value infers the type and the JSON default, and
     * member-level `</ ... />` attributes carry `required`, `type`,
     * `elementType`, `title`, `description`, `reference`, `defaultJson`,
     * `values`, `minimum`/`min`, `maximum`/`max`, `minLength`, `maxLength`.
     * @return Success, or a structured reflection/registration failure.
     */
    [[nodiscard]] eve::Result<void> registerFromClass(const ssq::Object& classOrInstance);
    /** @brief Removes all registered schemas and cached validation errors. */
    void clear();
    /** @brief Returns whether a stable schema id is registered. */
    bool has(const std::string& id) const;
    /** @brief Returns whether an exact schema id and version is registered.
     * @param id Stable schema id.
     * @param version Exact schema version.
     */
    bool hasVersion(const std::string& id, int version) const;
    /** @brief Returns the number of registered schema ids. */
    int getSchemaCount() const;
    /** @brief Returns the number of registered versions for one schema id.
     * @param id Stable schema id.
     */
    int getSchemaVersionCount(const std::string& id) const;
    /** @brief Returns a schema version at an ascending enumeration index.
     * @param id Stable schema id.
     * @param index Version enumeration index.
     * @return Version, or zero when the index is out of range.
     */
    int getSchemaVersionAt(const std::string& id, int index) const;
    /** @brief Returns the id at a deterministic enumeration index. */
    std::string getSchemaId(int index) const;
    /** @brief Returns the highest registered schema version, or zero when absent. */
    int getSchemaVersion(const std::string& id) const;
    /** @brief Returns a schema title, or an empty string when absent. */
    std::string getSchemaTitle(const std::string& id) const;
    /** @brief Returns a schema description, or an empty string when absent. */
    std::string getSchemaDescription(const std::string& id) const;
    /** @brief Returns whether undeclared object members are accepted. */
    bool getSchemaAdditionalProperties(const std::string& id) const;
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

    /** @brief Validates JSON against the highest registered version and caches failures.
     * @return Success, or the first structured validation failure.
     */
    [[nodiscard]] eve::Result<void> validateJson(const std::string& schemaId, const std::string& json);
    /** @brief Validates JSON against one exact schema version.
     * @param schemaId Stable schema id.
     * @param version Exact schema version.
     * @param json JSON value to validate.
     * @return Success when validation succeeds; otherwise the first validation failure.
     */
    [[nodiscard("schema validation outcome must be checked")]] eve::Result<void> validateJsonVersioned(
        const std::string& schemaId, int version, const std::string& json);
    /** @brief Returns cached validation failure count. */
    int getValidationErrorCount() const;
    /** @brief Returns a failure's JSON Pointer-like path. */
    std::string getValidationErrorPath(int index) const;
    /** @brief Returns a stable machine-readable failure code. */
    std::string getValidationErrorCode(int index) const;
    /** @brief Returns a human-readable failure description. */
    std::string getValidationErrorMessage(int index) const;

private:
    /**
     * @brief Resolves a field for one validation operation.
     * @return Borrowed nullable field owned by the schema registry.
     * @ownership SchemaRegistry owns field definitions; callers must not delete or mutate the result.
     * @lifetime Valid until registry mutation or destruction; this private view is never retained.
     * @thread Call on the schema registry's owning thread.
     * @reentrancy Does not invoke callbacks and is invalid across re-entrant registry mutation.
     */
    const FieldDefinition*       field(const std::string& id, int index) const;
    std::vector<ValidationError> validationErrors_;
};

}  // namespace eve::schema
