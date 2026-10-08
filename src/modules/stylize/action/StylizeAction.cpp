#include "stylize/action/StylizeAction.h"

#include "common/SquirrelBinding.h"
#include "common/Value.h"
#include "stylize/AttackVfxRecipe.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace eve::stylize_action {

void registerCameraAttackVfxExecutor();
void unregisterCameraAttackVfxExecutor();
void registerAudioAttackVfxExecutor();
void unregisterAudioAttackVfxExecutor();
void registerPrefabAttackVfxExecutor();
void unregisterPrefabAttackVfxExecutor();
void registerActionAttackVfxProvider();
void unregisterActionAttackVfxProvider();

namespace {

eve::stylize::AttackVfxHandle makeHandle(std::uint32_t slot, std::uint32_t generation) {
    return eve::stylize::AttackVfxHandle{slot, generation};
}

eve::Value projectHandle(const eve::stylize::AttackVfxHandle& handle) {
    return eve::Value::Object{{"slot", static_cast<std::int64_t>(handle.slot)},
                              {"generation", static_cast<std::int64_t>(handle.generation)}};
}

eve::Value projectFrame(eve::stylize::AttackVfxFrame frame) {
    eve::Value::Array events;
    events.reserve(frame.events.size());
    for (const auto& event : frame.events) {
        std::string kind = "cueIgnored";
        switch (event.kind) {
            case eve::stylize::AttackVfxFrameEvent::Kind::PhaseEnter: kind = "phaseEnter"; break;
            case eve::stylize::AttackVfxFrameEvent::Kind::PhaseExit: kind = "phaseExit"; break;
            case eve::stylize::AttackVfxFrameEvent::Kind::InstanceStopped:
                kind = "instanceStopped";
                break;
            case eve::stylize::AttackVfxFrameEvent::Kind::CueConsumed: kind = "cueConsumed"; break;
            case eve::stylize::AttackVfxFrameEvent::Kind::CueIgnored: kind = "cueIgnored"; break;
            case eve::stylize::AttackVfxFrameEvent::Kind::LayerStarted: kind = "layerStarted"; break;
            case eve::stylize::AttackVfxFrameEvent::Kind::LayerSkipped: kind = "layerSkipped"; break;
        }
        events.push_back(eve::Value::Object{
            {"kind", kind},
            {"role", std::string(eve::stylize::attackVfxLayerRoleName(event.role))},
            {"cue", event.cue},
            {"layerIndex", static_cast<std::int64_t>(event.layerIndex)},
            {"layerCount", static_cast<std::int64_t>(event.layerCount)},
        });
    }
    return eve::Value::Object{{"advancedCount", static_cast<std::int64_t>(frame.advanced.size())},
                              {"stoppedCount", static_cast<std::int64_t>(frame.stopped.size())},
                              {"eventCount", static_cast<std::int64_t>(frame.events.size())},
                              {"events", std::move(events)}};
}

eve::Value projectInstance(eve::stylize::AttackVfxInstanceState state) {
    std::int64_t activePhases = 0;
    for (const auto& phase : state.phases)
        if (phase.active) ++activePhases;
    return eve::Value::Object{
        {"slot", static_cast<std::int64_t>(state.handle.slot)},
        {"generation", static_cast<std::int64_t>(state.handle.generation)},
        {"recipeId", state.recipeId.format()},
        {"age", state.age},
        {"stopping", state.stopping},
        {"phaseCount", static_cast<std::int64_t>(state.phases.size())},
        {"activeCount", activePhases},
    };
}

eve::stylize::AttackVfxStopMode parseStopMode(std::string_view mode) {
    if (mode == "stop" || mode == "stopEmitting") return eve::stylize::AttackVfxStopMode::StopEmitting;
    return eve::stylize::AttackVfxStopMode::ClearImmediately;
}

}  // namespace

Module_IMPL(StylizeAction, new StylizeAction());

StylizeAction::StylizeAction() {
    registerCameraAttackVfxExecutor();
    registerAudioAttackVfxExecutor();
    registerPrefabAttackVfxExecutor();
    registerActionAttackVfxProvider();
}

StylizeAction::~StylizeAction() {
    unregisterActionAttackVfxProvider();
    unregisterPrefabAttackVfxExecutor();
    unregisterAudioAttackVfxExecutor();
    unregisterCameraAttackVfxExecutor();
}

eve::Result<void> StylizeAction::registerRecipeJson(std::string_view json) {
    auto parsed = eve::stylize::AttackVfxRecipe::fromJson(json);
    if (!parsed) return eve::Result<void>::failure(parsed.status());
    return runtime_.registerRecipe(std::move(parsed).takeValue());
}

eve::Result<void> StylizeAction::registerSkinJson(std::string_view json) {
    auto parsed = eve::stylize::AttackVfxSkin::fromJson(json);
    if (!parsed) return eve::Result<void>::failure(parsed.status());
    return runtime_.registerSkin(std::move(parsed).takeValue());
}

eve::Result<eve::stylize::AttackVfxHandle> StylizeAction::play(std::string_view recipeId,
                                                              std::uint32_t    sourceId,
                                                              std::uint32_t    targetId) {
    auto parsed = eve::LogicalId::parse(recipeId);
    if (!parsed)
        return eve::Result<eve::stylize::AttackVfxHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "recipeId is not a LogicalId", "recipeId"));
    eve::stylize::AttackVfxRequest request;
    request.sourceId = sourceId;
    request.targetId = targetId;
    return runtime_.play(*parsed, request);
}

eve::Result<eve::stylize::AttackVfxFrame> StylizeAction::signal(std::uint32_t    slot,
                                                               std::uint32_t    generation,
                                                               std::string_view cue) {
    return runtime_.signal(makeHandle(slot, generation), cue);
}

eve::Result<eve::stylize::AttackVfxFrame> StylizeAction::advance(double dtSeconds) {
    return runtime_.advance(dtSeconds);
}

eve::Result<eve::stylize::AttackVfxFrame> StylizeAction::stop(std::uint32_t    slot,
                                                             std::uint32_t    generation,
                                                             std::string_view mode) {
    return runtime_.stop(makeHandle(slot, generation), parseStopMode(mode));
}

eve::Result<eve::stylize::AttackVfxInstanceState> StylizeAction::inspect(std::uint32_t slot,
                                                                        std::uint32_t generation) {
    auto snapshot = runtime_.inspect(makeHandle(slot, generation));
    if (!snapshot)
        return eve::Result<eve::stylize::AttackVfxInstanceState>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "AttackVfx handle is stale", "handle"));
    return eve::Result<eve::stylize::AttackVfxInstanceState>::success(std::move(*snapshot));
}

std::size_t StylizeAction::activeCount() const noexcept { return runtime_.activeCount(); }

void StylizeAction::expose(ssq::Table& table) {
    auto module = table.addClass(name, StylizeAction::create, false);
    expose(module);
}

void StylizeAction::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("registerRecipeJson", [vm](StylizeAction* self, const std::string& json) {
        auto result = self ? self->registerRecipeJson(json)
                           : eve::Result<void>::failure(eve::Diagnostic::error(
                                 eve::DiagnosticCode::NotFound, "StylizeAction unavailable", "self"));
        return eve::script::projectResult(vm, std::move(result));
    });
    cls.addFunc("registerSkinJson", [vm](StylizeAction* self, const std::string& json) {
        auto result = self ? self->registerSkinJson(json)
                           : eve::Result<void>::failure(eve::Diagnostic::error(
                                 eve::DiagnosticCode::NotFound, "StylizeAction unavailable", "self"));
        return eve::script::projectResult(vm, std::move(result));
    });
    cls.addFunc("play", [vm](StylizeAction* self, const std::string& recipeId, int sourceId,
                             int targetId) {
        auto result =
            self ? self->play(recipeId, static_cast<std::uint32_t>(sourceId < 0 ? 0 : sourceId),
                              static_cast<std::uint32_t>(targetId < 0 ? 0 : targetId))
                 : eve::Result<eve::stylize::AttackVfxHandle>::failure(eve::Diagnostic::error(
                       eve::DiagnosticCode::NotFound, "StylizeAction unavailable", "self"));
        return eve::script::projectResult(vm, std::move(result),
                                          [](eve::stylize::AttackVfxHandle handle) {
                                              return projectHandle(handle);
                                          });
    });
    cls.addFunc("signal", [vm](StylizeAction* self, int slot, int generation, const std::string& cue) {
        auto result =
            self ? self->signal(static_cast<std::uint32_t>(slot < 0 ? 0 : slot),
                                static_cast<std::uint32_t>(generation < 0 ? 0 : generation), cue)
                 : eve::Result<eve::stylize::AttackVfxFrame>::failure(eve::Diagnostic::error(
                       eve::DiagnosticCode::NotFound, "StylizeAction unavailable", "self"));
        return eve::script::projectResult(vm, std::move(result),
                                          [](eve::stylize::AttackVfxFrame frame) {
                                              return projectFrame(std::move(frame));
                                          });
    });
    cls.addFunc("advance", [vm](StylizeAction* self, float dt) {
        auto result = self ? self->advance(static_cast<double>(dt))
                           : eve::Result<eve::stylize::AttackVfxFrame>::failure(eve::Diagnostic::error(
                                 eve::DiagnosticCode::NotFound, "StylizeAction unavailable", "self"));
        return eve::script::projectResult(vm, std::move(result),
                                          [](eve::stylize::AttackVfxFrame frame) {
                                              return projectFrame(std::move(frame));
                                          });
    });
    cls.addFunc("stop", [vm](StylizeAction* self, int slot, int generation, const std::string& mode) {
        auto result =
            self ? self->stop(static_cast<std::uint32_t>(slot < 0 ? 0 : slot),
                              static_cast<std::uint32_t>(generation < 0 ? 0 : generation), mode)
                 : eve::Result<eve::stylize::AttackVfxFrame>::failure(eve::Diagnostic::error(
                       eve::DiagnosticCode::NotFound, "StylizeAction unavailable", "self"));
        return eve::script::projectResult(vm, std::move(result),
                                          [](eve::stylize::AttackVfxFrame frame) {
                                              return projectFrame(std::move(frame));
                                          });
    });
    cls.addFunc("inspect", [vm](StylizeAction* self, int slot, int generation) {
        auto result =
            self ? self->inspect(static_cast<std::uint32_t>(slot < 0 ? 0 : slot),
                                 static_cast<std::uint32_t>(generation < 0 ? 0 : generation))
                 : eve::Result<eve::stylize::AttackVfxInstanceState>::failure(eve::Diagnostic::error(
                       eve::DiagnosticCode::NotFound, "StylizeAction unavailable", "self"));
        return eve::script::projectResult(vm, std::move(result),
                                          [](eve::stylize::AttackVfxInstanceState state) {
                                              return projectInstance(std::move(state));
                                          });
    });
    cls.addFunc("activeCount", [](StylizeAction* self) -> int {
        return self ? static_cast<int>(self->activeCount()) : 0;
    });
}

}  // namespace eve::stylize_action
