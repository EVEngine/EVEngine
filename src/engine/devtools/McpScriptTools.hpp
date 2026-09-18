#pragma once

/**
 * @file McpScriptTools.hpp
 * @brief Project-script → MCP export registry.
 *
 * Game developers describe their own domain models in Squirrel. Without an
 * export path those models are only reachable through generic escape hatches
 * (`eve_run_script`, `eve_host_script`), which an agent cannot discover from
 * `tools/list` and cannot call by name. This registry lets a project publish a
 * named, described, schema-carrying endpoint that the embedded server advertises
 * exactly like a built-in tool:
 *
 * @code
 * eve.mcp.tool("game_npcs", {
 *     description = "List live NPCs",
 *     inputSchema = { type = "object", properties = { lane = { type = "string" } } },
 *     handler     = function(args) { return { count = 3 }; }
 * });
 * @endcode
 *
 * Boundary: this is for observing and actuating **game-domain state**. Mutating
 * an editable document still belongs to the editor command protocol
 * (`editor.registerScriptCommand` + `eve_editor_execute`), which owns
 * transactions, validation and undo. Publishing a second write path for the same
 * document would break that authority.
 *
 * Thread affinity: registration and dispatch happen on the game-loop thread,
 * which is also where the MCP server polls. The registry is mutex-guarded so a
 * handler may re-enter it (for example to register another tool) without
 * deadlocking.
 */

#include <Poco/JSON/Object.h>

#include <squirrel.h>

#include <string>
#include <vector>

namespace ssq {
class VM;
}

namespace eve::dev {

/** @brief Whether `name` is exported by the project script registry. */
[[nodiscard]] bool isScriptTool(const std::string& name);

/** @brief Exported tool names in registration order. */
[[nodiscard]] std::vector<std::string> scriptToolNames();

/** @brief Number of exported tools (for `eve_status`). */
[[nodiscard]] int scriptToolCount();

/**
 * @brief Comma-free MCP tool schema fragments for every exported tool.
 * @return Object fragments without surrounding brackets, or an empty string.
 */
[[nodiscard]] std::string scriptToolSchemas();

/**
 * @brief Invoke one exported tool.
 * @param name Registered tool name.
 * @param args Decoded `arguments` object (may be null, passed as an empty table).
 * @return Complete MCP `tools/call` result object JSON (content envelope included).
 */
[[nodiscard]] std::string callScriptTool(const std::string& name, Poco::JSON::Object::Ptr args);

/**
 * @brief Drop registry entries owned by `owner`.
 * @param owner VM whose handlers may be released. Entries registered against a
 *        different VM are discarded without touching it: that VM may already be
 *        destroyed, and releasing into freed VM memory would corrupt the heap.
 */
void clearScriptTools(HSQUIRRELVM owner);

/** @brief Expose `eve.mcp.tool/remove/tools/clear` on a VM. */
void exposeMcpScriptApi(ssq::VM& vm);

/**
 * @brief Validate an exported tool name.
 *
 * Requires 3..64 characters of `[a-z][a-z0-9_]*` and rejects the prefixes used by
 * the built-in tables (`eve_`, `inspect_`, `set_`, `capture_`, `get_`), because a
 * built-in always wins at dispatch time and a shadowed tool would be silently
 * unreachable.
 */
[[nodiscard]] bool isValidScriptToolName(const std::string& name);

}  // namespace eve::dev
