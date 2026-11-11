#pragma once
#include "common/Export.h"

/**
 * @file RTSMission.h
 * @brief Match-owned campaign trigger host: events, monitors, timers, variables, actions.
 */

#include "rts/RTSTypes.h"

#include <cstddef>
#include <string_view>

namespace eve::rts {

/**
 * @brief Campaign orchestration boundary attached to one Match.
 * @remarks Complements Match::Objectives (win/fail). Does not invent a parallel match root.
 */
class EVENGINE_API_DOMAINS MissionScriptSystem {
public:
    /**
     * @brief Register one trigger definition (Setup or Running).
     * @cost Linear in existing trigger count for duplicate-id checks.
     */
    [[nodiscard]] static Result<void> addTrigger(Match& match, MissionTrigger trigger);
    /**
     * @brief Register one axis-aligned region monitor.
     * @cost Linear in existing monitor count for duplicate-id checks.
     */
    [[nodiscard]] static Result<void> addMonitor(Match& match, MissionRegionMonitor monitor);
    /**
     * @brief Register one countdown timer; optionally start it immediately.
     * @cost Linear in existing timer count for duplicate-id checks.
     */
    [[nodiscard]] static Result<void> addTimer(Match& match, MissionTimer timer);
    /** @brief Create or overwrite one named campaign variable. */
    [[nodiscard]] static Result<void> setVariable(Match& match, std::string id, double value);
    /** @brief Read one named campaign variable; missing ids return 0. */
    [[nodiscard]] static Result<double> variable(Match& match, std::string_view id) noexcept;
    /** @brief Enable or disable one registered trigger by id. */
    [[nodiscard]] static Result<void> setTriggerEnabled(Match& match, std::string_view id, bool enabled);
    /** @brief Enable or disable one registered region monitor by id. */
    [[nodiscard]] static Result<void> setMonitorEnabled(Match& match, std::string_view id, bool enabled);
    /** @brief Start, stop, or reset one timer by id. */
    [[nodiscard]] static Result<void> controlTimer(Match& match, std::string_view id, bool running, bool reset);
    /**
     * @brief Push one custom (or domain) event onto the campaign pending queue.
     * @cost Amortized constant; triggers evaluate during step.
     */
    [[nodiscard]] static Result<void> pushEvent(Match& match, MissionPendingEvent event);
    /**
     * @brief Advance timers/monitors, bridge Match::Events, wake triggers, execute built-in actions.
     * @param tick Injected simulation tick stamped onto Script action log entries.
     * @cost Linear in timers, monitors × live subjects, pending events × triggers, and actions.
     */
    [[nodiscard]] static Result<std::size_t> step(Match& match, const SimulationStep& step);
    /** @brief Clear retained Script action log entries after the game layer has consumed them. */
    [[nodiscard]] static Result<void> clearActionLog(Match& match) noexcept;

    /** @brief Parse one trigger definition from an `eve.rts.mission.trigger` JSON object. */
    [[nodiscard]] static Result<MissionTrigger> parseTriggerJson(std::string_view json);
    /** @brief Parse one region monitor from an `eve.rts.mission.monitor` JSON object. */
    [[nodiscard]] static Result<MissionRegionMonitor> parseMonitorJson(std::string_view json);
};

}  // namespace eve::rts
