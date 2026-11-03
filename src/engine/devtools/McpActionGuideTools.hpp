#pragma once

#include "common/Export.h"

#include <Poco/JSON/Object.h>
#include <string>
#include <string_view>

namespace eve::dev {

/** @brief True for the action-guide MCP family. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpActionGuideTool(std::string_view name);

/** @brief Main-thread tool dispatch for guide-pack validate/landmarks/capture/export. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpActionGuideTool(std::string_view        name,
                                                                         Poco::JSON::Object::Ptr args);

/** @brief Comma-separated schemas for tools/list. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpActionGuideToolSchemas();

/** @brief Clear the process-local guide-pack session used by get/set. */
EVENGINE_API_FOUNDATION void resetMcpActionGuide();

}  // namespace eve::dev
