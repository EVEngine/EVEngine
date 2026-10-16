#pragma once

/**
 * @file GameWorldContext.hpp
 * @brief Runtime game-world Discover snapshot for MCP / Play Host.
 *
 * After scripts initialize dynamically configured systems, agents need a
 * first-class MCP path to learn what is actually live — not a second copy of
 * on-disk prose. This module owns:
 *
 * - `config.title` and `config.modules` / `optionalModules` from the live VM
 * - live module slots / gameplay `domains`
 * - an optional script brief (`eve.dev.publishWorldContext`) for summary / genre /
 *   tags / dimensions / perspective (and any title override)
 * - the snapshot for `eve_play` status, `eve_game_world`, and `eve://game-world`
 *
 * Authored files such as `game.agent.json` are not a world authority here;
 * agents that want static prose can read those files directly.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace eve::dev {

/** @brief Schema id for the game-world snapshot returned to agents. */
inline constexpr std::string_view kGameWorldSchemaId = "evengine.game-world";
/** @brief Schema version for the game-world snapshot. */
inline constexpr std::int64_t kGameWorldSchemaVersion = 1;

/**
 * @brief Validate one runtime-published `world` object.
 * @param world Candidate object; unknown fields fail.
 * @return Normalized owning object on success.
 * @remarks Allowed keys: title, summary, genre, tags, dimensions, perspective,
 *          systems. At least one of title / summary / genre / systems is required.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> parseWorldObject(const Value& world);

/**
 * @brief Replace the runtime world brief published by the live game script.
 * @param world Owning object; validated with parseWorldObject.
 * @thread Game-loop / MCP poll thread only.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> publishWorldContext(Value world);

/** @brief Drop the runtime world brief (soft restart with script reload). */
EVENGINE_API_FOUNDATION void clearWorldContext();

/** @brief Whether a runtime brief is currently published. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool hasWorldContext() noexcept;

/**
 * @brief Current runtime brief, or null when none is published.
 * @ownership Borrowed; valid until the next publish/clear on this thread.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION const Value* runtimeWorldContext() noexcept;

/**
 * @brief Inputs used to assemble one Discover snapshot.
 * @ownership Callers own the pointed-to Values; they are not retained.
 */
struct GameWorldSnapshotInput {
    /** @brief Contract `id` when a Play contract loaded; empty otherwise. */
    std::string contractId;
    /** @brief Whether `game.agent.json` was present (Play Host only; not world authority). */
    bool contractLoaded = false;
    /** @brief Absolute or relative game root reported to agents. */
    std::string gameRoot;
    /** @brief `config.title` when readable from the VM (window / project title). */
    std::string configTitle;
    /** @brief Published gameplay-control domain names (sorted). */
    std::vector<std::string> domains;
    /** @brief `config.modules` (+ optionalModules) when readable from the VM. */
    std::vector<std::string> configuredModules;
    /** @brief Instantiated module slot names when readable from the VM. */
    std::vector<std::string> liveModules;
};

/**
 * @brief Assemble runtime world brief plus live system lists.
 * @param input Snapshot ingredients; pointers are not retained.
 * @return Owning Value with schemaId `evengine.game-world`.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> buildGameWorldSnapshot(const GameWorldSnapshotInput& input);

/**
 * @brief Read `config.title` from the attached VM root.
 * @param vm Live Squirrel VM, or null (returns empty).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string readConfigTitle(HSQUIRRELVM vm);

/**
 * @brief Read `config.modules` / `optionalModules` from the attached VM root.
 * @param vm Live Squirrel VM, or null (returns empty).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::vector<std::string> readConfiguredModules(HSQUIRRELVM vm);

/**
 * @brief List instantiated module slots declared in `eve.moduleList`.
 * @param vm Live Squirrel VM, or null (returns empty).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::vector<std::string> readLiveModules(HSQUIRRELVM vm);

/**
 * @brief Build the Discover snapshot against the live engine.
 * @return Owning snapshot.
 * @thread Game-loop / MCP poll thread only.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> liveGameWorldSnapshot();

}  // namespace eve::dev
