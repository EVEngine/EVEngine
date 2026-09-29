#include "action/ActionAttackVfxBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include "stylize/action/StylizeAction.h"
#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eve::stylize_action {
namespace {

eve::stylize::AttackVfxRuntime* moduleRuntime() {
    auto* module = eve::ModuleManager::getInstance<StylizeAction>("StylizeAction");
    return module ? &module->runtime() : nullptr;
}

eve::Result<eve::LogicalId> ensureRecipe(eve::stylize::AttackVfxRuntime& runtime,
                                         const eve::action::ActionAttackVfxBinding& binding) {
    if (binding.recipeId.isValid()) {
        if (!runtime.findRecipe(binding.recipeId))
            return eve::Result<eve::LogicalId>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "AttackVfx recipeId is not registered", "recipeId"));
        return eve::Result<eve::LogicalId>::success(binding.recipeId);
    }

    std::string json;
    if (binding.uri.rfind("json:", 0) == 0) {
        json = binding.uri.substr(5);
    } else {
        auto* fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
        if (!fs)
            return eve::Result<eve::LogicalId>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "Filesystem is required to load AttackVfx uri", "uri"));
        json = fs->readText(binding.uri);
        if (json.empty())
            return eve::Result<eve::LogicalId>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "AttackVfx recipe uri could not be read", "uri"));
    }
    auto parsed = eve::stylize::AttackVfxRecipe::fromJson(json);
    if (!parsed) return eve::Result<eve::LogicalId>::failure(parsed.status());
    auto recipe = std::move(parsed).takeValue();
    const eve::LogicalId id = recipe.id;
    if (!runtime.findRecipe(id)) {
        auto registered = runtime.registerRecipe(recipe);
        if (!registered) return eve::Result<eve::LogicalId>::failure(registered.status());
    }
    return eve::Result<eve::LogicalId>::success(id);
}

eve::stylize::AttackVfxRequest makeRequest(const eve::action::ActionAttackVfxBinding& binding,
                                           const eve::action::ActionNotifyContext& context) {
    eve::stylize::AttackVfxRequest request;
    request.skinOverride = binding.skinId;
    if (context.source) {
        request.sourceId         = context.source->id;
        request.sourceGeneration = context.source->generation;
    }
    if (!context.targets.empty()) {
        const auto index =
            binding.spatial.target == eve::action::ActionSpatialTarget::Target ? binding.spatial.targetIndex : 0;
        if (index < context.targets.size()) {
            request.targetId         = context.targets[index].id;
            request.targetGeneration = context.targets[index].generation;
        }
    }
    return request;
}

class ActionAttackVfxHandler final : public eve::action::IActionNotifyHandler {
public:
    eve::Result<void> handle(const eve::action::ActionTimelineEvent& event,
                             const eve::action::ActionNotifyContext& context) override {
        auto* runtime = moduleRuntime();
        if (!runtime)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "StylizeAction module is unavailable", "stylizeAction"));

        const ActiveKey key{context.executionId, event.itemId.format()};
        if (event.kind == eve::action::ActionTimelineEventKind::StateExit) {
            const auto found = active_.find(key);
            if (found == active_.end())
                return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
            auto stopped = runtime->stop(found->second.handle, eve::stylize::AttackVfxStopMode::StopEmitting);
            active_.erase(found);
            if (!stopped) return eve::Result<void>::failure(stopped.status());
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }

        const bool instant = event.kind == eve::action::ActionTimelineEventKind::Notify;
        if (!instant && event.kind != eve::action::ActionTimelineEventKind::StateEnter)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "AttackVfx state handler requires enter or exit boundary", "event.kind"));
        if (!instant && active_.contains(key))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "AttackVfx state is already active", "itemId"));

        const auto shape =
            instant ? eve::action::ActionAttackVfxShape::Instant : eve::action::ActionAttackVfxShape::State;
        auto binding = eve::action::ActionAttackVfxBinding::fromPayload(event.payload, shape);
        if (!binding) return eve::Result<void>::failure(binding.status());

        auto recipeId = ensureRecipe(*runtime, binding.value());
        if (!recipeId) return eve::Result<void>::failure(recipeId.status());

        auto request = makeRequest(binding.value(), context);
        auto played  = runtime->play(recipeId.value(), request);
        if (!played) return eve::Result<void>::failure(played.status());

        ActiveInstance owned;
        owned.handle       = std::move(played).takeValue();
        owned.binding      = std::move(binding).takeValue();
        owned.startTime    = context.time;
        owned.executionId  = context.executionId;
        owned.nextCue      = 0;
        owned.lastAge      = 0.0;

        if (instant) {
            auto duration = eve::Duration::fromSeconds(owned.binding.lifetimeSeconds);
            if (!duration) return eve::Result<void>::failure(duration.status());
            auto end = context.time.tryAdd(duration.value());
            if (!end) return eve::Result<void>::failure(end.status());
            owned.endTime = std::move(end).takeValue();
            transients_.push_back(std::move(owned));
        } else {
            active_.emplace(key, std::move(owned));
        }
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> update(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext& context) override {
        auto* runtime = moduleRuntime();
        if (!runtime)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "StylizeAction module is unavailable", "stylizeAction"));
        const auto found = active_.find({context.executionId, block.itemId.format()});
        if (found == active_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "Active AttackVfx state has no instance", "itemId"));
        return deliverCues(*runtime, found->second, context.time);
    }

    eve::Result<void> advance(const eve::action::ActionNotifyContext& context) override {
        auto* runtime = moduleRuntime();
        if (!runtime)
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));

        bool changed = false;
        const double now = context.time.seconds();
        double dt = now - lastAdvanceSeconds_;
        if (context.scrubbing || dt < 0.0) dt = 0.0;
        lastAdvanceSeconds_ = now;
        if (dt > 0.0) {
            auto frame = runtime->advance(dt);
            if (!frame) return eve::Result<void>::failure(frame.status());
            changed = !frame.value().advanced.empty() || !frame.value().stopped.empty() ||
                      !frame.value().events.empty();
        }

        for (auto& active : active_) {
            auto cues = deliverCues(*runtime, active.second, context.time);
            if (!cues) return cues;
            changed = changed || cues.code() == eve::StatusCode::Applied;
        }

        for (auto it = transients_.begin(); it != transients_.end();) {
            if (it->executionId != context.executionId) {
                ++it;
                continue;
            }
            auto cues = deliverCues(*runtime, *it, context.time);
            if (!cues) return cues;
            if (context.time < it->endTime) {
                ++it;
                continue;
            }
            auto stopped = runtime->stop(it->handle, eve::stylize::AttackVfxStopMode::ClearImmediately);
            if (!stopped) return eve::Result<void>::failure(stopped.status());
            it = transients_.erase(it);
            changed = true;
        }

        return eve::Result<void>::success(
            eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
    }

private:
    using ActiveKey = std::pair<eve::action::ActionExecutionId, std::string>;

    struct ActiveInstance {
        eve::stylize::AttackVfxHandle      handle{};
        eve::action::ActionAttackVfxBinding binding;
        eve::Duration                      startTime = eve::Duration::zero();
        eve::Duration                      endTime   = eve::Duration::zero();
        eve::action::ActionExecutionId     executionId{};
        std::size_t                        nextCue = 0;
        double                             lastAge = 0.0;
    };

    static eve::Result<void> deliverCues(eve::stylize::AttackVfxRuntime& runtime, ActiveInstance& active,
                                         eve::Duration now) {
        const double age =
            static_cast<double>(now.nanoseconds() - active.startTime.nanoseconds()) / 1000000000.0;
        bool changed = false;
        while (active.nextCue < active.binding.cues.size()) {
            const auto& cue = active.binding.cues[active.nextCue];
            if (cue.offsetSeconds > age) break;
            if (cue.offsetSeconds + 1e-9 >= active.lastAge) {
                auto signaled = runtime.signal(active.handle, cue.cue);
                if (!signaled) return eve::Result<void>::failure(signaled.status());
                changed = true;
            }
            ++active.nextCue;
        }
        active.lastAge = age;
        return eve::Result<void>::success(
            eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
    }

    std::map<ActiveKey, ActiveInstance> active_;
    std::vector<ActiveInstance>         transients_;
    double                              lastAdvanceSeconds_ = 0.0;
};

class ActionAttackVfxProvider final : public eve::action::IActionNotifyProvider {
public:
    eve::Result<void> install(eve::action::ActionNotifyRegistry& registry) override {
        auto handler = std::make_shared<ActionAttackVfxHandler>();
        auto instant = registry.registerHandler("presentation:attack-vfx", handler);
        if (!instant) return instant;
        return registry.registerHandler("presentation:attack-vfx-state", std::move(handler));
    }
};

ActionAttackVfxProvider& provider() {
    static ActionAttackVfxProvider instance;
    return instance;
}

bool gProviderRegistered = false;

}  // namespace

void registerActionAttackVfxProvider() {
    if (gProviderRegistered) return;
    eve::cap::addListener<eve::action::IActionNotifyProvider>(&provider());
    gProviderRegistered = true;
}

void unregisterActionAttackVfxProvider() {
    if (!gProviderRegistered) return;
    eve::cap::removeListener<eve::action::IActionNotifyProvider>(&provider());
    gProviderRegistered = false;
}

}  // namespace eve::stylize_action
