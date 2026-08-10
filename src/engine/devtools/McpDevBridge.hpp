#pragma once

#include <cstddef>
#include <string>

namespace eve::dev {

/** Thin hooks so McpServer.inc need not include DevTool.hpp (MSVC export bloat). */
bool               mcpDevAttached();
std::size_t        mcpCallgraphEvents();
std::size_t        mcpCallgraphStackDepth();
const std::string& mcpLastReport();
std::string        mcpFormatError(const std::string& message);

}  // namespace eve::dev
