#pragma once

#include <Poco/JSON/Object.h>

#include <string>
#include <string_view>

namespace eve::dev {

/** @brief Whether a tool name belongs to the Agent development-session protocol. */
bool isAgentDevelopmentTool(std::string_view name);
/** @brief Execute one Agent development-session tool and return compact JSON. */
std::string callAgentDevelopmentTool(std::string_view name, Poco::JSON::Object::Ptr args);
/** @brief Comma-separated MCP tool schema fragments without surrounding array brackets. */
std::string_view agentDevelopmentToolSchemas();

}  // namespace eve::dev
