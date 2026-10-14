#pragma once
#include "common/Export.h"

#include "editing/EditingProperty.h"
#include "editing/EditingResult.h"
#include "editing/EditingValue.h"

#include <string_view>

namespace eve::editing {

/**
 * @brief Project a PropertySchema into a JSON Schema object tree (pointer-free Value).
 * @param schema Authoring property schema.
 * @param schemaId Stable schema identity (usually the command id or target type).
 * @return Applied object suitable for MCP inputSchema / createSchema, or a structured failure.
 * @note Native pointers and runtime handles are never encoded.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<Value> propertySchemaToJsonSchema(const PropertySchema& schema,
                                                                             std::string_view      schemaId);

/**
 * @brief Build an explicit empty-object payload schema.
 * @param typeId Stable type id (default "emptyObject").
 */
[[nodiscard]] EVENGINE_API_PLATFORM PropertySchema emptyObjectPropertySchema(std::string_view typeId = "emptyObject");

}  // namespace eve::editing
