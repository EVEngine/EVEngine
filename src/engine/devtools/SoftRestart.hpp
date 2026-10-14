#pragma once

/**
 * @file SoftRestart.hpp
 * @brief Soft-restart a live game without unloading the resource cache.
 *
 * AI agents debugging a game should not kill the process (and re-decode every
 * asset) just to get a clean run. Soft restart keeps Vulkan, modules, MCP and
 * ResourceManager intact while re-running script init with optional parameters.
 */

#include "common/Export.h"
#include "common/Result.h"

#include <Poco/JSON/Object.h>

#include <cstddef>
#include <cstdint>
#include <string>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace eve::dev {

/**
 * @brief Inputs for one soft restart.
 * @thread Game / MCP poll thread only.
 */
struct SoftRestartRequest {
    /** @brief Optional JSON object exposed to scripts as `eve.restartArgs`. */
    Poco::JSON::Object::Ptr args;
    /**
     * @brief When true (default), re-dofile tracked scripts after clearing
     *        persist/state roots so initializers run again. When false, only
     *        native providers are reset and `eve_restart` / `eve_init` is called.
     */
    bool reloadScripts = true;
};

/**
 * @brief Outcome of a successful soft restart.
 * @ownership Owning value; safe to keep across frames.
 */
struct SoftRestartReport {
    /** @brief Cached resource entries before the restart (must equal after). */
    std::size_t resourceCountBefore = 0;
    /** @brief Cached resource entries after the restart. */
    std::size_t resourceCountAfter = 0;
    /** @brief Whether tracked scripts were re-dofile'd. */
    bool scriptsReloaded = false;
    /** @brief Whether `eve_init` / `eve_restart` ran. */
    bool initCalled = false;
    /** @brief Echo of the args table that was installed (may be empty). */
    Poco::JSON::Object::Ptr args;
};

/**
 * @brief Soft-restart the game attached to @p vm.
 * @param vm Live game VM (must already have run `load.nut`).
 * @param request Args + reloadScripts flag.
 * @return Report on success; Failed/NotFound when the VM has no soft_restart_game
 *         helper or the restart fails.
 * @remarks Does not call ResourceManager::clear(). Resource counts in the report
 *          let callers prove assets were not dropped.
 * @thread Owner game-loop / MCP poll thread only.
 * @reentrancy Must not be called while a ReloadSession is active.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<SoftRestartReport> executeSoftRestart(
    HSQUIRRELVM vm, SoftRestartRequest request);

/**
 * @brief Reset every registered IStateProvider to its defaults.
 * @return Number of providers that reported success.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::uint32_t resetNativeStateProviders();

/**
 * @brief Current ResourceManager cache entry count (for restart telemetry).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::size_t resourceCacheCount();

}  // namespace eve::dev
