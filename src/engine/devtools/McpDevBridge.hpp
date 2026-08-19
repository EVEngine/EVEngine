#pragma once

#include <cstddef>
#include <string>

namespace eve::dev {

/** Thin hooks so McpServer.cpp need not include DevTool.hpp (avoids a cycle). */
bool               mcpDevAttached();
std::size_t        mcpCallgraphEvents();
std::size_t        mcpCallgraphStackDepth();
const std::string& mcpLastReport();
std::string        mcpFormatError(const std::string& message);

}  // namespace eve::dev
