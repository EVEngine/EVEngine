#pragma once
#include <memory>
#include <string_view>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::rpg {
class RPGActor;
class GameState;
class Tracker;
/**
 * @brief Two-participant action encounter with generation-safe links and retryable kill delivery.
 * @ownership Host owns this coordinator; ECS owns actors, GameState owns reward facts.
 * @lifetime Retains handles and identity values only. Destroying either actor invalidates attacks.
 * Recreate and bind after restore; no transient combat state is serialized.
 * @thread Simulation owner thread only.
 * @reentrancy No scripts or unknown callbacks are invoked by this coordinator.
 */
class EVENGINE_API_PLATFORM ActionEncounter final {
public:
    /** @brief Create an unbound encounter and scoped action damage provider. */
    ActionEncounter();
    /** @brief Unregister the provider before destroying its link mapping. */
    ~ActionEncounter();
    /**
     * @brief Abandon this transient encounter and unregister immediately before restart/load.
     * @remarks Explicit lifecycle end; discards pending delivery intentionally. For continuation,
     * claim first. Rebind creates a fresh provider. Idempotent, no participant mutation.
     */
    void close() noexcept;
    ActionEncounter(const ActionEncounter&)            = delete;
    ActionEncounter& operator=(const ActionEncounter&) = delete;
    /**
     * @brief Bind borrowed actors to stable per-life UUID strings.
     * @return Failure for invalid links or outstanding reward; success replaces both links atomically.
     * @remarks Never reuse an enemy ID for a new life. Both destruction orders are safe.
     */
    [[nodiscard]] Result<void> bind(RPGActor& player, RPGActor& enemy, std::string_view playerId,
                                    std::string_view enemyId);
    /**
     * @brief Dispatch a physical hit through the action registry and authoritative RPG Vitals.
     * @return Actual applied damage or failure without HP mutation.
     * @remarks Host owns range, cooldown and windup admission. Tick is injected and nondecreasing;
     * no random stream is used. Pending player kill receipts block further hits until claimed.
     * @cost One settlement transaction per admitted hit, substantially more than reading HP.
     */
    [[nodiscard]] Result<double> hit(bool playerAttacks, double amount, int tick);
    /**
     * @brief Deliver the pending player kill to the canonical reward transaction.
     * @return Applied or duplicate NoOp; failures retain the receipt for retry.
     * @ownership State and tracker are borrowed for this call only.
     * @cost Copies quest and game state once per kill through BattleVictory.
     */
    [[nodiscard]] Result<void> claim(GameState& state, Tracker& tracker, std::string_view encounterId);
    /** @brief True while a committed death still needs reward delivery; saving is unsafe then. */
    [[nodiscard]] bool hasPendingReward() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rpg
