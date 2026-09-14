#include "graphics/DepthOfFieldFocus.h"
#include <algorithm>
#include <cmath>
namespace eve::graphics {
Result<void> evaluateDepthOfFieldFocus(DepthOfFieldFocusState* state, DepthOfFieldFocusOutput* output,
                                       const DepthOfFieldFocusSettings* settings, const DepthOfFieldFocusInput* input) {
    if (!state || !output || !settings || !input)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "graphics.depthOfFieldFocus: objects required"));
    const bool finite = std::isfinite(settings->focusOffset) && std::isfinite(settings->maximumDistance) &&
                        std::isfinite(settings->aperture) && std::isfinite(settings->focalLength) &&
                        std::isfinite(settings->response) && std::isfinite(input->rayHitDistance) &&
                        std::isfinite(input->targetDistance) && std::isfinite(input->deltaSeconds);
    if (!finite || settings->maximumDistance < 0.f || settings->aperture <= 0.f || settings->focalLength <= 0.f ||
        settings->response < 0.f || input->deltaSeconds < 0.f || input->rayHitDistance < 0.f ||
        input->targetDistance < 0.f || settings->tracking < 0 || settings->tracking > 2)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "graphics.depthOfFieldFocus: invalid finite settings"));
    DepthOfFieldFocusState  next = *state;
    DepthOfFieldFocusOutput result;
    result.active      = settings->enabled;
    result.aperture    = settings->aperture;
    result.focalLength = settings->focalLength;
    if (!next.initialized) {
        next.initialized     = true;
        next.maximumExceeded = true;
        next.focusDistance   = settings->maximumDistance;
    }
    float      target   = next.focusDistance;
    const auto tracking = static_cast<DepthOfFieldTracking>(settings->tracking);
    if (tracking == DepthOfFieldTracking::FixedOffset) {
        target               = settings->focusOffset;
        next.maximumExceeded = false;
    } else if (tracking == DepthOfFieldTracking::FollowTarget) {
        if (input->hasTarget) {
            target               = input->targetDistance + settings->focusOffset;
            next.maximumExceeded = false;
        }
    } else if (input->hasRayHit) {
        if (settings->interactWithPlayer && input->hitPlayer) {
            target               = settings->maximumDistance;
            next.maximumExceeded = true;
        } else {
            target               = input->rayHitDistance + settings->focusOffset;
            next.maximumExceeded = false;
        }
    } else if (next.maximumExceeded)
        target = settings->maximumDistance;
    if (tracking == DepthOfFieldTracking::FixedOffset)
        next.focusDistance = target;
    else {
        const float t = std::clamp(input->deltaSeconds * settings->response, 0.f, 1.f);
        next.focusDistance += (target - next.focusDistance) * t;
    }
    next.focusDistance   = std::max(0.f, next.focusDistance);
    result.focusDistance = next.focusDistance;
    *state               = next;
    *output              = result;
    return Result<void>::success();
}
}  // namespace eve::graphics
