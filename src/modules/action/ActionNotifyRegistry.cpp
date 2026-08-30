#include "action/ActionNotifyRegistry.h"

#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

bool validType(std::string_view type) { return LogicalId::parse(type).has_value(); }

}  // namespace

Result<ActionNotifyRegistry> ActionNotifyRegistry::withBuiltins() {
    ActionNotifyRegistry                      registry;
    const std::vector<ActionNotifyDescriptor> builtins = {
        {"gameplay:event", "Gameplay Event", "Gameplay", ActionNotifyShape::Instant, {"tag"}},
        {"combat:damage", "Apply Damage", "Combat", ActionNotifyShape::Instant, {"damageType", "amount"}},
        {"presentation:vfx", "Spawn VFX", "Presentation", ActionNotifyShape::Instant, {"uri"}},
        {"presentation:audio", "Play Audio", "Presentation", ActionNotifyShape::Instant, {"uri"}},
        {"presentation:camera", "Camera Cue", "Presentation", ActionNotifyShape::Instant, {"cue"}},
        {"combat:hitbox-window", "Hitbox Window", "Combat", ActionNotifyShape::State, {"hitbox"}},
        {"combat:invulnerability-window", "Invulnerability Window", "Combat", ActionNotifyShape::State, {}},
        {"input:combo-window", "Combo Window", "Input", ActionNotifyShape::State, {"input"}},
        {"collision:ignore-window", "Collision Ignore", "Collision", ActionNotifyShape::State, {"channel"}},
        {"movement:root-motion-window", "Root Motion", "Movement", ActionNotifyShape::State, {"mode"}},
    };
    for (auto descriptor : builtins) {
        auto registered = registry.registerDescriptor(std::move(descriptor));
        if (!registered) return Result<ActionNotifyRegistry>::failure(registered.status());
    }
    return Result<ActionNotifyRegistry>::success(std::move(registry));
}

Result<void> ActionNotifyRegistry::registerDescriptor(ActionNotifyDescriptor descriptor) {
    if (!validType(descriptor.type)) return failure(DiagnosticCode::InvalidArgument, "Notify type is invalid", "type");
    if (descriptor.displayName.empty())
        return failure(DiagnosticCode::InvalidArgument, "Notify display name is empty", "displayName");
    if (descriptor.category.empty())
        return failure(DiagnosticCode::InvalidArgument, "Notify category is empty", "category");
    for (const auto& field : descriptor.requiredPayloadFields)
        if (field.empty())
            return failure(DiagnosticCode::InvalidArgument, "Required payload field is empty", "requiredPayloadFields");
    const std::string key = descriptor.type;
    if (descriptors_.contains(key))
        return failure(DiagnosticCode::AlreadyExists, "Notify descriptor is already registered", key);
    descriptors_.emplace(key, std::move(descriptor));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionNotifyRegistry::registerHandler(std::string_view                      type,
                                                   std::shared_ptr<IActionNotifyHandler> handler) {
    if (!descriptors_.contains(type))
        return failure(DiagnosticCode::NotFound, "Notify descriptor is not registered", std::string(type));
    if (!handler) return failure(DiagnosticCode::InvalidArgument, "Notify handler is null", "handler");
    if (handlers_.contains(type))
        return failure(DiagnosticCode::AlreadyExists, "Notify handler is already registered", std::string(type));
    handlers_.emplace(std::string(type), std::move(handler));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionNotifyRegistry::unregisterHandler(std::string_view type) {
    const auto found = handlers_.find(type);
    if (found == handlers_.end())
        return failure(DiagnosticCode::NotFound, "Notify handler is not registered", std::string(type));
    handlers_.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<ActionNotifyDescriptor> ActionNotifyRegistry::descriptor(std::string_view type) const {
    const auto found = descriptors_.find(type);
    if (found == descriptors_.end())
        return failure<ActionNotifyDescriptor>(DiagnosticCode::NotFound, "Notify descriptor is not registered",
                                               std::string(type));
    return Result<ActionNotifyDescriptor>::success(found->second);
}

std::vector<ActionNotifyDescriptor> ActionNotifyRegistry::descriptors() const {
    std::vector<ActionNotifyDescriptor> result;
    result.reserve(descriptors_.size());
    for (const auto& entry : descriptors_) result.push_back(entry.second);
    return result;
}

Result<void> ActionNotifyRegistry::validate(const ActionTimelineEvent& event) const {
    auto found = descriptors_.find(event.type.format());
    if (found == descriptors_.end())
        return failure(DiagnosticCode::NotFound, "Timeline event notify type is not registered", event.type.format());
    const bool instant = event.kind == ActionTimelineEventKind::Notify;
    if ((found->second.shape == ActionNotifyShape::Instant) != instant)
        return failure(DiagnosticCode::InvalidArgument, "Timeline event boundary does not match notify shape",
                       event.type.format());
    for (const auto& field : found->second.requiredPayloadFields)
        if (!event.payload.contains(field))
            return failure(DiagnosticCode::InvalidArgument, "Timeline event is missing required payload field", field);
    return Result<void>::success();
}

Result<void> ActionNotifyRegistry::dispatch(const ActionTimelineEvent& event, const ActionNotifyContext& context) {
    auto valid = validate(event);
    if (!valid) return valid;
    const auto handler = handlers_.find(event.type.format());
    if (handler == handlers_.end())
        return failure(DiagnosticCode::NotFound, "No runtime handler is registered for notify type",
                       event.type.format());
    return handler->second->handle(event, context);
}

}  // namespace eve::action
