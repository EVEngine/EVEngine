#pragma once

/** @file RTSMatch.h @brief Deterministic match lifecycle and victory systems. */

#include "rts/RTSTypes.h"

#include <cstddef>
#include <functional>
#include <string_view>

namespace eve::rts {

/** @brief Reads a resource balance from the game-owned canonical economy account. */
using MatchResourceQuery = std::function<Result<double>(Faction&, std::string_view)>;

/** @brief Match command and simulation boundary. */
class MatchSystem {
public:
    /** @brief Add one live faction before match start. */
    [[nodiscard]] static Result<void> addParticipant(Match& match, Faction& faction, int team);
    /** @brief Validate rules/participants and transition Setup to Running. */
    [[nodiscard]] static Result<void> start(Match& match);
    /** @brief Eliminate a participant and all of its live RTS entities. */
    [[nodiscard]] static Result<void> surrender(Match& match, Faction& faction);
    /** @brief Evaluate the configured victory rule and settle a winner or draw. */
    [[nodiscard]] static Result<std::size_t> step(Match& match, const MatchResourceQuery& resources = {});
};

}  // namespace eve::rts
