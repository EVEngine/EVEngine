#pragma once

/**
 * @file GameWorldContext.hpp
 * @brief Authored + runtime world brief for AI Discover over MCP / Play Host.
 *
 * After game scripts initialize dynamically configured systems (modules,
 * gameplay domains), agents need a first-class way to learn what kind of world
 * they are looking at. This module owns:
 *
 * - optional `world` objects from `game.agent.json`
 * - a script-published runtime overlay (`eve.dev.publishWorldContext`)
 * - a merged snapshot for `eve_play` status, `eve_game_world`, and `eve://game-world`
 *
 * Narrative identity stays in the world object; live systems (domains / modules)
 * are reported alongside it and are never treated as a second GDD.
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

/** @brief Schema id for the merged game-world snapshot returned to agents. */
inline constexpr std::string_view kGameWorldSchemaId = "evengine.game-world";
/** @brief Schema version for the merged game-world snapshot. */
inline constexpr std::int64_t kGameWorldSchemaVersion = 1;

/**
 * @brief Validate one `world` object (contract or runtime).
 * @param world Candidate object; unknown fields fail.
 * @return Normalized owning object on success.
 * @remarks Allowed keys: title, summary, genre, tags, dimensions, perspective,
 *          systems. At least one of title / summary / genre / systems is required.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> parseWorldObject(const Value& world);

/**
 * @brief Replace the runtime world overlay published by the live game script.
 * @param world Owning object; validated with parseWorldObject.
 * @thread Game-loop / MCP poll thread only.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> publishWorldContext(Value world);

/** @brief Drop the runtime world overlay (soft restart with script reload). */
EVENGINE_API_FOUNDATION void clearWorldContext();

/** @brief Whether a runtime overlay is currently published. */
[[nodiscard]] EVENGINE_API_FOUNDATION bool hasWorldContext() noexcept;

/**
 * @brief Current runtime overlay, or null when none is published.
 * @ownership Borrowed; valid until the next publish/clear on this thread.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION const Value* runtimeWorldContext() noexcept;

/**
 * @brief Inputs used to assemble one Discover snapshot.
 * @ownership Callers own the pointed-to Values; they are not retained.
 */
struct GameWorldSnapshotInput {
    /** @brief Optional `world` object already parsed from `game.agent.json`. */
    const Value* contractWorld = nullptr;
    /** @brief Contract `id` when a contract loaded; empty otherwise. */
    std::string contractId;
    /** @brief Whether `game.agent.json` was present and valid enough to read. */
    bool contractLoaded = false;
    /** @brief Absolute or relative game root reported to agents. */
    std::string gameRoot;
    /** @brief Published gameplay-control domain names (sorted). */
    std::vector<std::string> domains;
    /** @brief `config.modules` (+ optionalModules) when readable from the VM. */
    std::vector<std::string> configuredModules;
    /** @brief Instantiated module slot names when readable from the VM. */
    std::vector<std::string> liveModules;
};

/**
 * @brief Merge contract world, runtime overlay, and live system lists.
 * @param input Snapshot ingredients; pointers are not retained.
 * @return Owning Value with schemaId `evengine.game-world`.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> buildGameWorldSnapshot(const GameWorldSnapshotInput& input);

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
 * @brief Build the Discover snapshot against the live engine / Play contract.
 * @return Owning snapshot, or a diagnostic when the contract world is malformed.
 * @thread Game-loop / MCP poll thread only.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> liveGameWorldSnapshot();

}  // namespace eve::dev
