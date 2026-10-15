#pragma once
#include "common/Export.h"


/** @file AbilitySystem.h @brief Grants, cooldowns, instancing, activation groups and gameplay-event triggers. */

#include "action/Action.h"
#include "common/StrongUint64.h"
#include "tags/GameplayTag.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace eve::action {

/** @brief Controls where mutable ability instance state is allocated. */
enum class AbilityInstancingPolicy : std::uint8_t { NonInstanced, PerOwner, PerExecution };

/** @brief Concurrency policy modeled after independent and exclusive ability groups. */
enum class AbilityActivationGroup : std::uint8_t { Independent, ExclusiveReplaceable, ExclusiveBlocking };

/** @brief One gameplay-event trigger declared by an ability definition. */
struct AbilityTrigger {
    std::string            gameplayTag;
    tags::GameplayTagMatch match = tags::GameplayTagMatch::Exact;
};

/** @brief Canonical immutable-by-convention definition of a grantable ability. */
struct EVENGINE_API_PLATFORM AbilityDefinition {
    LogicalId                   id;
    ActionDefinition            action;
    Duration                    cooldown        = Duration::zero();
    AbilityInstancingPolicy     instancing      = AbilityInstancingPolicy::PerOwner;
    AbilityActivationGroup      activationGroup = AbilityActivationGroup::Independent;
    std::vector<AbilityTrigger> triggers;

    /** @brief Validate identity, action, cooldown and gameplay-event trigger invariants. */
    [[nodiscard]] Result<void> validate() const;
};

struct AbilityGrantIdTag;
struct AbilityActivationIdTag;
struct AbilityInstanceIdTag;
/** @brief Opaque identity of one owner-to-definition grant. */
using AbilityGrantId = eve::detail::StrongUint64<AbilityGrantIdTag>;
/** @brief Opaque identity of one activation attempt accepted by ActionRuntime. */
using AbilityActivationId = eve::detail::StrongUint64<AbilityActivationIdTag>;
/** @brief Opaque mutable-instance identity; zero explicitly means NonInstanced. */
using AbilityInstanceId = eve::detail::StrongUint64<AbilityInstanceIdTag>;

/** @brief Owning snapshot of one granted ability's mutable state. */
struct AbilityGrantState {
    AbilityGrantId grantId;
    std::string    ownerId;
    LogicalId      definitionId;
    Duration       cooldownRemaining = Duration::zero();
};

/** @brief Owning link between an ability activation and its action execution. */
struct AbilityActivation {
    AbilityActivationId    id;
    AbilityGrantId         grantId;
    AbilityInstanceId      instanceId;
    ActionExecutionId      executionId;
    std::string            ownerId;
    AbilityActivationGroup group = AbilityActivationGroup::Independent;
};

/** @brief One activation advanced during AbilityRuntime::step. */
struct AbilityStepAdvance {
    AbilityActivationId activationId;
    ActionExecutionId   executionId;
    ActionPhase         phase = ActionPhase::Requested;
    Duration            totalElapsed;
};

/**
 * @brief Owning report from one coordinated ability step.
 *
 * @remarks ActionRuntime remains the sole owner of phase state; this report is a
 *          snapshot for callers (script projection, combat adapters, tests).
 */
struct AbilityStepReport {
    std::vector<AbilityStepAdvance> advances;
    std::size_t                     completedCount = 0;
    std::size_t                     activeCount    = 0;
};

/**
 * @brief Owner-thread coordinator for grant and activation state.
 *
 * Owns definitions, grants, cooldowns and activation links. Borrows ActionRuntime
 * for phase submission/advance/cancel; never owns phase timers itself. Script and
 * combat façades must project into this API rather than re-implementing step or
 * timeline assembly. No method reads a wall clock or invokes scripts.
 */
class EVENGINE_API_PLATFORM AbilityRuntime {
public:
    /** @brief Construct a system borrowing the action runtime used for submissions. */
    explicit AbilityRuntime(ActionRuntime& runtime) : runtime_(runtime) {}

    /** @brief Register one canonical definition; duplicate ids are rejected. */
    [[nodiscard]] Result<void> registerDefinition(AbilityDefinition definition);
    /**
     * @brief Transactionally replace one registered definition for future activations.
     *
     * Existing grants and cooldowns remain valid. Active ActionExecution objects
     * retain the owning definition snapshot captured when they were submitted.
     * A definition with grants cannot change its instancing policy.
     */
    [[nodiscard]] Result<void> replaceDefinition(AbilityDefinition definition);
    /** @brief Grant a registered definition to a non-empty owner identity. */
    [[nodiscard]] Result<AbilityGrantId> grant(std::string ownerId, const LogicalId& definitionId);
    /** @brief Revoke an inactive grant. Active grants must be synchronized/cancelled first. */
    [[nodiscard]] Result<void> revoke(AbilityGrantId grantId);
    /** @brief Return an owning grant-state snapshot. */
    [[nodiscard]] Result<AbilityGrantState> findGrant(AbilityGrantId grantId) const;
    /** @brief Return an owning copy of a registered definition. */
    [[nodiscard]] Result<AbilityDefinition> findDefinition(const LogicalId& definitionId) const;

    /**
     * @brief Submit an activation after cooldown, instancing and group checks.
     * @param grantId Granted ability selected by the caller.
     * @param request Fully owned request matching the definition's action id.
     * @param tick Injected deterministic tick used when replacing an exclusive activation.
     */
    [[nodiscard]] Result<AbilityActivation> activate(AbilityGrantId grantId, ActionRequest request,
                                                     SimulationTick tick);
    /**
     * @brief Activate a grant with a request whose action id and tick come from the definition.
     * @param grantId Granted ability selected by the caller.
     * @param tick Injected deterministic tick used for exclusive replacement and request time.
     * @remarks Builds ActionRequest from the registered definition; callers that need targets
     *          or costs must use the ActionRequest overload instead.
     */
    [[nodiscard]] Result<AbilityActivation> activate(AbilityGrantId grantId, SimulationTick tick);
    /**
     * @brief Cancel one active activation through ActionRuntime, then synchronize links.
     * @param activationId Active activation selected by the caller.
     * @param tick Injected deterministic tick of the cancel.
     */
    [[nodiscard]] Result<void> cancel(AbilityActivationId activationId, SimulationTick tick);
    /**
     * @brief Advance every active execution, cool downs, and drop terminal links.
     * @param tick Injected deterministic tick passed to ActionRuntime::advance.
     * @param delta Injected deterministic duration for phase advance and cooldowns.
     * @return Owning step report; if any advance fails, still synchronizes then returns that failure.
     * @cost Relative to active activation count; amortize per simulation step, not per grant.
     */
    [[nodiscard]] Result<AbilityStepReport> step(SimulationTick tick, Duration delta);
    /** @brief Return grants whose declared gameplay-event triggers match an event tag. */
    [[nodiscard]] std::vector<AbilityGrantId> matchingGrants(std::string_view ownerId,
                                                             std::string_view gameplayEventTag) const;
    /** @brief Decrease every cooldown using injected deterministic time. */
    [[nodiscard]] Result<void> advanceCooldowns(Duration delta);
    /** @brief Remove links whose ActionRuntime execution reached a terminal phase. */
    [[nodiscard]] Result<std::size_t> synchronize();
    /** @brief Return owning active-link snapshots in activation-id order. */
    [[nodiscard]] std::vector<AbilityActivation> activeActivations() const;

private:
    struct GrantRecord {
        AbilityGrantState state;
        AbilityInstanceId ownerInstanceId;
    };

    [[nodiscard]] Result<AbilityGrantId>      nextGrantId();
    [[nodiscard]] Result<AbilityActivationId> nextActivationId();
    [[nodiscard]] Result<AbilityInstanceId>   nextInstanceId();
    [[nodiscard]] bool                        grantHasActiveActivation(AbilityGrantId grantId) const;

    ActionRuntime&                                   runtime_;
    std::map<std::string, AbilityDefinition>         definitions_;
    std::map<AbilityGrantId, GrantRecord>            grants_;
    std::map<AbilityActivationId, AbilityActivation> activations_;
    AbilityGrantId                                   nextGrant_{1};
    AbilityActivationId                              nextActivation_{1};
    AbilityInstanceId                                nextInstance_{1};
};

}  // namespace eve::action
