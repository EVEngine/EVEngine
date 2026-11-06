#pragma once
#include "common/Export.h"


/** @file RTSMatch.h @brief Deterministic match lifecycle and victory systems. */

#include "rts/RTSTypes.h"

#include <cstddef>
#include <functional>
#include <string_view>

namespace eve::rts {

/** @brief Reads a resource balance from the game-owned canonical economy account. */
using MatchResourceQuery = std::function<Result<double>(Faction&, std::string_view)>;

/** @brief Match command and simulation boundary. */
class EVENGINE_API_DOMAINS MatchSystem {
public:
    /** @brief Add one live faction before match start. */
    [[nodiscard]] static Result<void> addParticipant(Match& match, Faction& faction, int team);
    /**
     * @brief Register one campaign objective while the match is in setup.
     * @cost Linear in existing objectives for duplicate-id checks.
     */
    [[nodiscard]] static Result<void> addObjective(Match& match, MissionObjectiveSpec objective);
    /** @brief Validate rules/participants/objectives and transition Setup to Running. */
    [[nodiscard]] static Result<void> start(Match& match);
    /** @brief Eliminate a participant and all of its live RTS entities. */
    [[nodiscard]] static Result<void> surrender(Match& match, Faction& faction);
    /**
     * @brief Credit a finite team score while the match is running.
     * @cost Linear in the number of score entries (typically team count).
     */
    [[nodiscard]] static Result<void> creditScore(Match& match, int team, double amount);
    /** @brief Read one team's match-owned score; missing teams return 0. */
    [[nodiscard]] static Result<double> teamScore(Match& match, int team) noexcept;
    /**
     * @brief Advance the match clock, evaluate objectives/victory, and settle a winner or draw.
     * @param step Fixed simulation tick/delta used for elapsed time and control-point accrual.
     * @param resources Required for ResourceTarget races, timed resource settlement, and ReachResource objectives.
     * @cost Linear in participants, objectives, and held control-point buildings when score accrual is enabled.
     */
    [[nodiscard]] static Result<std::size_t> step(Match& match, const SimulationStep& step = {},
                                                  const MatchResourceQuery& resources = {});
    /** @brief Evaluate victory without advancing the match clock. */
    [[nodiscard]] static Result<std::size_t> step(Match& match, const MatchResourceQuery& resources);
};

}  // namespace eve::rts
