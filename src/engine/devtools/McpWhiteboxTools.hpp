#pragma once

/**
 * @file McpWhiteboxTools.hpp
 * @brief Fast white-box batch authoring + playtest orchestration over MCP.
 *
 * One recipe call can reset/stage a blockout (`eve_scene_batch`), inject
 * playtest input (`eve_input_batch`), and optionally run Play Host asserts
 * (`eve_play`). Wait/capture still completes across `eve_input_poll` — this
 * tool never blocks the game loop.
 */

#include <Poco/JSON/Object.h>

#include "common/Export.h"

#include <string>
#include <string_view>

namespace eve::dev {

/** @brief Whether `name` belongs to the white-box playtest tool family. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpWhiteboxTool(std::string_view name);

/**
 * @brief Execute one white-box tool.
 * @return Complete MCP `tools/call` result object JSON (content envelope included).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpWhiteboxTool(std::string_view        name,
                                                                      Poco::JSON::Object::Ptr args);

/** @brief Comma-free MCP tool schema fragments for the white-box family. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpWhiteboxToolSchemas();

}  // namespace eve::dev
