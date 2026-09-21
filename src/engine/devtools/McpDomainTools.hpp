#pragma once

/**
 * @file McpDomainTools.hpp
 * @brief Capability-backed domain tool families for the embedded MCP server.
 *
 * The built-in table grew feature by feature, so several modules that already
 * publish a query capability had no agent surface at all (`IDecalQuery`,
 * `ICameraObstructionQuery`, the profiler and the sensing debug hook). This
 * module hosts those families behind one prefix-dispatched interface so the
 * 2500-line server TU does not grow another few hundred lines per domain, and so
 * "the provider exists but no tool does" stops being invisible.
 *
 * A family must degrade observably: when its provider is trimmed out of the
 * build the tool answers with a named reason instead of pretending to succeed.
 */

#include <Poco/JSON/Object.h>

#include <string>
#include <string_view>

namespace eve::dev {

/** @brief Whether `name` belongs to a capability-backed domain tool family. */
[[nodiscard]] bool isMcpDomainTool(std::string_view name);

/**
 * @brief Execute one domain tool.
 * @param name Tool name accepted by isMcpDomainTool.
 * @param args Decoded `arguments` object (may be null).
 * @return Complete MCP `tools/call` result object JSON (content envelope included).
 */
[[nodiscard]] std::string callMcpDomainTool(std::string_view name, Poco::JSON::Object::Ptr args);

/** @brief Comma-free MCP tool schema fragments for the domain families. */
[[nodiscard]] std::string_view mcpDomainToolSchemas();

}  // namespace eve::dev
