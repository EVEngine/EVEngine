#pragma once

/** @file Damage.h @brief Deterministic damage, poise, hit-reaction and knockback resolution. */

#include "action/Action.h"
#include "common/BorrowedRef.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
#include "settlement/Settlement.h"

#include <optional>
#include <string>

namespace eve::combat {

/** @brief Renderer/physics-neutral knockback impulse authored in world units. */
struct Impulse3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/** @brief Typed reaction selected after health and poise are resolved. */
enum class HitReaction : std::uint8_t { None, Flinch, Stagger, Knockdown, Death };

/** @brief Indicates whether default amounts or an injected rule produced the outcome. */
enum class DamageRuleSource : std::uint8_t { Default, Provider };

/**
 * @brief Mutable combat state owned by the gameplay domain for one subject.
 *
 * This is a focused component-like value, not an actor inheritance root. The
 * owning domain resolves SubjectRef lifetime and persists/restores this value.
 */
struct CombatState {
    SubjectRef subject;
    double     health    = 0.0;
    double     maxHealth = 0.0;
    double     poise     = 0.0;
    double     maxPoise  = 0.0;

    /** @brief Validate finite ranges and the non-nil subject link. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning input for one damage transaction. */
struct DamageRequest {
    SubjectRef                               source;
    SubjectRef                               target;
    std::optional<action::ActionExecutionId> actionExecution;
    std::string                              damageType;
    double                                   healthDamage = 0.0;
    double                                   poiseDamage  = 0.0;
    Impulse3                                 knockback;
    /** @brief Injected simulation tick copied into the canonical settlement event. */
    SimulationTick tick = SimulationTick::zero();
    /** @brief Presentation metadata; the requested amount already includes any critical multiplier. */
    bool critical = false;

    /** @brief Validate target, canonical gameplay-tag damage type and finite amounts. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Provider result prepared before the authoritative state is mutated. */
struct DamageAmounts {
    double healthDamage   = 0.0;
    double poiseDamage    = 0.0;
    double knockbackScale = 1.0;
};

/**
 * @brief Optional game-owned mitigation/amplification policy.
 *
 * Implementations are called synchronously without locks and must not retain
 * request/state references. The provider must outlive DamageRuntime.
 */
class IDamageRule {
public:
    virtual ~IDamageRule() = default;
    /** @brief Resolve final non-negative amounts without mutating target state. */
    [[nodiscard]] virtual Result<DamageAmounts> evaluate(const DamageRequest& request,
                                                         const CombatState&   target) const = 0;
};

/** @brief Threshold policy for deterministic reaction selection. */
struct HitReactionPolicy {
    double flinchDamageThreshold = 0.0;
    double staggerPoiseFraction  = 0.5;

    /** @brief Validate finite non-negative thresholds and fraction in [0,1]. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Complete owning audit result of one committed damage transaction. */
struct DamageOutcome {
    SubjectRef       source;
    SubjectRef       target;
    double           previousHealth      = 0.0;
    double           health              = 0.0;
    double           previousPoise       = 0.0;
    double           poise               = 0.0;
    /** @brief Compatibility projection of settlement.applied; never used as mutable authority. */
    double           appliedHealthDamage = 0.0;
    double           appliedPoiseDamage  = 0.0;
    Impulse3         knockback;
    HitReaction      reaction   = HitReaction::None;
    DamageRuleSource ruleSource = DamageRuleSource::Default;
    /** @brief Owning canonical result including stages, actual damage and result event. */
    settlement::SettlementResult settlement;
};

/**
 * @brief Owner-thread deterministic damage coordinator.
 *
 * `apply` uses the common settlement pipeline to prepare and commit one assignment to
 * target state. It uses no clock or RNG. IEEE-754 results are deterministic for
 * identical inputs on supported backends; replay comparison uses exact stored
 * doubles produced by this single resolution path.
 */
class DamageRuntime {
public:
    /** @brief Construct with an optional borrowed rule provider and owning policy copy. */
    explicit DamageRuntime(const IDamageRule* rule = nullptr, HitReactionPolicy policy = {})
        : rule_(rule), policy_(policy) {}

    /** @brief Run canonical settlement on an isolated copy and return actual, clamped damage for tactical prediction.
     */
    [[nodiscard]] Result<DamageAmounts> preview(const CombatState& target, const DamageRequest& request) const;
    /** @brief Resolve and atomically commit one damage request. */
    [[nodiscard]] Result<DamageOutcome> apply(CombatState& target, const DamageRequest& request) const;
    /**
     * @brief Run an explicitly configured common pipeline and optionally append its result event.
     * @param target Borrowed authority; valid for this synchronous owner-thread call.
     * @param request Borrowed immutable input; never retained.
     * @param pipeline Borrowed pipeline; custom stages must not mutate target or reenter this runtime.
     * @param events Optional borrowed log; event failure rolls back health and poise together.
     * @return Owning outcome, or failure with target and log unchanged. No references escape.
     */
    [[nodiscard]] Result<DamageOutcome> apply(CombatState& target, const DamageRequest& request,
                                              const settlement::SettlementPipeline& pipeline,
                                              OptionalRef<game_event::GameEventLog> events = {}) const;
    /** @brief Restore an explicit poise amount, clamped to maxPoise. */
    [[nodiscard]] Result<double> recoverPoise(CombatState& target, double amount) const;

private:
    const IDamageRule* rule_ = nullptr;
    HitReactionPolicy  policy_;
};

}  // namespace eve::combat
