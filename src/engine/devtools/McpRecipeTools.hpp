#pragma once

/**
 * @file McpRecipeTools.hpp
 * @brief Content recipe list/apply facade for agent task profiles.
 *
 * Agents name a registered recipe id + small params instead of free-form prop
 * scripts. Apply expands to a single scene_director.batch transaction.
 */

#include <Poco/JSON/Object.h>

#include "common/Export.h"

#include <string>
#include <string_view>

namespace eve::dev {

/** @brief Whether `name` is `eve_recipe_list` or `eve_recipe_apply`. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpRecipeTool(std::string_view name);

/**
 * @brief Execute one recipe tool.
 * @return Complete MCP `tools/call` result object JSON (content envelope included).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpRecipeTool(std::string_view        name,
                                                                    Poco::JSON::Object::Ptr args);

/** @brief Comma-free MCP tool schema fragments for the recipe family. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpRecipeToolSchemas();

}  // namespace eve::dev
