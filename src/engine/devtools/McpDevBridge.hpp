#pragma once

#include <cstddef>
#include <string>

namespace eve::dev {

/** @brief Thin hooks so McpServer.cpp need not include DevTool.hpp (avoids a cycle). */
bool               mcpDevAttached();
std::size_t        mcpCallgraphEvents();
std::size_t        mcpCallgraphStackDepth();
const std::string& mcpLastReport();
/** @brief VM-thread-only owning diagnostic publication; clears stale slicing data. */
void               mcpRecordSnippetError(std::string report);
std::string        mcpFormatError(const std::string& message);

}  // namespace eve::dev
