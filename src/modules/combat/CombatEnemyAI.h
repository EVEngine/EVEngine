#pragma once
#include "common/Export.h"

/** @file CombatEnemyAI.h @brief Minimal ring-based combat enemy intent source. */

#include "action/AbilityController.h"
#include "combat/CombatTarget.h"
#include "common/Identity.h"
#include "common/SubjectRef.h"

#include <cstdint>
#include <map>
#include <string>

namespace eve::combat {

/** @brief Distance band used by the simple enemy brain. */
enum class CombatEnemyBand : std::uint8_t { Far, Mid, Near };

/** @brief Owning configuration for one enemy fighter. */
struct CombatEnemyDefinition {
    SubjectRef             subject;
    std::string            ownerId;
    action::AbilityGrantId lightGrant{};
    double                 nearRadius         = 1.8;
    double                 midRadius          = 4.0;
    std::uint64_t          attackCooldownTicks = 30;

    /** @brief Validate subject, owner and positive radii. */
    [[nodiscard]] Result<void> validate() const;
};

/**
 * @brief Deterministic enemy intent source for arena vertical slices.
 *
 * When the locked target is inside the near band and the cooldown elapsed, it
 * emits one light-attack intent. Otherwise it emits nothing and the game moves
 * the enemy via CombatCharacterRuntime / CombatLocomotionRuntime.
 */
class EVENGINE_API_BACKENDS CombatEnemyIntentSource final : public action::IAbilityIntentSource {
public:
    /** @brief Register or replace one enemy definition after validation. */
    [[nodiscard]] Result<void> registerEnemy(CombatEnemyDefinition definition);
    /** @brief Remove one enemy. */
    [[nodiscard]] Result<void> unregisterEnemy(SubjectRef subject);
    /** @brief Borrow lock-on state so the enemy attacks its current target. */
    void setTargetRuntime(CombatTargetRuntime& targets) noexcept { targets_ = &targets; }
    void clearTargetRuntime() noexcept { targets_ = nullptr; }
    /** @brief Provide world positions for band checks (enemies and their targets). */
    [[nodiscard]] Result<void> setPosition(SubjectRef subject, double x, double y, double z);
    /** @brief Set the action id copied into emitted intents. */
    [[nodiscard]] Result<void> setLightAction(SubjectRef subject, LogicalId actionId);
    /** @brief Emit at most one attack intent for the supplied tick. */
    [[nodiscard]] Result<std::optional<action::AbilityIntent>> nextIntent(SimulationTick tick) override;
    /** @brief Current band for debugging; Far when unknown. */
    [[nodiscard]] CombatEnemyBand band(SubjectRef subject) const;

private:
    struct Pose {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };
    struct EnemyState {
        CombatEnemyDefinition definition;
        LogicalId             lightAction;
        bool                  hasAction = false;
        std::uint64_t         lastAttackTick = 0;
    };

    std::map<std::string, EnemyState, std::less<>> enemies_;
    std::map<std::string, Pose, std::less<>>       poses_;
    CombatTargetRuntime*                           targets_ = nullptr;
};

}  // namespace eve::combat
