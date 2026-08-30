#pragma once

/** @file ActionNotifyRegistry.h @brief Extensible notify descriptors, validation and runtime routing. */

#include "action/Action.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve::action {

/** @brief Declares whether a notify is instantaneous or an enter/exit state. */
enum class ActionNotifyShape : std::uint8_t { Instant, State };

/** @brief Owning editor/runtime contract for one notify type. */
struct ActionNotifyDescriptor {
    std::string              type;
    std::string              displayName;
    std::string              category;
    ActionNotifyShape        shape = ActionNotifyShape::Instant;
    std::vector<std::string> requiredPayloadFields;
};

/** @brief Context linking a routed event to its action execution. */
struct ActionNotifyContext {
    ActionExecutionId                executionId;
    std::optional<ecs::EntityHandle> source;
    std::vector<ecs::EntityHandle>   targets;
};

/**
 * @brief Consumer-owned handler for one registered notify type.
 *
 * Handlers are invoked synchronously without locks. They must not retain event
 * or context references; both are borrowed only for the call.
 */
class IActionNotifyHandler {
public:
    virtual ~IActionNotifyHandler() = default;
    /** @brief Consume one already validated event. */
    [[nodiscard]] virtual Result<void> handle(const ActionTimelineEvent& event, const ActionNotifyContext& context) = 0;
};

/**
 * @brief Canonical notify descriptor owner and runtime handler router.
 *
 * Descriptor queries return owning copies. Handlers are shared-owned by the
 * registry so plugin unload cannot leave a raw pointer. All methods are
 * owner-thread-only; unregister before unloading handler code.
 */
class ActionNotifyRegistry {
public:
    /** @brief Build a registry containing the engine's standard semantic notify types. */
    [[nodiscard]] static Result<ActionNotifyRegistry> withBuiltins();
    /** @brief Register a unique descriptor after validating its canonical type. */
    [[nodiscard]] Result<void> registerDescriptor(ActionNotifyDescriptor descriptor);
    /** @brief Register an owning handler for an existing descriptor. */
    [[nodiscard]] Result<void> registerHandler(std::string_view type, std::shared_ptr<IActionNotifyHandler> handler);
    /** @brief Remove a handler while retaining its descriptor for authoring. */
    [[nodiscard]] Result<void> unregisterHandler(std::string_view type);
    /** @brief Find an owning descriptor copy. */
    [[nodiscard]] Result<ActionNotifyDescriptor> descriptor(std::string_view type) const;
    /** @brief Return descriptors in lexical type order. */
    [[nodiscard]] std::vector<ActionNotifyDescriptor> descriptors() const;
    /** @brief Validate type, shape and required payload fields. */
    [[nodiscard]] Result<void> validate(const ActionTimelineEvent& event) const;
    /** @brief Validate and route an event, failing observably when no handler is present. */
    [[nodiscard]] Result<void> dispatch(const ActionTimelineEvent& event, const ActionNotifyContext& context);

private:
    std::map<std::string, ActionNotifyDescriptor, std::less<>>                descriptors_;
    std::map<std::string, std::shared_ptr<IActionNotifyHandler>, std::less<>> handlers_;
};

}  // namespace eve::action
