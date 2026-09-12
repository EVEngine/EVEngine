#pragma once

/** @file ActionDamageSink.h @brief CombatState adapter for timeline damage blocks. */

#include "action/ActionDamageBlock.h"
#include "combat/Damage.h"
#include "common/BorrowedRef.h"

#include <functional>
#include <optional>

namespace eve::combat {

/**
 * @brief Resolves a borrowed mutable CombatState for one live ECS handle.
 * @remarks Called synchronously on the owner thread without an engine lock. The
 * returned reference must remain valid through the current dispatch, repeated
 * resolution of the same handle must be stable during that dispatch, and the
 * callback must not throw or reenter the sink.
 */
using ActionCombatStateResolver = std::function<OptionalRef<CombatState>(ecs::EntityHandle)>;

/**
 * @brief Explicitly registered bridge from Action damage blocks to DamageRuntime.
 *
 * The game owns entity-to-CombatState resolution and both states. This adapter
 * owns only its resolver, deterministic DamageRuntime, and the latest owning outcome.
 * All methods are owner-thread-only; destruction automatically unregisters the sink.
 */
class CombatActionDamageSink final : public action::IActionDamageSink {
public:
    /** @brief Construct with a synchronous resolver copied into the adapter. */
    explicit CombatActionDamageSink(ActionCombatStateResolver resolver);
    ~CombatActionDamageSink() override;

    /** @brief Opt this adapter into or out of Action damage dispatch. */
    void setEnabled(bool enabled);
    /** @brief Return whether this exact adapter is currently registered. */
    [[nodiscard]] bool enabled() const;
    /** @copydoc action::IActionDamageSink::supports */
    [[nodiscard]] bool supports(ecs::EntityHandle target) const override;
    /** @copydoc action::IActionDamageSink::apply */
    [[nodiscard]] Result<void> apply(const action::ActionDamageBinding& binding,
                                     const action::ActionNotifyContext& context) override;
    /** @brief Return an owning copy of the latest committed damage outcome. */
    [[nodiscard]] std::optional<DamageOutcome> lastOutcome() const;

private:
    ActionCombatStateResolver    resolver_;
    DamageRuntime                runtime_;
    std::optional<DamageOutcome> lastOutcome_;
};

}  // namespace eve::combat
