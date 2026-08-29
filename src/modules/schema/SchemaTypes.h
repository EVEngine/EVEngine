#pragma once

#include "common/Export.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace eve::schema {

/** @brief JSON-compatible value kinds understood by a schema field. */
enum class ValueType { Any, Null, Boolean, Integer, Number, String, Object, Array };

/** @brief Outcome of an exact schema-version registration. */
enum class SchemaRegistrationStatus { Registered, Replaced, Conflict, Invalid };

/** @brief Returns the stable lowercase name of a registration outcome. */
EVENGINE_API const char* schemaRegistrationStatusName(SchemaRegistrationStatus status);

/** @brief Writes the stable schema-registration status name. */
inline std::ostream& operator<<(std::ostream& stream, SchemaRegistrationStatus status) {
    return stream << schemaRegistrationStatusName(status);
}

/** @brief Converts a stable schema type name to its enum value. */
EVENGINE_API std::optional<ValueType> valueTypeFromString(const std::string& name);

/** @brief Returns the stable lowercase name of a schema value type. */
EVENGINE_API const char* valueTypeName(ValueType type);

struct SchemaDefinition;

/**
 * @brief Recursive node in the deliberately small Eve Schema language.
 *
 * Eve Schema is a data-validation and tooling contract, not a full JSON
 * Schema implementation. It supports JSON scalar/object/array kinds,
 * bounded scalar constraints, one array item node, whole-schema refs, and a
 * discriminator-aware union. It intentionally does not implement arbitrary
 * JSON Pointer fragments, patternProperties, conditionals, tuple arrays,
 * unevaluatedProperties, or arbitrary keyword composition.
 */
struct EVENGINE_API SchemaNode {
    /** @brief The value kind accepted by this node. */
    ValueType type = ValueType::Any;
    /** @brief Legacy homogeneous array constraint retained for compatibility. */
    ValueType elementType = ValueType::Any;
    /** @brief Whole-schema reference, for example `combat:damage`. */
    std::string ref;
    /** @brief Exact referenced schema version; zero means the highest version. */
    int refVersion = 0;
    /** @brief Inline object shape for an object node. */
    std::shared_ptr<const SchemaDefinition> objectSchema;
    /** @brief Recursive schema for every array item. */
    std::shared_ptr<const SchemaNode> itemSchema;
    /** @brief Union alternatives; at least one alternative is required. */
    std::vector<SchemaNode> variants;
    /** @brief Object member used to select a union alternative. */
    std::string discriminator;
    /** @brief Discriminator value to ref or zero-based variant index. */
    std::map<std::string, std::string> discriminatorMapping;
    /** @brief String enum values accepted by this node. */
    std::vector<std::string> enumValues;
    std::optional<double>    minimum;
    std::optional<double>    maximum;
    std::optional<int>       minLength;
    std::optional<int>       maxLength;
    std::optional<int>       minItems;
    std::optional<int>       maxItems;
};

/** @brief Metadata and validation constraints for one object member. */
struct EVENGINE_API FieldDefinition : SchemaNode {
    std::string name;
    bool        required = false;
    std::string title;
    std::string description;
    /** @brief Legacy display/category reference; use `ref` for schema refs. */
    std::string reference;
    std::string defaultJson;
};

/** @brief Versioned runtime schema for a JSON object.
 *
 * `version` is the schema version component of the registry key. JSON input
 * may spell this member `schemaVersion`; the legacy `version` spelling is
 * accepted by the compatibility facade.
 */
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
