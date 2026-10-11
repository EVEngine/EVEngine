#include "ui/ParentScaler.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::ui {

void exposeParentScalerBindings(ssq::Table& table) {
    auto settings = table.addClass("ParentScalerSettings", ssq::Class::Ctor<ParentScalerSettings()>());
    settings.addVar("scaleWithCanvas", &ParentScalerSettings::scaleWithCanvas);
    settings.addVar("partScreen", &ParentScalerSettings::partScreen);
    settings.addVar("maxHeight", &ParentScalerSettings::maxHeight);
    auto input = table.addClass("ParentScalerInput", ssq::Class::Ctor<ParentScalerInput()>());
    input.addVar("hasCanvas", &ParentScalerInput::hasCanvas);
    input.addVar("targetCount", &ParentScalerInput::targetCount);
    input.addVar("canvasHeight", &ParentScalerInput::canvasHeight);
    auto state = table.addClass("ParentScalerState", ssq::Class::Ctor<ParentScalerState()>());
    state.addVar("lastScaleHeight", &ParentScalerState::lastScaleHeight);
    auto output = table.addClass("ParentScalerOutput", ssq::Class::Ctor<ParentScalerOutput()>());
    output.addVar("applyHeight", &ParentScalerOutput::applyHeight);
    output.addVar("height", &ParentScalerOutput::height);
    table.addFunc("evaluateParentScaler",
                  [vm = table.getHandle()](ParentScalerState* state, ParentScalerOutput* output,
                                           ParentScalerSettings* settings,
                                           ParentScalerInput* input) {
        return eve::script::projectResult(vm,
                                          evaluateParentScaler(state, output, settings, input));
    });
}

}  // namespace eve::ui
