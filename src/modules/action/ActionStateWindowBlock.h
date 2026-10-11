#pragma once

/** @file ActionStateWindowBlock.h @brief Typed combat/input/collision action-state window contracts. */

#include "action/ActionNotifyRegistry.h"

#include <optional>
#include <string>

namespace eve::action {

/** @brief Stable semantic kind for one built-in state window. */
enum class ActionStateWindowKind : std::uint8_t { Hitbox, Invulnerability, Combo, CollisionIgnore };

/** @brief Owning validated state-window request authored on an action timeline. */
struct ActionStateWindowBinding {
    ActionStateWindowKind kind = ActionStateWindowKind::Hitbox;
    /** @brief Hitbox, input action, or collision channel identifier; empty for invulnerability. */
    std::string resource;
    /** @brief Optional target index; absence selects ActionNotifyContext::source. */
    std::optional<std::size_t> targetIndex;

    /** @brief Decode one known state-window payload without mutating domain state. */
    [[nodiscard]] static Result<ActionStateWindowBinding> fromPayload(std::string_view type,
                                                                      const Value::Object& payload);
};

/**
 * @brief Optional target-domain adapter for paired action state windows.
 *
 * Implementations own all projected mutable state. Enter and exit are synchronous,
 * owner-thread-only, and may not retain references to the event or context.
 */
class IActionStateWindowSink {
public:
    static constexpr const char* capabilityName = "eve.action.state-window-sink";
    virtual ~IActionStateWindowSink() = default;

    /** @brief Whether this sink owns the supplied semantic window kind. */
    [[nodiscard]] virtual bool supports(ActionStateWindowKind kind) const noexcept = 0;
    /** @brief Enter one validated window or idempotently observe the same active key. */
    [[nodiscard]] virtual Result<void> enter(const ActionStateWindowBinding& binding,
                                             const ActionTimelineEvent& event,
                                             const ActionNotifyContext& context) = 0;
    /** @brief Exit one previously entered window by execution and item identity. */
    [[nodiscard]] virtual Result<void> exit(const ActionStateWindowBinding& binding,
                                            const ActionTimelineEvent& event,
                                            const ActionNotifyContext& context) = 0;
};

}  // namespace eve::action
