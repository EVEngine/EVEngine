#pragma once
#include "common/Export.h"


#include "editing/EditingResult.h"
#include "editing/EditingValue.h"

#include <string>

namespace eve::editing {

using EditorValue = Value;
using EditorStatus = Status;
template <class T>
using EditorResult = Result<T>;

/** @brief Serialize an EditorValue to deterministic compact JSON. */
EVENGINE_API_PLATFORM std::string editorValueToJson(const EditorValue& value);

/**
 * @brief Parse JSON into the pointer-free EditorValue protocol tree.
 * @param json UTF-8 JSON text.
 * @return Parsed value or a structured parse diagnostic.
 */
EVENGINE_API_PLATFORM EditorResult<EditorValue> editorValueFromJson(const std::string& json);

/** @brief Stable content hash derived from deterministic JSON serialization. */
EVENGINE_API_PLATFORM std::string editorValueContentHash(const EditorValue& value);

}  // namespace eve::editing
