#pragma once

/**
 * @file McpSceneDirector.hpp
 * @brief Shared helpers for MCP tools that drive the embedded scene_director kit.
 */

#include "common/Export.h"

#include <squirrel.h>

#include <string>

namespace eve::dev {

/**
 * @brief Compile and run a transient MCP snippet (no return value).
 * @thread Game-loop / MCP poll thread with an attached VM.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION bool runMcpVmSnippet(HSQUIRRELVM vm, const std::string& source, std::string* err);

/** @brief Compile+run a snippet that returns a value; serialize it as JSON. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callSceneDirectorReturn(HSQUIRRELVM        vm,
                                                                          const std::string& snippet,
                                                                          std::string*       err);

/** @brief Install scene_director.nut into the live VM (idempotent). */
[[nodiscard]] EVENGINE_API_FOUNDATION bool ensureSceneDirectorInstalled(HSQUIRRELVM vm, std::string* err);

/** @brief Format a scene-director tool failure as `error: <name>: <err>`. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string sceneDirectorToolError(const std::string& name,
                                                                         const std::string& err);

}  // namespace eve::dev
