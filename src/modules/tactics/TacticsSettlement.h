#pragma once

/** @file TacticsSettlement.h @brief Shared settlement entry for declared tactical abilities. */

#include "settlement/Settlement.h"
#include "settlement/SettlementRules.h"
#include "tactics/TacticsBattle.h"

#include <span>

namespace eve::tactics {

/** @brief Owning values required to settle one previously declared tactical ability. */
struct AbilitySettlementRequest {
    AbilityReceipt            ability;
    /** @brief Optional stable board/cell subject; targetUnit remains the default when valid. */
    SubjectRef                target;
    std::string               kind;
    double                    magnitude = 0.0;
    std::vector<std::string>  tags;
    SimulationTick            tick = SimulationTick::zero();
    Value                     context;
};

/**
 * @brief Validate and project a committed tactical ability into the canonical settlement request.
 * @param request Owning tactical receipt and settlement values.
 * @return The exact request consumed by TacticsSettlementRuntime, or a located validation failure.
 * @thread Thread-safe for independent request values; reads no module state.
 * @reentrancy Does not invoke callbacks.
 * @cost Linear in tag and context size due to owning projection.
 */
[[nodiscard]] Result<settlement::SettlementRequest> makeSettlementRequest(const AbilitySettlementRequest& request);

/**
 * @brief Owner-thread settlement bridge between tactics declarations and game-owned combat state.
 *
 * Tactics continues to own action legality and resources. The supplied policy owns health,
 * status, board-object, or other effect state; this bridge only supplies the common ordered,
 * explainable and transactional settlement protocol.
 */
class TacticsSettlementRuntime final {
public:
    /**
     * @brief Replace declarative rules used by subsequent tactical ability settlements.
     * @return Applied, or a structured failure while retaining the previous rule set.
     * @thread Call on the battle's owning simulation thread outside settle.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] Result<void> configureSettlementRules(const settlement::SettlementRuleSet& rules);

    /**
     * @brief Settle one committed ability receipt against game-owned state.
     * @param request Owning receipt, magnitude, tags, context and deterministic tick.
     * @param policy Synchronous borrowed policy; it is not retained after the call.
     * @param eventLog Optional event owner participating in the atomic commit.
     * @return Complete settlement audit, or failure with policy state rolled back.
     * @thread Call on the owning simulation thread.
     * @reentrancy The policy must not re-enter this runtime.
     */
    [[nodiscard]] Result<settlement::SettlementResult> settle(
        const AbilitySettlementRequest& request, settlement::ISettlementPolicy& policy,
        game_event::GameEventLog* eventLog = nullptr) const;

    /**
     * @brief Settle an ordered tactical area effect as one atomic transaction.
     * @param requests Owning unit or cell-target requests in deterministic area order.
     * @param policies Borrowed state owners matching requests one-for-one.
     * @param eventLog Optional event owner rolled back with every domain mutation on failure.
     * @return One canonical result per request, or failure with all domain state restored.
     * @thread Call on every involved state's common owning simulation thread.
     * @reentrancy Policies and the event log must not re-enter this runtime.
     * @cost Linear in request count plus one full settlement per area target.
     */
    [[nodiscard]] Result<std::vector<settlement::SettlementResult>> settleAtomic(
        std::span<const AbilitySettlementRequest> requests, std::span<settlement::ISettlementPolicy*> policies,
        game_event::GameEventLog* eventLog = nullptr) const;

private:
    settlement::SettlementPipeline settlement_;
};

}  // namespace eve::tactics
