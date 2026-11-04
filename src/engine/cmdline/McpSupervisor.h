#pragma once

#include <string>

namespace eve::cmd::detail {
/** @brief Run the desktop stdio MCP supervisor synchronously on the main thread.
 * Owns a worker process and its pipes until exit; no VM pointers cross processes.
 * Timeout kills and reaps the worker, returning an MCP diagnostic. The next
 * request starts a fresh worker. Returns the supervisor's process exit status.
 */
[[nodiscard]] int runMcpSupervisor(const std::string& root, int timeoutMs);
}  // namespace eve::cmd::detail
