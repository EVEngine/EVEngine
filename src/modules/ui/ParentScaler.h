#pragma once
#include "common/Export.h"


#include "common/Result.h"

namespace ssq {
class Table;
}

namespace eve::ui {

/** @brief Configuration for Pcg ParentScaler canvas-height behavior. */
struct ParentScalerSettings {
    bool scaleWithCanvas = true;
    bool partScreen = false;
    float maxHeight = 500.f;
};

/** @brief Current canvas and target-list observation. */
struct ParentScalerInput {
    bool hasCanvas = false;
    int targetCount = 0;
    float canvasHeight = 0.f;
};

/** @brief Caller-owned change detector retained across evaluations. */
struct ParentScalerState {
    float lastScaleHeight = 0.f;
};

/** @brief Height write emitted for every valid target rect. */
struct ParentScalerOutput {
    bool applyHeight = false;
    float height = 0.f;
};

/**
 * @brief Evaluate Pcg ParentScaler when canvas height changes.
 * @param state Required caller-owned previous-height state.
 * @param output Required caller-owned output replaced atomically on success.
 * @param settings Required finite settings.
 * @param input Current canvas observation and number of target rects.
 * @return Success, or InvalidArgument without changing state/output.
 * @ownership All values remain caller-owned and no references are retained.
 * @thread UI owner thread; no callbacks are invoked.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> evaluateParentScaler(ParentScalerState* state, ParentScalerOutput* output,
                                                                   const ParentScalerSettings* settings,
                                                                   const ParentScalerInput*    input);

/** @brief Register ParentScaler value types and evaluator with the root script table. */
void exposeParentScalerBindings(ssq::Table& table);
}
