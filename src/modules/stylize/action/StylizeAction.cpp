#include "stylize/action/StylizeAction.h"

#include "stylize/AttackVfxRecipe.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace eve::stylize_action {

void registerCameraAttackVfxExecutor();
void unregisterCameraAttackVfxExecutor();
void registerActionAttackVfxProvider();
void unregisterActionAttackVfxProvider();

Module_IMPL(StylizeAction, new StylizeAction());

StylizeAction::StylizeAction() {
    registerCameraAttackVfxExecutor();
    registerActionAttackVfxProvider();
}

StylizeAction::~StylizeAction() {
    unregisterActionAttackVfxProvider();
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

void StylizeAction::expose(ssq::Table& table) {
    auto module = table.addClass(name, StylizeAction::create, false);
    expose(module);
}

void StylizeAction::expose(ssq::Class& cls) {
    cls.addFunc("registerRecipeJson", [](StylizeAction* self, const std::string& json) {
        if (!self) throw std::runtime_error("StylizeAction module is unavailable");
        auto result = self->registerRecipeJson(json);
        if (!result) throw std::runtime_error(result.status().describe());
    });
    cls.addFunc("registerSkinJson", [](StylizeAction* self, const std::string& json) {
        if (!self) throw std::runtime_error("StylizeAction module is unavailable");
        auto result = self->registerSkinJson(json);
        if (!result) throw std::runtime_error(result.status().describe());
    });
    cls.addFunc("activeCount", [](StylizeAction* self) -> int {
        return self ? static_cast<int>(self->runtime().activeCount()) : 0;
    });
}

}  // namespace eve::stylize_action
