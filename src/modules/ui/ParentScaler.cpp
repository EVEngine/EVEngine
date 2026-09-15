#include "ui/ParentScaler.h"

#include <algorithm>
#include <cmath>

namespace eve::ui {

Result<void> evaluateParentScaler(ParentScalerState* state, ParentScalerOutput* output,
                                  const ParentScalerSettings* settings,
                                  const ParentScalerInput* input) {
    if (!state || !output || !settings || !input || !std::isfinite(settings->maxHeight) ||
        !std::isfinite(input->canvasHeight) || input->targetCount < 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "ui.parentScaler: objects, finite heights and non-negative target count required"));

    ParentScalerOutput candidate;
    if (!settings->scaleWithCanvas || !input->hasCanvas || input->targetCount == 0 ||
        (state->lastScaleHeight == input->canvasHeight && state->lastScaleHeight != 0.f)) {
        *output = candidate;
        return Result<void>::success();
    }
    candidate.applyHeight = true;
    candidate.height = input->canvasHeight;
    if (settings->partScreen)
        candidate.height = std::min(std::max(input->canvasHeight, 0.1f), settings->maxHeight);
    state->lastScaleHeight = input->canvasHeight;
    *output = candidate;
    return Result<void>::success();
}

}  // namespace eve::ui
