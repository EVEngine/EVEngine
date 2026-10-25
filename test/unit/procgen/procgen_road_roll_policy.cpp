#include <cmath>
#include <limits>
#include <tuple>
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
bool near(float actual, float expected) { return std::fabs(actual - expected) < 1e-4f; }

SplinePath rollSpan(SplineRollInterpolation policy) {
    SplinePath path;
    SplinePoint first, last;
    first.rollDegrees       = -10.f;
    first.rollPivotSide     = -2.f;
    first.rollInterpolation = policy;
    last.x                  = 20.f;
    last.rollDegrees        = 30.f;
    last.rollPivotSide      = 6.f;
    REQUIRE(path.addPointResult(first).ok());
    REQUIRE(path.addPointResult(last).ok());
    return path;
}
}  // namespace

TEST_CASE("procgen.road.roll.linearProfileContract") {
    // Two endpoints define a linear roll and pivot profile over the entire span.
    auto path = rollSpan(SplineRollInterpolation::Linear);
    for (const auto [t, roll, pivot] :
         {std::tuple{0.f, -10.f, -2.f}, std::tuple{.25f, 0.f, 0.f}, std::tuple{.5f, 10.f, 2.f},
          std::tuple{.75f, 20.f, 4.f}, std::tuple{1.f, 30.f, 6.f}}) {
        auto sample = path.evaluateResult(t);
        REQUIRE(sample.ok());
        REQUIRE(near(sample.value().rollDegrees, roll));
        REQUIRE(near(sample.value().rollPivotSide, pivot));
    }
}

TEST_CASE("procgen.road.roll.smoothStepProfileContract") {
    auto path = rollSpan(SplineRollInterpolation::SmoothStep);
    // smoothstep(.25)=.15625; smoothstep(.75)=.84375.
    for (const auto [t, roll, pivot] :
         {std::tuple{0.f, -10.f, -2.f}, std::tuple{.25f, -3.75f, -.75f}, std::tuple{.5f, 10.f, 2.f},
          std::tuple{.75f, 23.75f, 4.75f}, std::tuple{1.f, 30.f, 6.f}}) {
        auto sample = path.evaluateResult(t);
        REQUIRE(sample.ok());
        REQUIRE(near(sample.value().rollDegrees, roll));
        REQUIRE(near(sample.value().rollPivotSide, pivot));
    }
}

TEST_CASE("procgen.road.roll.invalidInputPreservesPriorProfile") {
    auto path   = rollSpan(SplineRollInterpolation::SmoothStep);
    auto before = path.evaluateResult(.25f);
    REQUIRE(before.ok());
    SplinePoint invalid;
    invalid.x                 = 40.f;
    invalid.rollInterpolation = static_cast<SplineRollInterpolation>(9);
    REQUIRE(!path.addPointResult(invalid).ok());
    invalid.rollInterpolation = SplineRollInterpolation::Linear;
    invalid.rollDegrees       = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!path.addPointResult(invalid).ok());
    REQUIRE(!path.evaluateResult(std::numeric_limits<float>::infinity()).ok());
    REQUIRE(!path.evaluateResult(std::numeric_limits<float>::quiet_NaN()).ok());
    auto after = path.evaluateResult(.25f);
    REQUIRE(after.ok());
    REQUIRE(near(after.value().rollDegrees, before.value().rollDegrees));
    REQUIRE(near(after.value().rollPivotSide, before.value().rollPivotSide));
}

TEST_CASE("procgen.road.roll.snapshotRoundTripLegacyDefaultAndInvalidRejection") {
    auto network = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(network.ok());
    const auto edgeId       = network.value().edges()[0].id;
    auto       style        = network.value().edges()[0].style;
    style.rollInterpolation = RoadRollInterpolation::SmoothStep;
    REQUIRE(network.value().setEdgeStyle(edgeId, style).ok());
    auto captured = captureRoadNetwork(network.value());
    REQUIRE(captured.ok());
    auto restored = restoreRoadNetwork(captured.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].style.rollInterpolation == RoadRollInterpolation::SmoothStep);

    // A snapshot produced before the field existed selects the legacy linear policy.
    auto  legacy       = captured.value();
    auto& encodedEdges = legacy.getIf<eve::Value::Object>()->at("edges");
    auto& encodedStyle = encodedEdges.getIf<eve::Value::Array>()->at(0).getIf<eve::Value::Object>()->at("style");
    encodedStyle.getIf<eve::Value::Object>()->erase("rollInterpolation");
    restored = restoreRoadNetwork(legacy);
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].style.rollInterpolation == RoadRollInterpolation::Linear);

    // Rejection must preserve the already valid network.
    (*encodedStyle.getIf<eve::Value::Object>())["rollInterpolation"] = 9;
    REQUIRE(!restoreRoadNetwork(legacy).ok());
    style.rollInterpolation = static_cast<RoadRollInterpolation>(9);
    REQUIRE(!network.value().setEdgeStyle(edgeId, style).ok());
    REQUIRE(network.value().edges()[0].style.rollInterpolation == RoadRollInterpolation::SmoothStep);
}
