#include "action/ActionNotifyRegistry.h"

#include "action/ActionAudioBlock.h"
#include "action/ActionCameraBlock.h"
#include "action/ActionDamageBlock.h"
#include "action/ActionGameplayEventBlock.h"
#include "action/ActionParameterCurve.h"
#include "action/ActionPrefabBlock.h"
#include "action/ActionStateWindowBlock.h"
#include "action/ActionVfxBlock.h"

#include "common/Capability.h"

#include <algorithm>
#include <map>
#include <set>
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

class ActionCameraCueHandler final : public IActionNotifyHandler {
public:
    Result<void> handle(const ActionTimelineEvent& event, const ActionNotifyContext& context) override {
        if (event.kind != ActionTimelineEventKind::Notify)
            return failure(DiagnosticCode::InvalidArgument, "camera cue must be instantaneous", "event.kind");
        auto binding = ActionCameraCueBinding::fromPayload(event.payload);
        if (!binding) return Result<void>::failure(binding.status());
        std::optional<Result<void>> result;
        cap::forEachUntil<IActionCameraCueSink>([&](IActionCameraCueSink* sink) {
            if (!sink->supports(binding.value().cue)) return false;
            result.emplace(sink->trigger(binding.value(), context));
            return true;
        });
        if (result) return std::move(*result);
        return failure(DiagnosticCode::NotFound, "no camera sink accepts the action cue", binding.value().cue.format());
    }
};

class ActionDamageHandler final : public IActionNotifyHandler {
public:
    Result<void> handle(const ActionTimelineEvent& event, const ActionNotifyContext& context) override {
        if (event.kind != ActionTimelineEventKind::Notify)
            return failure(DiagnosticCode::InvalidArgument, "damage block must be instantaneous", "event.kind");
        auto binding = ActionDamageBinding::fromPayload(event.payload);
        if (!binding) return Result<void>::failure(binding.status());
        if (binding.value().targetIndex >= context.targets.size())
            return failure(DiagnosticCode::NotFound, "damage target index is unavailable", "targetIndex");
        std::optional<Result<void>> result;
        cap::forEachUntil<IActionDamageSink>([&](IActionDamageSink* sink) {
            if (!sink->supports(context.targets[binding.value().targetIndex])) return false;
            result.emplace(sink->apply(binding.value(), context));
            return true;
        });
        if (result) return std::move(*result);
        return failure(DiagnosticCode::NotFound, "no damage sink accepts the action target", "target");
    }
};

class ActionGameplayEventHandler final : public IActionNotifyHandler {
public:
    Result<void> handle(const ActionTimelineEvent& event, const ActionNotifyContext& context) override {
        if (event.kind != ActionTimelineEventKind::Notify)
            return failure(DiagnosticCode::InvalidArgument,
                           "gameplay event block must be instantaneous", "event.kind");
        auto binding = ActionGameplayEventBinding::fromPayload(event.payload);
        if (!binding) return Result<void>::failure(binding.status());
        std::optional<Result<void>> result;
        cap::forEachUntil<IActionGameplayEventSink>([&](IActionGameplayEventSink* sink) {
            result.emplace(sink->emit(binding.value(), context));
            return true;
        });
        if (result) return std::move(*result);
        return failure(DiagnosticCode::NotFound, "no gameplay event sink is registered", binding.value().tag);
    }
};

class ActionStateWindowHandler final : public IActionNotifyHandler {
public:
    Result<void> handle(const ActionTimelineEvent& event, const ActionNotifyContext& context) override {
        if (event.kind != ActionTimelineEventKind::StateEnter && event.kind != ActionTimelineEventKind::StateExit)
            return failure(DiagnosticCode::InvalidArgument, "state window requires enter or exit", "event.kind");
        auto binding = ActionStateWindowBinding::fromPayload(event.type.format(), event.payload);
        if (!binding) return Result<void>::failure(binding.status());
        std::optional<Result<void>> result;
        cap::forEachUntil<IActionStateWindowSink>([&](IActionStateWindowSink* sink) {
            if (!sink->supports(binding.value().kind)) return false;
            result.emplace(event.kind == ActionTimelineEventKind::StateEnter
                               ? sink->enter(binding.value(), event, context)
                               : sink->exit(binding.value(), event, context));
            return true;
        });
        if (result) return std::move(*result);
        return failure(DiagnosticCode::NotFound, "no state-window sink accepts the action block",
                       event.type.format());
    }
};

class ActionParameterCurveHandler final : public IActionNotifyHandler {
public:
    Result<void> handle(const ActionTimelineEvent& event, const ActionNotifyContext& context) override {
        auto binding = ActionParameterCurveBinding::fromPayload(event.payload);
        if (!binding) return Result<void>::failure(binding.status());
        const ActiveKey key{context.executionId, event.itemId.format()};
        if (event.kind == ActionTimelineEventKind::StateEnter)
            return Result<void>::success(Status::success(StatusCode::NoOp));
        if (event.kind != ActionTimelineEventKind::StateExit)
            return failure(DiagnosticCode::InvalidArgument, "parameter curve requires state boundaries", "event.kind");
        const auto found = active_.find(key);
        if (found == active_.end())
            return Result<void>::success(Status::success(StatusCode::NoOp));
        ActionParameterSample sample{ActionParameterPhase::End, context.executionId, event.itemId,
                                     found->second.target, found->second.operation, found->second.value,
                                     context.preview};
        auto applied = dispatch(sample);
        if (!applied) return applied;
        active_.erase(found);
        return applied;
    }

    Result<void> update(const ActionActiveBlock& block, const ActionNotifyContext& context) override {
        auto binding = ActionParameterCurveBinding::fromPayload(block.payload);
        if (!binding) return Result<void>::failure(binding.status());
        if (block.duration <= Duration::zero())
            return failure(DiagnosticCode::InvalidArgument, "parameter curve block duration must be positive",
                           "duration");
        const double progress = std::clamp(block.localTime.seconds() / block.duration.seconds(), 0.0, 1.0);
        const double value    = binding.value().sample(progress);
        const ActiveKey key{context.executionId, block.itemId.format()};
        const auto found = active_.find(key);
        const auto phase = found == active_.end() ? ActionParameterPhase::Begin : ActionParameterPhase::Update;
        ActionParameterSample sample{phase, context.executionId, block.itemId, binding.value().target,
                                     binding.value().operation, value, context.preview};
        auto applied = dispatch(sample);
        if (!applied) return applied;
        active_[key] = ActiveParameter{binding.value().target, binding.value().operation, value};
        return applied;
    }

    Result<void> sample(const ActionActiveBlock& block, const ActionNotifyContext&) const override {
        auto binding = ActionParameterCurveBinding::fromPayload(block.payload);
        if (!binding) return Result<void>::failure(binding.status());
        if (block.duration <= Duration::zero())
            return failure(DiagnosticCode::InvalidArgument, "parameter curve block duration must be positive",
                           "duration");
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }

private:
    using ActiveKey = std::pair<ActionExecutionId, std::string>;
    struct ActiveParameter {
        LogicalId                target;
        ActionParameterOperation operation = ActionParameterOperation::Replace;
        double                   value = 0.0;
    };

    static Result<void> dispatch(const ActionParameterSample& sample) {
        std::optional<Result<void>> result;
        cap::forEachUntil<IActionParameterSink>([&](IActionParameterSink* sink) {
            if (!sink->supports(sample.target)) return false;
            result.emplace(sink->apply(sample));
            return true;
        });
        if (result) return std::move(*result);
        return failure(DiagnosticCode::NotFound,
                       "no parameter sink accepts the action target", sample.target.format());
    }

    std::map<ActiveKey, ActiveParameter> active_;
};

}  // namespace

Result<ActionNotifyRegistry> ActionNotifyRegistry::withBuiltins() {
    ActionNotifyRegistry                      registry;
    const std::vector<ActionNotifyDescriptor> builtins = {
        {"gameplay:event", "Gameplay Event", "Gameplay", ActionNotifyShape::Instant, {"tag"}},
        {"combat:damage", "Apply Damage", "Combat", ActionNotifyShape::Instant, {"damageType", "amount"}},
        {"presentation:vfx", "Spawn VFX", "Presentation", ActionNotifyShape::Instant, {"uri", "lifetimeSeconds"}},
        {"presentation:vfx-state", "VFX State", "Presentation", ActionNotifyShape::State, {"uri"}},
        {"presentation:audio", "Play Audio", "Presentation", ActionNotifyShape::Instant, {"uri"}},
        {"presentation:audio-state", "Audio State", "Presentation", ActionNotifyShape::State, {"uri"}},
        {"gameplay:prefab-spawn", "Spawn Prefab", "Gameplay", ActionNotifyShape::State, {"uri"}},
        {"presentation:camera", "Camera Cue", "Presentation", ActionNotifyShape::Instant, {"cue"}},
        {"combat:hitbox-window", "Hitbox Window", "Combat", ActionNotifyShape::State, {"hitbox"}},
        {"combat:invulnerability-window", "Invulnerability Window", "Combat", ActionNotifyShape::State, {}},
        {"input:combo-window", "Combo Window", "Input", ActionNotifyShape::State, {"input"}},
        {"collision:ignore-window", "Collision Ignore", "Collision", ActionNotifyShape::State, {"channel"}},
        {"movement:root-motion-window", "Root Motion", "Movement", ActionNotifyShape::State, {"mode"}},
        {"presentation:parameter-curve", "Parameter Curve", "Presentation", ActionNotifyShape::State,
         {"target", "keys"}},
    };
    for (auto descriptor : builtins) {
        auto registered = registry.registerDescriptor(std::move(descriptor));
        if (!registered) return Result<ActionNotifyRegistry>::failure(registered.status());
    }
    auto cameraHandler = registry.registerHandler("presentation:camera", std::make_shared<ActionCameraCueHandler>());
    if (!cameraHandler) return Result<ActionNotifyRegistry>::failure(cameraHandler.status());
    auto damageHandler = registry.registerHandler("combat:damage", std::make_shared<ActionDamageHandler>());
    if (!damageHandler) return Result<ActionNotifyRegistry>::failure(damageHandler.status());
    auto gameplayEventHandler =
        registry.registerHandler("gameplay:event", std::make_shared<ActionGameplayEventHandler>());
    if (!gameplayEventHandler) return Result<ActionNotifyRegistry>::failure(gameplayEventHandler.status());
    auto stateWindowHandler = std::make_shared<ActionStateWindowHandler>();
    for (const char* type : {"collision:ignore-window", "combat:hitbox-window",
                             "combat:invulnerability-window", "input:combo-window"}) {
        auto registered = registry.registerHandler(type, stateWindowHandler);
        if (!registered) return Result<ActionNotifyRegistry>::failure(registered.status());
    }
    auto parameterHandler = registry.registerHandler(
        "presentation:parameter-curve", std::make_shared<ActionParameterCurveHandler>());
    if (!parameterHandler) return Result<ActionNotifyRegistry>::failure(parameterHandler.status());
    Result<void> providers = Result<void>::success(Status::success(StatusCode::NoOp));
    cap::forEach<IActionNotifyProvider>([&](IActionNotifyProvider* provider) {
        if (!providers) return;
        providers = provider->install(registry);
    });
    if (!providers) return Result<ActionNotifyRegistry>::failure(providers.status());
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

bool ActionNotifyRegistry::hasHandler(std::string_view type) const noexcept { return handlers_.contains(type); }

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
    if (event.type.format() == "gameplay:prefab-spawn") {
        auto prefab = ActionPrefabSpawnBinding::fromPayload(event.payload);
        if (!prefab) return Result<void>::failure(prefab.status());
    }
    if (event.type.format() == "presentation:audio" || event.type.format() == "presentation:audio-state") {
        const auto shape = event.kind == ActionTimelineEventKind::Notify ? ActionAudioShape::Instant
                                                                        : ActionAudioShape::State;
        auto audio = ActionAudioBinding::fromPayload(event.payload, shape);
        if (!audio) return Result<void>::failure(audio.status());
    }
    if (event.type.format() == "presentation:vfx" || event.type.format() == "presentation:vfx-state") {
        const auto shape = event.kind == ActionTimelineEventKind::Notify ? ActionVfxShape::Instant
                                                                        : ActionVfxShape::State;
        auto vfx = ActionVfxBinding::fromPayload(event.payload, shape);
        if (!vfx) return Result<void>::failure(vfx.status());
    }
    if (event.type.format() == "presentation:parameter-curve") {
        auto curve = ActionParameterCurveBinding::fromPayload(event.payload);
        if (!curve) return Result<void>::failure(curve.status());
    }
    if (event.type.format() == "presentation:camera") {
        auto camera = ActionCameraCueBinding::fromPayload(event.payload);
        if (!camera) return Result<void>::failure(camera.status());
    }
    if (event.type.format() == "combat:damage") {
        auto damage = ActionDamageBinding::fromPayload(event.payload);
        if (!damage) return Result<void>::failure(damage.status());
    }
    if (event.type.format() == "gameplay:event") {
        auto gameplayEvent = ActionGameplayEventBinding::fromPayload(event.payload);
        if (!gameplayEvent) return Result<void>::failure(gameplayEvent.status());
    }
    if (event.type.format() == "collision:ignore-window" || event.type.format() == "combat:hitbox-window" ||
        event.type.format() == "combat:invulnerability-window" || event.type.format() == "input:combo-window") {
        auto window = ActionStateWindowBinding::fromPayload(event.type.format(), event.payload);
        if (!window) return Result<void>::failure(window.status());
    }
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

Result<void> ActionNotifyRegistry::dispatchUpdate(const ActionActiveBlock& block, const ActionNotifyContext& context) {
    ActionTimelineEvent event{ActionTimelineEventKind::StateEnter, block.trackId, block.itemId, block.type,
                              context.time, block.payload};
    auto                valid = validate(event);
    if (!valid) return valid;
    const auto handler = handlers_.find(block.type.format());
    if (handler == handlers_.end())
        return failure(DiagnosticCode::NotFound, "No runtime handler is registered for notify type",
                       block.type.format());
    return handler->second->update(block, context);
}

Result<void> ActionNotifyRegistry::dispatchSample(const ActionActiveBlock& block,
                                                  const ActionNotifyContext& context) const {
    ActionTimelineEvent event{ActionTimelineEventKind::StateEnter, block.trackId, block.itemId, block.type,
                              context.time, block.payload};
    auto                valid = validate(event);
    if (!valid) return valid;
    const auto handler = handlers_.find(block.type.format());
    if (handler == handlers_.end())
        return failure(DiagnosticCode::NotFound, "No preview handler is registered for notify type",
                       block.type.format());
    return handler->second->sample(block, context);
}

Result<void> ActionNotifyRegistry::advanceHandlers(const ActionNotifyContext& context) {
    std::set<IActionNotifyHandler*> advanced;
    StatusCode outcome = StatusCode::NoOp;
    for (const auto& [type, handler] : handlers_) {
        (void)type;
        if (!advanced.insert(handler.get()).second) continue;
        auto result = handler->advance(context);
        if (!result) return result;
        if (result.status().code() == StatusCode::Applied) outcome = StatusCode::Applied;
    }
    return Result<void>::success(Status::success(outcome));
}

}  // namespace eve::action
