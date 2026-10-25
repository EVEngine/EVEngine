#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace {
using eve::procgen::SplinePath;
using eve::procgen::SplinePoint;

SplinePath unevenPath() {
    SplinePath path;
    path.setKindResult("catmullRom").expect("test path kind");
    for (const auto& xyz : std::array<std::array<float, 3>, 3>{{{-4, 0, 0}, {0, 0, 8}, {12, 0, 9}}}) {
        SplinePoint point;
        point.x             = xyz[0];
        point.y             = xyz[1];
        point.z             = xyz[2];
        point.rollDegrees   = 15.f;
        point.rollPivotSide = 2.f;
        point.worldUpFrame  = true;
        path.addPointResult(point).expect("test control point");
    }
    return path;
}

// Input contract: the caller supplies 1..4097 strictly increasing finite
// parameters in [0,1]. Bad input does not edit the authored path revision.
TEST_CASE("procgen.spline.explicit.spec.parameter_validation") {
    auto                       path     = unevenPath();
    const auto                 revision = path.revision();
    const std::array<float, 1> valid    = {.4f};
    REQUIRE(path.sampleFramesAtParametersResult(valid).ok());
    const std::array<float, 2> duplicate  = {.5f, .5f};
    const std::array<float, 2> descending = {.7f, .2f};
    const std::array<float, 2> negative   = {-.01f, .5f};
    const std::array<float, 2> beyond     = {0.f, 1.01f};
    const std::array<float, 2> notFinite  = {0.f, std::numeric_limits<float>::quiet_NaN()};
    CHECK(!path.sampleFramesAtParametersResult(std::span<const float>{}).ok());
    CHECK(!path.sampleFramesAtParametersResult(duplicate).ok());
    CHECK(!path.sampleFramesAtParametersResult(descending).ok());
    CHECK(!path.sampleFramesAtParametersResult(negative).ok());
    CHECK(!path.sampleFramesAtParametersResult(beyond).ok());
    CHECK(!path.sampleFramesAtParametersResult(notFinite).ok());
    CHECK(!path.sampleFramesAtParametersResult(std::vector<float>(4098, 0.f)).ok());
    CHECK_EQ(path.revision(), revision);
}

// Pose contract: explicit uniform parameters reproduce the fixed sampler's
// position and transported basis, including authored roll/pivot policy.
TEST_CASE("procgen.spline.explicit.spec.uniform_positions_and_basis") {
    auto                       path           = unevenPath();
    const std::array<float, 5> ratios         = {0.f, .25f, .5f, .75f, 1.f};
    auto                       explicitFrames = path.sampleFramesAtParametersResult(ratios);
    auto                       fixedFrames    = path.sampleFramesResult(4, false);
    REQUIRE(explicitFrames.ok());
    REQUIRE(fixedFrames.ok());
    REQUIRE_EQ(explicitFrames.value().size(), ratios.size());
    REQUIRE_EQ(fixedFrames.value().size(), ratios.size());
    for (std::size_t i = 0; i < ratios.size(); ++i) {
        const auto& actual   = explicitFrames.value()[i];
        const auto& expected = fixedFrames.value()[i];
        CHECK_EQ(actual.sample.x, expected.sample.x);
        CHECK_EQ(actual.sample.y, expected.sample.y);
        CHECK_EQ(actual.sample.z, expected.sample.z);
        CHECK_EQ(actual.sideX, expected.sideX);
        CHECK_EQ(actual.sideY, expected.sideY);
        CHECK_EQ(actual.sideZ, expected.sideZ);
        CHECK_EQ(actual.upX, expected.upX);
        CHECK_EQ(actual.upY, expected.upY);
        CHECK_EQ(actual.upZ, expected.upZ);
        CHECK_EQ(actual.forwardX, expected.forwardX);
        CHECK_EQ(actual.forwardY, expected.forwardY);
        CHECK_EQ(actual.forwardZ, expected.forwardZ);
    }
}

// Distance contract: output normalizedDistance measures arc length; it does
// not claim the author's nonuniform parameter itself is distance.
TEST_CASE("procgen.spline.explicit.spec.arc_distance_and_markers") {
    auto                       path         = unevenPath();
    const std::array<float, 3> parameters   = {0.f, .5f, 1.f};
    auto                       frames       = path.sampleFramesAtParametersResult(parameters);
    auto                       controlPoint = path.closestPointResult(0.f, 0.f, 8.f, 24);
    REQUIRE(frames.ok());
    REQUIRE(controlPoint.ok());
    CHECK(std::fabs(frames.value()[1].sample.normalizedDistance - controlPoint.value().normalizedDistance) < 1e-6f);
    CHECK(std::fabs(frames.value()[1].sample.normalizedDistance - .5f) > .02f);
    for (const auto& frame : frames.value()) CHECK(frame.authoredMarker);
}

// Closed-path contract: requesting only a subrange does not force its two
// endpoints into a seam or duplicate the first frame at the end.
TEST_CASE("procgen.spline.explicit.spec.closed_partial_range") {
    SplinePath path;
    REQUIRE(path.applyShapePresetResult("circle", 8, 10, 0).ok());
    REQUIRE(path.isClosed());
    const std::array<float, 2> parameters = {.125f, .5f};
    auto                       frames     = path.sampleFramesAtParametersResult(parameters);
    auto end = path.evaluateResult(.5f);
    REQUIRE(frames.ok());
    REQUIRE(end.ok());
    CHECK_EQ(frames.value().back().sample.x, end.value().x);
    CHECK_EQ(frames.value().back().sample.y, end.value().y);
    CHECK_EQ(frames.value().back().sample.z, end.value().z);
    CHECK(std::hypot(frames.value().back().sample.x - frames.value().front().sample.x,
                     frames.value().back().sample.z - frames.value().front().sample.z) > 1.f);
}
}  // namespace
