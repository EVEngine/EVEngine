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
[[nodiscard]] EVENGINE_API_FOUNDATION std::string callSceneDirectorReturn(HSQUIRRELVM vm, const std::string& snippet,
                                                                          std::string* err);

/**
 * @brief Install scene_director.nut into the live VM (idempotent).
 * @param allowHeavyWork When false, a missing kit is queued for `flushPendingSceneDirectorInstall`
 *        and the call returns false with err `deferred-to-update` (never compile in tools/call).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION bool ensureSceneDirectorInstalled(HSQUIRRELVM vm, std::string* err,
                                                                        bool allowHeavyWork);
/** @brief Convenience overload: allow heavy install immediately (tests / explicit install tool). */
[[nodiscard]] EVENGINE_API_FOUNDATION bool ensureSceneDirectorInstalled(HSQUIRRELVM vm, std::string* err);

/** @brief Run a deferred scene_director install on the game/MCP poll thread. */
EVENGINE_API_FOUNDATION void flushPendingSceneDirectorInstall(HSQUIRRELVM vm);

/** @brief Whether a deferred install is waiting for the next poll. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool sceneDirectorInstallPending();

/**
 * @brief Format a scene-director tool failure.
 * @remarks `deferred-to-update` becomes a structured retryable JSON object; other errors keep
 *          the legacy `error: <name>: <err>` text for existing clients.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string sceneDirectorToolError(const std::string& name,
                                                                         const std::string& err);

}  // namespace eve::dev
