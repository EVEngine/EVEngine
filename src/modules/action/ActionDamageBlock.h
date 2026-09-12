#pragma once

/** @file ActionDamageBlock.h @brief Typed instantaneous combat-damage action contract. */

#include "action/ActionNotifyRegistry.h"

#include <cstddef>
#include <string>

namespace eve::action {

/** @brief Renderer- and combat-implementation-neutral knockback impulse. */
struct ActionDamageImpulse {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    auto operator<=>(const ActionDamageImpulse&) const = default;
};

/** @brief Owning validated damage request authored on an action timeline. */
struct ActionDamageBinding {
    /** @brief Gameplay-tag damage classification. */
    std::string damageType;
    /** @brief Non-negative health damage. */
    double amount = 0.0;
    /** @brief Non-negative poise damage. */
    double poiseAmount = 0.0;
    /** @brief Target index into ActionNotifyContext::targets. */
    std::size_t targetIndex = 0;
    /** @brief Optional world-space knockback impulse. */
    ActionDamageImpulse knockback;

    auto operator<=>(const ActionDamageBinding&) const = default;

    /** @brief Decode and validate a damage payload without mutating combat state. */
    [[nodiscard]] static Result<ActionDamageBinding> fromPayload(const Value::Object& payload);
};

/**
 * @brief Optional target-domain adapter for timeline damage.
 *
 * Implementations resolve the generation-qualified ECS target synchronously
 * and own all combat-state mutation. No borrowed binding/context data may be retained.
 */
class IActionDamageSink {
public:
    static constexpr const char* capabilityName = "eve.action.damage-sink";
    virtual ~IActionDamageSink() = default;

    /** @brief Whether this sink can resolve the supplied target handle now. */
    [[nodiscard]] virtual bool supports(ecs::EntityHandle target) const = 0;
    /** @brief Atomically apply one validated damage request. */
    [[nodiscard]] virtual Result<void> apply(const ActionDamageBinding& binding,
                                             const ActionNotifyContext& context) = 0;
};

}  // namespace eve::action
