#pragma once
#include <Poco/JSON/Object.h>
#include <string>
#include <string_view>
#include "common/Export.h"

namespace eve::dev {
/** @brief Recognizes project-index discovery/refresh tools; no project code is executed. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpProjectIndexTool(std::string_view name);
/**
 * @brief Refresh or query current project evidence, including the project mode of eve_task_context.
 * @ownership Returned JSON text owns its contents; cached source records are process-owned.
 * @lifetime Project snapshots are replaced atomically after each successful scan.
 * @thread Main MCP poll thread; filesystem reads and derived cache updates only.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpProjectIndexTool(std::string_view        name,
                                                                          Poco::JSON::Object::Ptr args);
/**
 * @brief Comma-separated tool schemas without an enclosing array.
 * @ownership Borrowed process-owned immutable text.
 * @lifetime Valid until process exit.
 * @thread Synchronized static initialization, then immutable reads.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpProjectIndexToolSchemas();
}  // namespace eve::dev
