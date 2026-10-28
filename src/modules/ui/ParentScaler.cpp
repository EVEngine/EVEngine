#include "ui/ParentScaler.h"
#include <algorithm>
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"

// ParentScaler.cpp
namespace eve::ui {

Result<void> evaluateParentScaler(ParentScalerState* state, ParentScalerOutput* output,
                                  const ParentScalerSettings* settings, const ParentScalerInput* input) {
    if (!state || !output || !settings || !input || !std::isfinite(settings->maxHeight) ||
        !std::isfinite(input->canvasHeight) || input->targetCount < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "ui.parentScaler: objects, finite heights and non-negative target count required"));

    ParentScalerOutput candidate;
    if (!settings->scaleWithCanvas || !input->hasCanvas || input->targetCount == 0 ||
        (state->lastScaleHeight == input->canvasHeight && state->lastScaleHeight != 0.f)) {
        *output = candidate;
        return Result<void>::success();
    }
    candidate.applyHeight = true;
    candidate.height      = input->canvasHeight;
    if (settings->partScreen) candidate.height = std::min(std::max(input->canvasHeight, 0.1f), settings->maxHeight);
    state->lastScaleHeight = input->canvasHeight;
    *output                = candidate;
    return Result<void>::success();
}

}  // namespace eve::ui

// ParentScalerBindings.cpp
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
                                           ParentScalerSettings* settings, ParentScalerInput* input) {
                      return eve::script::projectResult(vm, evaluateParentScaler(state, output, settings, input));
                  });
}

}  // namespace eve::ui
