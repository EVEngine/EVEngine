#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/ParentScaler.h"

#include <limits>

using namespace eve::ui;

TEST_CASE("ui.parentScaler.fullAndPartScreenTrackCanvasChanges") {
    ParentScalerSettings settings;
    ParentScalerInput input{true, 2, 720.f};
    ParentScalerState state;
    ParentScalerOutput output;
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(output.applyHeight);
    CHECK_EQ(output.height, 720.f);
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(!output.applyHeight);

    settings.partScreen = true;
    input.canvasHeight = 800.f;
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(output.applyHeight);
    CHECK_EQ(output.height, 500.f);
    input.canvasHeight = 0.f;
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK_EQ(output.height, 0.1f);
}

TEST_CASE("ui.parentScaler.disabledEmptyAndInvalidAreAtomic") {
    ParentScalerSettings settings;
    ParentScalerInput input{false, 3, 640.f};
    ParentScalerState state;
    ParentScalerOutput output{true, 123.f};
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(!output.applyHeight);
    CHECK_EQ(state.lastScaleHeight, 0.f);
    input.hasCanvas = true;
    input.targetCount = 0;
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(!output.applyHeight);
    const auto unchangedState = state;
    const auto unchangedOutput = output;
    input.canvasHeight = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateParentScaler(&state, &output, &settings, &input));
    CHECK_EQ(state.lastScaleHeight, unchangedState.lastScaleHeight);
    CHECK_EQ(output.applyHeight, unchangedOutput.applyHeight);
}
