#pragma once

/**
 * @file McpAssetKitTools.hpp
 * @brief MCP adapter for eve.asset-kit.api (list / get / resolve).
 *
 * Same catalog contract as scripts/asset_kits.py and share/eve/ai/asset-kits/.
 * Agents and internal systems must resolve kits before inventing Unity/Godot
 * sizes. Catalog source: cmake/sdk/asset-kits/catalog.json (embedded at build).
 */

#include <Poco/JSON/Object.h>

#include "common/Export.h"

#include <string>
#include <string_view>

namespace eve::dev {

/** @brief Whether `name` is list/get/resolve under the asset-kit family. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpAssetKitTool(std::string_view name);

/**
 * @brief Execute one asset-kit tool.
 * @return Complete MCP `tools/call` result object JSON (content envelope included).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpAssetKitTool(std::string_view        name,
                                                                      Poco::JSON::Object::Ptr args);

/** @brief Comma-free MCP tool schema fragments for the asset-kit family. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpAssetKitToolSchemas();

}  // namespace eve::dev
