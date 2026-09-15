#pragma once

/** @file ActionGameplayEventBlock.h @brief Typed instantaneous gameplay-event action contract. */

#include "action/ActionNotifyRegistry.h"

#include <optional>
#include <string>

namespace eve::action {

/** @brief Owning validated event request authored on an action timeline. */
struct ActionGameplayEventBinding {
    /** @brief Canonical gameplay-tag event topic. */
    std::string tag;
    /** @brief Optional target index; absence routes the event to the source subject. */
    std::optional<std::size_t> targetIndex;
    /** @brief Owning schema-neutral domain payload retained by the target event adapter. */
    Value data = Value(Value::Object{});

    /** @brief Decode and validate an event payload without emitting it. */
    [[nodiscard]] static Result<ActionGameplayEventBinding> fromPayload(const Value::Object& payload);
};

/**
 * @brief Optional target-domain adapter for instantaneous timeline gameplay events.
 *
 * Implementations own event ordering, persistence, and subject resolution. Calls
 * are synchronous on the ActionRuntime owner thread and may not retain arguments.
 */
class IActionGameplayEventSink {
public:
    static constexpr const char* capabilityName = "eve.action.gameplay-event-sink";
    virtual ~IActionGameplayEventSink() = default;

    /** @brief Emit one validated event or return a structured domain failure. */
    [[nodiscard]] virtual Result<void> emit(const ActionGameplayEventBinding& binding,
                                            const ActionNotifyContext& context) = 0;
};

}  // namespace eve::action
