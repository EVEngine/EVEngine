#pragma once
#include <Poco/JSON/Object.h>
#include <string>
#include <string_view>
#include "common/Export.h"

namespace eve::dev {
/** @brief True for the frame snapshot MCP family. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool isMcpFrameCaptureTool(std::string_view name);
/** @brief Main-thread tool dispatch; workers only access immutable CPU leases. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpFrameCaptureTool(std::string_view        name,
                                                                          Poco::JSON::Object::Ptr args);
/** @brief Comma-separated schemas for tools/list. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpFrameCaptureSchemas();
/** @brief Main-thread disconnect cleanup; workers observe cancellation before publishing files. */
EVENGINE_API_FOUNDATION void resetMcpFrameCapture();
}  // namespace eve::dev
