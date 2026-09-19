#pragma once

// MCP runtime-observation tools.
//
// The embedded MCP server already exposes the debugger, the editor command
// protocol and the Play Host. What it did not expose was the engine's own
// runtime console (devtools/ConsolePanel): a Squirrel `print()` or an uncaught
// script error only reached the process stdout/stderr, which the stdio
// transport deliberately keeps free of anything but JSON-RPC frames. An
// unattended agent therefore could not see the primary diagnostic channel of a
// scripted game at all.
//
// This module owns the tool family that closes that gap:
//
//   eve_console_read   read retained console lines from a monotonic cursor
//   eve_console_write  append an agent marker to the same ordered stream
//   eve_console_clear  drop retained lines without rewinding the cursor
//   eve_screenshot_image  capture the presented frame as an MCP image item
//
// Console lines carry a process-lifetime sequence number so a caller can read
// incrementally and tell "nothing new happened" apart from "lines were dropped
// before I read them".

#include <Poco/JSON/Object.h>

#include <string>
#include <string_view>

namespace eve::dev {

/** @brief Whether `name` belongs to the MCP runtime console / frame-capture surface. */
bool isMcpRuntimeTool(std::string_view name);

/**
 * @brief Execute one runtime tool.
 * @param name Tool name accepted by isMcpRuntimeTool.
 * @param args Decoded `arguments` object (may be null).
 * @return Complete MCP `tools/call` result object JSON (content envelope included).
 */
std::string callMcpRuntimeTool(std::string_view name, Poco::JSON::Object::Ptr args);

/** @brief Comma-separated MCP tool schema fragments without surrounding array brackets. */
std::string_view mcpRuntimeToolSchemas();

/**
 * @brief Summary of the persistent crash/error log (`eve.log`).
 *
 * An MCP server dies with the process it serves, so the only way an agent can
 * learn why a previous run ended is the log written by common/CrashLog.h. This
 * is the same object `eve_crash_report` returns, without the tail lines.
 */
[[nodiscard]] Poco::JSON::Object::Ptr crashLogSummary();

}  // namespace eve::dev
