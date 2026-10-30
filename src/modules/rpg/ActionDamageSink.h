#pragma once

#include "action/ActionDamageBlock.h"
#include "common/Export.h"
#include "common/SubjectRef.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace eve::rpg {

/** @brief Owning identity projection; the host owns the mapping, ECS owns the actor. */
struct RPGDamageTarget {
    ecs::EntityHandle actor;
    SubjectRef        subject;
};

/**
 * @brief Resolve a live action entity to an RPG actor and stable reward identity.
 * @ownership Callback captures remain host-owned and must outlive the sink.
 * @lifetime Returned handles are revalidated on every call, never cached as pointers.
 * @thread Simulation owner thread only.
 * @reentrancy Must not throw, mutate participants, or reenter the sink.
 * @remarks Return nullopt for an unowned entity. Rebuild mappings after restore;
 * removing either entity must invalidate the mapping. Never reuse a subject for a new life.
 */
using RPGDamageResolver = std::function<std::optional<RPGDamageTarget>(ecs::EntityHandle)>;

/** @brief Owning post-commit observation; no actor pointers survive dispatch. */
struct RPGActionDamageReceipt {
    SubjectRef                source;
    SubjectRef                target;
    action::ActionExecutionId execution;
    SimulationTick            tick    = SimulationTick::zero();
    double                    applied = 0.0;
    bool                      killed  = false;
};

/**
 * @brief Scoped action damage provider using RPG Vitals as the sole health authority.
 * @ownership Host owns this nonmovable sink and the resolver's captured mapping.
 * @lifetime Destroy the sink before captured owners; actor destruction is handle-checked.
 * @thread Create, dispatch, poll and destroy on the simulation owner thread.
 * @reentrancy Resolver calls are synchronous without locks and must not reenter.
 * @remarks Registers the existing eve.action.damage-sink capability. Owns no actors,
 * CombatState or persistent data. Supports hp damage; poise/knockback are explicitly
 * Unsupported. The host must select exactly one damage provider per target.
 */
class EVENGINE_API_PLATFORM RPGActionDamageSink final : public action::IActionDamageSink {
public:
    /** @brief Register a provider with an owning callback; an empty callback accepts no targets. */
    explicit RPGActionDamageSink(RPGDamageResolver resolver);
    /** @brief Unregister before releasing transient receipts and the resolver. */
    ~RPGActionDamageSink() override;
    RPGActionDamageSink(const RPGActionDamageSink&)            = delete;
    RPGActionDamageSink& operator=(const RPGActionDamageSink&) = delete;

    /** @brief Query the host mapping and current ECS generation without mutation. */
    [[nodiscard]] bool supports(ecs::EntityHandle target) const override;
    /**
     * @brief Settle one timeline hit against authoritative hp and queue its committed receipt.
     * @return Applied, preview NoOp, or a structured failure without a receipt or health mutation.
     * @ownership Inputs are borrowed only for this call; queued receipts own their identities.
     * @thread Simulation owner thread; no concurrent registry mutation.
     * @reentrancy Does not call scripts; the resolver must obey its non-reentrant contract.
     * @remarks Uses injected tick and no random stream. Hosts decide critical rolls upstream.
     * Only a positive-to-zero transition reports killed; repeated hits on a corpse do not.
     * Delivery deduplication belongs to the timeline producer, not execution ID (multi-hit is valid).
     * @cost One RPG settlement including attribute staging, more expensive than a Vitals read;
     * invoked once per authored hit, never as a per-frame health synchronization.
     */
    [[nodiscard]] Result<void> apply(const action::ActionDamageBinding& binding,
                                     const action::ActionNotifyContext& context) override;
    /** @brief Transfer and clear all owning receipts in commit order; consume once per host step. */
    [[nodiscard]] std::vector<RPGActionDamageReceipt> takeReceipts();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::rpg
