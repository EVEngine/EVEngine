#pragma once

/**
 * @file McpInputTools.hpp
 * @brief Playtest input batch tools for the embedded MCP server.
 *
 * Mirrors the Godot MCP runtime pattern: inject key/mouse/text/ui actions,
 * wait a number of game-loop polls, then optionally capture a frame — without
 * blocking the main thread inside tools/call (wait+capture completes via
 * pollMcpInputBatches on each McpServer::poll).
 */

#include <Poco/JSON/Object.h>

#include "common/Export.h"

#include <string>
#include <string_view>

namespace eve::dev {

/** @brief Whether `name` belongs to the input playtest tool family. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpInputTool(std::string_view name);

/**
 * @brief Execute one input tool.
 * @return Complete MCP `tools/call` result object JSON (content envelope included).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpInputTool(std::string_view        name,
                                                                   Poco::JSON::Object::Ptr args);

/** @brief Comma-free MCP tool schema fragments for the input family. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpInputToolSchemas();

/**
 * @brief Advance pending wait/capture batches once per game-loop poll.
 * @thread MCP poll / game-loop thread only.
 */
EVENGINE_API_FOUNDATION void pollMcpInputBatches();

/** @brief Drop pending batches (tests / server stop). */
EVENGINE_API_FOUNDATION void resetMcpInputBatchesForTest();

}  // namespace eve::dev
