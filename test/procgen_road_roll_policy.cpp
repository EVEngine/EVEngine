#include <cmath>
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.rollEasingMatchesActualEasyRoadsSpanSamples") {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    for (int i = 0; i < 3; ++i) {
        SplinePoint point;
        point.x                 = -18.f + 18.f * static_cast<float>(i);
        point.rollDegrees       = i == 1 ? 15.f : -10.f;
        point.rollInterpolation = SplineRollInterpolation::SmoothStep;
        REQUIRE(path.addPointResult(point).ok());
    }
    auto sample = path.evaluateResult(1.f / 6.f);
    REQUIRE(sample.ok());
    REQUIRE(std::fabs(sample.value().x + 13.333333f) < 1e-4f);
    REQUIRE(std::fabs(sample.value().rollDegrees + 3.518519f) < 1e-4f);
    sample = path.evaluateResult(.25f);
    REQUIRE(sample.ok());
    REQUIRE(std::fabs(sample.value().rollDegrees - 2.5f) < 1e-5f);
    sample = path.evaluateResult(5.f / 6.f);
    REQUIRE(sample.ok());
    REQUIRE(std::fabs(sample.value().rollDegrees + 3.518519f) < 1e-4f);
    SplinePath  linear;
    SplinePoint a, b;
    a.rollDegrees = -10.f;
    b.x           = 18.f;
    b.rollDegrees = 15.f;
    REQUIRE(linear.addPointResult(a).ok());
    REQUIRE(linear.addPointResult(b).ok());
    REQUIRE(std::fabs(linear.evaluateResult(.25f).value().rollDegrees + 3.75f) < 1e-5f);
    a.rollInterpolation = static_cast<SplineRollInterpolation>(9);
    REQUIRE(!linear.addPointResult(a).ok());
}

TEST_CASE("procgen.road.authoring.rollPolicySnapshotMigrationAndInvalidRejection") {
    auto network = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(network.ok());
    REQUIRE(network.value().edges()[0].style.rollInterpolation == RoadRollInterpolation::SmoothStep);
    auto captured = captureRoadNetwork(network.value());
    REQUIRE(captured.ok());
    auto restored = restoreRoadNetwork(captured.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].style.rollInterpolation == RoadRollInterpolation::SmoothStep);
    auto  value = captured.value();
    auto& edges = value.getIf<eve::Value::Object>()->at("edges");
    auto& style = edges.getIf<eve::Value::Array>()->at(0).getIf<eve::Value::Object>()->at("style");
    style.getIf<eve::Value::Object>()->erase("rollInterpolation");
    restored = restoreRoadNetwork(value);
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].style.rollInterpolation == RoadRollInterpolation::Linear);
    (*style.getIf<eve::Value::Object>())["rollInterpolation"] = 9;
    REQUIRE(!restoreRoadNetwork(value).ok());
    auto invalid              = network.value().edges()[0].style;
    invalid.rollInterpolation = static_cast<RoadRollInterpolation>(9);
    REQUIRE(!network.value().setEdgeStyle(1, invalid).ok());
    REQUIRE(network.value().edges()[0].style.rollInterpolation == RoadRollInterpolation::SmoothStep);
}
