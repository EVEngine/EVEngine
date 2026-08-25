#pragma once

#include "schema/SchemaTypes.h"

#include <string>
#include <vector>

namespace eve::schema {

/** @brief Process-wide registry for runtime gameplay schemas. */
class EVENGINE_API SchemaRegistry {
public:
    /** @brief Registers or replaces a schema with the same id.
     * @param definition Schema to copy into the registry.
     * @param error Optional registration failure description.
     * @return true when the definition is well formed and registered.
     */
    static bool registerSchema(const SchemaDefinition& definition, std::string* error = nullptr);

    /** @brief Parses and registers one schema definition from JSON. */
    static bool registerFromJson(const std::string& json, std::string* error = nullptr);

    /** @brief Finds a registered schema, or nullptr when absent. */
    static const SchemaDefinition* find(const std::string& id);

    /** @brief Removes a schema by stable id. */
    static bool remove(const std::string& id);

    /** @brief Clears all registered schemas. */
    static void clear();

    /** @brief Returns registered schema count. */
    static int count();

    /** @brief Returns registered ids in deterministic lexical order. */
    static std::vector<std::string> ids();

    /** @brief Validates JSON text against a registered schema.
     * @param schemaId Stable id of the schema to use.
     * @param json JSON-compatible value to validate.
     * @return All discovered failures; empty means valid.
     */
    static std::vector<ValidationError> validate(const std::string& schemaId, const std::string& json);
};

}  // namespace eve::schema
