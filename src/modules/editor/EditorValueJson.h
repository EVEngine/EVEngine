#pragma once

#include "editor/EditorResult.h"
#include "editor/EditorValue.h"

#include <string>

namespace eve::editor {

/** @brief Serialize an EditorValue to deterministic compact JSON. */
std::string editorValueToJson(const EditorValue& value);

/**
 * @brief Parse JSON into the pointer-free EditorValue protocol tree.
 * @param json UTF-8 JSON text.
 * @return Parsed value or a structured parse diagnostic.
 */
EditorResult<EditorValue> editorValueFromJson(const std::string& json);

/** @brief Stable content hash derived from deterministic JSON serialization. */
std::string editorValueContentHash(const EditorValue& value);

}  // namespace eve::editor
