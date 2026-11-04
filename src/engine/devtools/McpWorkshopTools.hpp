#pragma once

/**
 * @file McpWorkshopTools.hpp
 * @brief Narrow workshop facade (`eve_workshop`) for the `workshop.rpg` profile.
 *
 * Combines recipe apply, play observe/submit, and mode status behind one stable
 * tool name so Cursor clients that never refresh `tools/list` can still drive
 * the workshop without Godot-style enable_toolset round-trips.
 */

#include <Poco/JSON/Object.h>

#include "common/Export.h"

#include <string>
#include <string_view>

namespace eve::dev {

/** @brief Whether `name` is the workshop facade. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpWorkshopTool(std::string_view name);

/**
 * @brief Execute `eve_workshop` with a narrow `op`.
 * @return Complete MCP `tools/call` result object JSON (content envelope included).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpWorkshopTool(std::string_view        name,
                                                                      Poco::JSON::Object::Ptr args);

/** @brief Comma-free MCP tool schema fragment for `eve_workshop`. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpWorkshopToolSchemas();

/** @brief Test helper: clear workshop mode overlay. */
EVENGINE_API_FOUNDATION void resetMcpWorkshopStateForTest();

}  // namespace eve::dev
