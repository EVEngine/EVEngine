#pragma once
#include "common/Export.h"


/** @file Damage.h @brief Deterministic damage, poise, hit-reaction and knockback resolution. */

#include "action/Action.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
#include "settlement/Settlement.h"
#include "settlement/SettlementRules.h"

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
struct EVENGINE_API_BACKENDS CombatState {
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
    /** @brief Target-owned effect multiplier applied in the target-mitigation settlement stage. */
    double                                   incomingDamageMultiplier = 1.0;
    /** @brief Shield balance available to this transaction; the caller commits reported consumption. */
    double                                   availableShield = 0.0;
    Impulse3                                 knockback;

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
    double           appliedHealthDamage = 0.0;
    double           appliedPoiseDamage  = 0.0;
    double           absorbedShieldDamage = 0.0;
    Impulse3         knockback;
    HitReaction      reaction   = HitReaction::None;
    DamageRuleSource ruleSource = DamageRuleSource::Default;
    /** @brief Exact owning request executed by the shared settlement pipeline. */
    settlement::SettlementRequest settlementRequest;
    /** @brief Complete owning settlement audit retained for replay and verification. */
    settlement::SettlementResult settlementResult;
};

/**
 * @brief Owner-thread deterministic damage coordinator.
 *
 * `apply` prepares and validates the complete outcome before one assignment to
 * target state. It uses no clock or RNG. IEEE-754 results are deterministic for
 * identical inputs on supported backends; replay comparison uses exact stored
 * doubles produced by this single resolution path.
 */
class EVENGINE_API_BACKENDS DamageRuntime {
public:
    /** @brief Construct with an optional borrowed rule provider and owning policy copy. */
    explicit DamageRuntime(const IDamageRule* rule = nullptr, HitReactionPolicy policy = {})
        : rule_(rule), policy_(policy) {}

    /**
     * @brief Replace the optional declarative buff/effect rules used by subsequent damage settlements.
     *
     * @param rules Validated owning rule collection; the installed snapshot is copied.
     * @return Applied, or a duplicate/invalid stage diagnostic without changing the previous pipeline.
     * @thread Call on the owning simulation thread while no apply call is active.
     * @reentrancy Does not invoke gameplay callbacks.
     */
    [[nodiscard]] Result<void> configureSettlementRules(const settlement::SettlementRuleSet& rules);

    /** @brief Resolve provider damage amounts before settlement stages, without mutating the target. */
    [[nodiscard]] Result<DamageAmounts> preview(const CombatState& target, const DamageRequest& request) const;
    /**
     * @brief Preview the complete rule, mitigation, shield, clamp and reaction result without mutation.
     * @return The same owning outcome apply would produce while leaving @p target unchanged.
     * @cost Same order as one full damage settlement; use preview() when only provider amounts are needed.
     */
    [[nodiscard]] Result<DamageOutcome> previewSettlement(const CombatState& target,
                                                          const DamageRequest& request) const;
    /** @brief Resolve and atomically commit one damage request. */
    [[nodiscard]] Result<DamageOutcome> apply(CombatState& target, const DamageRequest& request) const;
    /**
     * @brief Resolve and atomically commit health restoration through the configured settlement pipeline.
     * @param target Authoritative combat state to restore.
     * @param source Stable subject responsible for the restoration; may be nil for passive regeneration.
     * @param amount Requested finite non-negative restoration.
     * @return Complete canonical settlement audit, including the clamped applied amount.
     */
    [[nodiscard]] Result<settlement::SettlementResult> heal(CombatState& target, SubjectRef source,
                                                             double amount) const;
    /** @brief Restore an explicit poise amount, clamped to maxPoise. */
    [[nodiscard]] Result<double> recoverPoise(CombatState& target, double amount) const;

private:
    const IDamageRule*             rule_ = nullptr;
    HitReactionPolicy              policy_;
    settlement::SettlementPipeline settlement_;
};

}  // namespace eve::combat
