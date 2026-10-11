#pragma once

#include "common/Result.h"

namespace ssq {
class Table;
}

namespace eve::graphics {

/** @brief Pcg-compatible autofocus tracking mode. */
enum class DepthOfFieldTracking { FollowScreen = 0, FollowTarget = 1, FixedOffset = 2 };

/** @brief Provider-neutral autofocus configuration. */
struct DepthOfFieldFocusSettings {
    int   tracking           = int(DepthOfFieldTracking::FollowScreen);
    bool  enabled            = true;
    bool  interactWithPlayer = true;
    float focusOffset        = 0.f;
    float maximumDistance    = 100.f;
    float aperture           = 7.5f;
    float focalLength        = 30.f;
    float response           = 3.5f;
};

/** @brief One frame of caller-owned raycast or target observations. */
struct DepthOfFieldFocusInput {
    bool  hasRayHit      = false;
    bool  hitPlayer      = false;
    float rayHitDistance = 0.f;
    bool  hasTarget      = false;
    float targetDistance = 0.f;
    float deltaSeconds   = 0.f;
};

/** @brief Persistent autofocus state owned by the caller. */
struct DepthOfFieldFocusState {
    bool  initialized     = false;
    bool  maximumExceeded = true;
    float focusDistance   = 1.f;
};

/** @brief Focus and lens values ready for a depth-of-field renderer. */
struct DepthOfFieldFocusOutput {
    bool  active        = false;
    float focusDistance = 1.f;
    float aperture      = 7.5f;
    float focalLength   = 30.f;
};

/**
 * @brief Evaluate Pcg AutoDepthOfField tracking and smoothing for one frame.
 * @param state Required caller-owned persistent tracking state.
 * @param output Required caller-owned output replaced atomically on success.
 * @param settings Required finite lens and distance configuration.
 * @param input Required finite raycast/target observation and injected delta time.
 * @return Success, or InvalidArgument before state or output mutation.
 * @ownership All objects remain caller-owned and no pointer is retained.
 * @thread Any thread; raycasts and rendering are performed by the caller.
 */
[[nodiscard]] Result<void> evaluateDepthOfFieldFocus(DepthOfFieldFocusState* state, DepthOfFieldFocusOutput* output,
                                                     const DepthOfFieldFocusSettings* settings,
                                                     const DepthOfFieldFocusInput*    input);

/** @brief Register Pcg autofocus value types and evaluator. */
void exposeDepthOfFieldFocusBindings(ssq::Table& table);

}  // namespace eve::graphics
