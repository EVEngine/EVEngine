#include <limits>
#include "graphics/DepthOfFieldFocus.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::graphics;
TEST_CASE("graphics.depthOfFieldFocus.matchesPcgTrackingAndSmoothing") {
    DepthOfFieldFocusState    state;
    DepthOfFieldFocusOutput   out;
    DepthOfFieldFocusSettings settings;
    DepthOfFieldFocusInput    input;
    input.hasRayHit      = true;
    input.rayHitDistance = 20.f;
    input.deltaSeconds   = 0.1f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(out.focusDistance, 72.f);
    CHECK_EQ(out.aperture, 7.5f);
    CHECK_EQ(out.focalLength, 30.f);
    input.hitPlayer = true;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(out.focusDistance, 81.8f);
    settings.tracking    = int(DepthOfFieldTracking::FixedOffset);
    settings.focusOffset = 12.f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(out.focusDistance, 12.f);
    auto before              = state;
    settings.maximumDistance = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(state.focusDistance, before.focusDistance);
}
TEST_CASE("graphics.depthOfFieldFocus.followTargetUsesObservedDistance") {
    DepthOfFieldFocusState    state;
    DepthOfFieldFocusOutput   out;
    DepthOfFieldFocusSettings settings;
    DepthOfFieldFocusInput    input;
    settings.tracking    = int(DepthOfFieldTracking::FollowTarget);
    settings.focusOffset = 2.f;
    settings.response    = 10.f;
    input.hasTarget      = true;
    input.targetDistance = 8.f;
    input.deltaSeconds   = 1.f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(out.focusDistance, 10.f);
}
