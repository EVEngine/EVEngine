#pragma once

#include "common/Export.h"

#include <optional>
#include <string>
#include <vector>

namespace eve::schema {

/** @brief JSON-compatible value kinds understood by a schema field. */
enum class ValueType { Any, Null, Boolean, Integer, Number, String, Object, Array };

/** @brief Converts a stable schema type name to its enum value. */
EVENGINE_API std::optional<ValueType> valueTypeFromString(const std::string& name);

/** @brief Returns the stable lowercase name of a schema value type. */
EVENGINE_API const char* valueTypeName(ValueType type);

/** @brief Metadata and validation constraints for one object member. */
struct EVENGINE_API FieldDefinition {
    std::string              name;
    ValueType                type        = ValueType::Any;
    ValueType                elementType = ValueType::Any;
    bool                     required    = false;
    std::string              title;
    std::string              description;
    std::string              reference;
    std::string              defaultJson;
    std::vector<std::string> enumValues;
    std::optional<double>    minimum;
    std::optional<double>    maximum;
    std::optional<int>       minLength;
    std::optional<int>       maxLength;
};

/** @brief Versioned runtime schema for a JSON object. */
struct EVENGINE_API SchemaDefinition {
    std::string                  id;
    int                          version = 1;
    std::string                  title;
    std::string                  description;
    bool                         additionalProperties = true;
    std::vector<FieldDefinition> fields;
};

/** @brief One machine-readable validation failure. */
struct EVENGINE_API ValidationError {
    std::string path;
    std::string code;
    std::string message;
};

}  // namespace eve::schema
