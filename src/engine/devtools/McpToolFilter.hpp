#pragma once

/**
 * @file McpToolFilter.hpp
 * @brief Toolset / read-only / exclude filtering for the embedded MCP server.
 *
 * Agents drown when tools/list returns 130+ schemas. Env-driven filters shrink
 * the advertised surface and reject calls that are outside the active set, so
 * list and call stay consistent (Godot MCP toolset/read-only pattern).
 *
 * Environment:
 * - `EVE_MCP_TOOLSET` — comma-separated tags (`all` default):
 *   `core`, `debug`, `play`, `editor`, `host`, `scene`, `input`, `domain`, `agent`
 * - `EVE_MCP_READONLY=1` — hide / reject mutating tools
 * - `EVE_MCP_EXCLUDE` — comma-separated exact tool names to drop
 */

#include "common/Export.h"

#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {

/** @brief One advertised MCP tool after schema parse (name only for filtering). */
struct McpToolNameView {
    std::string name;
};

/**
 * @brief Classify a built-in or script tool into toolset tags.
 * @param name Tool name from tools/list / tools/call.
 * @return Stable tag labels (never empty; unknown names get `core`).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::vector<std::string> mcpToolTags(std::string_view name);

/** @brief Whether the tool mutates game / editor / host state. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool mcpToolMutates(std::string_view name);

/**
 * @brief Whether `name` is visible under the current env filter.
 * @remarks Call sites must use the same predicate for tools/list and tools/call.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION bool mcpToolAllowed(std::string_view name);

/**
 * @brief Filter a JSON tools array body in place (objects joined by commas).
 * @param toolsBody Contents inside `[...]` without the brackets.
 * @return Filtered body; empty when every tool was removed.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string filterMcpToolsBody(std::string_view toolsBody);

/**
 * @brief Human-readable rejection payload fields when a call is filtered out.
 * @return Compact JSON object text: ok/error/hint/nextTools.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string mcpToolFilteredPayload(std::string_view name);

/** @brief Test helper: clear cached env snapshot so the next allow check reloads. */
EVENGINE_API_FOUNDATION void mcpToolFilterReloadForTest();

}  // namespace eve::dev
