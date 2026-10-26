#pragma once
#include <Poco/JSON/Object.h>
#include <string_view>
#include "common/Export.h"
namespace eve::dev {
/** @brief Owner-thread geometry-inspection dispatch; worker only owns immutable CPU pixels. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callMcpEntityInspectionTool(std::string_view        name,
                                                                              Poco::JSON::Object::Ptr args);
/** @brief Source-owned schemas for the optional entity-inspection capability. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view mcpEntityInspectionSchemas();
}  // namespace eve::dev
