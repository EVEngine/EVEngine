#pragma once
#include <Poco/JSON/Object.h>
#include <string>
#include <string_view>
#include "common/Export.h"

namespace eve::dev {
/** @brief Read-only architecture queries derived from the build's binding registry. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpArchitectureTool(std::string_view name);
/**
 * @brief Query evidence, candidate impact or a bounded task context; returns MCP envelope JSON.
 * @ownership Returns owning text, never pointers to world objects.
 * @lifetime Graph evidence is process-owned and immutable; returned text is independent.
 * @thread Registry/main MCP poll thread; no VM execution or world writes.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpArchitectureTool(std::string_view        name,
                                                                          Poco::JSON::Object::Ptr args);
/**
 * @brief Comma-free schema objects for the read-only architecture tools.
 * @ownership Borrowed view of process-owned immutable schema text.
 * @lifetime Valid for the remainder of the process after first initialization.
 * @thread Static initialization is synchronized; subsequent reads are safe.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpArchitectureToolSchemas();
}  // namespace eve::dev
