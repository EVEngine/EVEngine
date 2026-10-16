#include <array>
#include <cmath>
#include <limits>
#include "procgen/spline/SplinePath.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
namespace {
SplinePath unevenPath() {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    for (const auto& xyz : std::array<std::array<float, 3>, 3>{{{-4, 0, 0}, {0, 0, 8}, {12, 0, 9}}}) {
        SplinePoint point;
        point.x             = xyz[0];
        point.y             = xyz[1];
        point.z             = xyz[2];
        point.rollDegrees   = 15.f;
        point.rollPivotSide = 2.f;
        point.worldUpFrame  = true;
        REQUIRE(path.addPointResult(point).ok());
    }
    return path;
}
}  // namespace
TEST_CASE("procgen.splinePath.explicitFramesReusePoseAndReportActualArcPhase") {
    auto                       path           = unevenPath();
    const std::array<float, 5> parameters     = {0, .25f, .5f, .75f, 1};
    auto                       explicitFrames = path.sampleFramesAtParametersResult(parameters);
    auto                       fixed          = path.sampleFramesResult(4, false);
    REQUIRE(explicitFrames.ok());
    REQUIRE(fixed.ok());
    REQUIRE(explicitFrames.value().size() == fixed.value().size());
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const auto& a = explicitFrames.value()[i];
        const auto& b = fixed.value()[i];
        REQUIRE(a.sample.x == b.sample.x);
        REQUIRE(a.sample.y == b.sample.y);
        REQUIRE(a.sample.z == b.sample.z);
        REQUIRE(a.sideX == b.sideX);
        REQUIRE(a.sideY == b.sideY);
        REQUIRE(a.sideZ == b.sideZ);
        REQUIRE(a.upX == b.upX);
        REQUIRE(a.upY == b.upY);
        REQUIRE(a.upZ == b.upZ);
        REQUIRE(a.forwardX == b.forwardX);
        REQUIRE(a.forwardY == b.forwardY);
        REQUIRE(a.forwardZ == b.forwardZ);
    }
    auto marker = path.closestPointResult(0, 0, 8, 24);
    REQUIRE(marker.ok());
    REQUIRE(std::fabs(explicitFrames.value()[2].sample.normalizedDistance - marker.value().normalizedDistance) < 1e-6f);
    REQUIRE(std::fabs(explicitFrames.value()[2].sample.normalizedDistance - .5f) > .02f);
    REQUIRE(explicitFrames.value()[0].authoredMarker);
    REQUIRE(explicitFrames.value()[2].authoredMarker);
    REQUIRE(explicitFrames.value()[4].authoredMarker);
}
TEST_CASE("procgen.splinePath.explicitFramesRejectInvalidInputsWithoutAuthorMutation") {
    auto                       path      = unevenPath();
    const auto                 revision  = path.revision();
    const std::array<float, 2> duplicate = {.5f, .5f}, descending = {.7f, .2f}, outside = {0, 1.1f};
    const std::array<float, 2> nan = {0, std::numeric_limits<float>::quiet_NaN()};
    REQUIRE(!path.sampleFramesAtParametersResult(duplicate).ok());
    REQUIRE(!path.sampleFramesAtParametersResult(descending).ok());
    REQUIRE(!path.sampleFramesAtParametersResult(outside).ok());
    REQUIRE(!path.sampleFramesAtParametersResult(nan).ok());
    REQUIRE(!path.sampleFramesAtParametersResult(std::span<const float>{}).ok());
    const std::vector<float> tooMany(4098, 0);
    REQUIRE(!path.sampleFramesAtParametersResult(tooMany).ok());
    const std::array<float, 1> one = {.4f};
    REQUIRE(path.sampleFramesAtParametersResult(one).ok());
    REQUIRE(path.revision() == revision);
}
TEST_CASE("procgen.splinePath.explicitFramesKeepPartialClosedRangesOpen") {
    SplinePath path;
    REQUIRE(path.applyShapePresetResult("circle", 8, 10, 0).ok());
    REQUIRE(path.isClosed());
    const std::array<float, 2> parameters = {.125f, .5f};
    auto                       frames     = path.sampleFramesAtParametersResult(parameters);
    REQUIRE(frames.ok());
    auto end = path.evaluateResult(.5f);
    REQUIRE(end.ok());
    REQUIRE(frames.value().back().sample.x == end.value().x);
    REQUIRE(frames.value().back().sample.y == end.value().y);
    REQUIRE(frames.value().back().sample.z == end.value().z);
    REQUIRE(std::hypot(frames.value().back().sample.x - frames.value().front().sample.x,
                       frames.value().back().sample.z - frames.value().front().sample.z) > 1.f);
}
