#pragma once

/** @file ActionNotifyRegistry.h @brief Extensible notify descriptors, validation and runtime routing. */

#include "action/Action.h"
#include "common/AttachmentPoint.h"
#include "common/BorrowedRef.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve::action {

class ActionNotifyRegistry;

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
    /** @brief Execution whose lifecycle is being routed. */
    ActionExecutionId                executionId;
    /** @brief Optional source entity; resolved by consumers at call time. */
    std::optional<ecs::EntityHandle> source;
    /** @brief Owning target handle list for this synchronous dispatch. */
    std::vector<ecs::EntityHandle>   targets;
    /** @brief Optional borrowed animated attachment source corresponding to source. */
    OptionalRef<const IAttachmentPointSource> sourceAttachment;
    /**
     * @brief Borrowed animated attachment sources indexed like targets; empty entries are valid.
     * @remarks References are valid only during synchronous dispatch and are never retained by handlers.
     */
    std::vector<OptionalRef<const IAttachmentPointSource>> targetAttachments;
    /** @brief Authoritative timeline time associated with this dispatch. */
    Duration                         time = Duration::zero();
    /** @brief Finite positive host playback multiplier used by synchronized presentation blocks. */
    double                           playbackRate = 1.0;
    /** @brief Whether the callback is an editor/runtime preview projection. */
    bool                             preview = false;
    /** @brief Whether preview time was positioned discontinuously. */
    bool                             scrubbing = false;
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
    /**
     * @brief Consume one already validated event.
     * @param event Owning boundary projection borrowed only for this call.
     * @param context Owning dispatch context borrowed only for this call.
     * @return Applied/NoOp or a structured consumer failure.
     */
    [[nodiscard]] virtual Result<void> handle(const ActionTimelineEvent& event, const ActionNotifyContext& context) = 0;
    /**
     * @brief Update one active state block after its boundary events were delivered.
     * @param block Owning active-state projection borrowed only for this call.
     * @param context Owning dispatch context borrowed only for this call.
     * @return Applied/NoOp or a structured consumer failure.
     * @remarks The default is an explicit no-op for boundary-only handlers.
     */
    [[nodiscard]] virtual Result<void> update(const ActionActiveBlock&, const ActionNotifyContext&) {
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
    /**
     * @brief Evaluate one preview sample without creating persistent side effects.
     * @param block Owning active-state projection borrowed only for this call.
     * @param context Owning preview context borrowed only for this call.
     * @return Applied/NoOp or a structured preview failure.
     * @remarks Implementations must be side-effect-free and must not retain references.
     */
    [[nodiscard]] virtual Result<void> sample(const ActionActiveBlock&, const ActionNotifyContext&) const {
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
    /**
     * @brief Advance handler-owned transient instances after events and state updates.
     * @param context Current deterministic action time and synchronous bindings.
     * @return Applied/NoOp or a structured maintenance failure.
     * @remarks Called once per unique handler instance per ActionBlockRuntime apply.
     */
    [[nodiscard]] virtual Result<void> advance(const ActionNotifyContext&) {
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
};

/**
 * @brief Optional module provider that installs handlers into a new registry.
 *
 * Providers are discovered through the capability listener registry while
 * `withBuiltins()` is constructing its candidate. The callback is synchronous,
 * owner-thread-only and must not retain the borrowed registry reference.
 */
class IActionNotifyProvider {
public:
    static constexpr const char* capabilityName = "eve.action.notify-provider";
    virtual ~IActionNotifyProvider() = default;

    /**
     * @brief Install provider-owned handler instances for known descriptors.
     * @param registry Borrowed candidate registry valid only for this call.
     * @return Applied/NoOp or a structured installation failure.
     */
    [[nodiscard]] virtual Result<void> install(ActionNotifyRegistry& registry) = 0;
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
    /** @brief Register a unique descriptor after validating its canonical type. @param descriptor Owning contract. */
    [[nodiscard]] Result<void> registerDescriptor(ActionNotifyDescriptor descriptor);
    /**
     * @brief Register an owning handler for an existing descriptor.
     * @param type Registered type.
     * @param handler Shared handler owner.
     */
    [[nodiscard]] Result<void> registerHandler(std::string_view type, std::shared_ptr<IActionNotifyHandler> handler);
    /** @brief Remove a handler while retaining its descriptor for authoring. @param type Registered type. */
    [[nodiscard]] Result<void> unregisterHandler(std::string_view type);
    /** @brief Find an owning descriptor copy. @param type Registered type. */
    [[nodiscard]] Result<ActionNotifyDescriptor> descriptor(std::string_view type) const;
    /** @brief Return descriptors in lexical type order. */
    [[nodiscard]] std::vector<ActionNotifyDescriptor> descriptors() const;
    /** @brief Return whether this registry has a runtime handler for the exact canonical type. */
    [[nodiscard]] bool hasHandler(std::string_view type) const noexcept;
    /** @brief Validate type, shape and required payload fields. @param event Owning event projection. */
    [[nodiscard]] Result<void> validate(const ActionTimelineEvent& event) const;
    /**
     * @brief Validate and route an event, failing observably when no handler is present.
     * @param event Event projection.
     * @param context Dispatch context.
     */
    [[nodiscard]] Result<void> dispatch(const ActionTimelineEvent& event, const ActionNotifyContext& context);
    /**
     * @brief Route one authoritative active-state update to its registered handler.
     * @param block Active block.
     * @param context Dispatch context.
     */
    [[nodiscard]] Result<void> dispatchUpdate(const ActionActiveBlock& block, const ActionNotifyContext& context);
    /**
     * @brief Route one side-effect-free preview sample to its registered handler.
     * @param block Active block.
     * @param context Preview context.
     */
    [[nodiscard]] Result<void> dispatchSample(const ActionActiveBlock& block,
                                              const ActionNotifyContext& context) const;
    /** @brief Advance each unique registered handler once in lexical type order. */
    [[nodiscard]] Result<void> advanceHandlers(const ActionNotifyContext& context);

private:
    std::map<std::string, ActionNotifyDescriptor, std::less<>>                descriptors_;
    std::map<std::string, std::shared_ptr<IActionNotifyHandler>, std::less<>> handlers_;
};

}  // namespace eve::action
