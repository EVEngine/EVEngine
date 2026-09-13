#include "procgen/spline/SplinePath.h"

#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::procgen;

namespace {

SplinePoint point(float x, float y, float z) {
    SplinePoint value;
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

}  // namespace

TEST_CASE("procgen.splinePath.samplesLinearPathByArcLength") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(3.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(3.f, 4.f, 0.f)).ok());
    auto length = path.lengthResult();
    REQUIRE(length.ok());
    CHECK(std::abs(length.value() - 7.f) < 1e-5f);
    auto sample = path.evaluateDistanceResult(3.5f);
    REQUIRE(sample.ok());
    CHECK(std::abs(sample.value().x - 3.f) < 1e-4f);
    CHECK(std::abs(sample.value().y - 0.5f) < 1e-4f);
    CHECK(std::abs(sample.value().normalizedDistance - 0.5f) < 1e-5f);
}

TEST_CASE("procgen.splinePath.supportsBezierClosedAndClosestQueries") {
    SplinePath path;
    REQUIRE(path.setKindResult("bezier").ok());
    auto a = point(0.f, 0.f, 0.f);
    a.outY = 2.f;
    auto b = point(2.f, 0.f, 0.f);
    b.inY  = 2.f;
    REQUIRE(path.addPointResult(a).ok());
    REQUIRE(path.addPointResult(b).ok());
    auto midpoint = path.evaluateResult(0.5f);
    REQUIRE(midpoint.ok());
    CHECK(std::abs(midpoint.value().x - 1.f) < 1e-5f);
    CHECK(midpoint.value().y > 1.49f);
    auto closest = path.closestPointResult(1.f, 1.4f, 0.f, 64);
    REQUIRE(closest.ok());
    CHECK(std::abs(closest.value().x - 1.f) < 0.05f);
    path.setClosed(true);
    CHECK_EQ(path.segmentCount(), 2);
}

TEST_CASE("procgen.splinePath.supportsExplicitQuadraticBezierInterpolation") {
    SplinePath path;
    REQUIRE(path.setKindResult("quadraticBezier").ok());
    SplinePoint first{0.f, 0.f, 0.f};
    first.outY = 2.f;
    SplinePoint second{2.f, 0.f, 0.f};
    second.inY = 2.f;
    REQUIRE(path.addPointResult(first).ok());
    REQUIRE(path.addPointResult(second).ok());
    const auto middle = path.evaluateResult(0.5f);
    REQUIRE(middle.ok());
    CHECK(std::abs(middle.value().x - 1.f) < 1e-5f);
    CHECK(std::abs(middle.value().y - 1.f) < 1e-5f);
    CHECK(std::abs(middle.value().tangentX - 1.f) < 1e-5f);
    CHECK(std::abs(middle.value().tangentY) < 1e-5f);
}

TEST_CASE("procgen.splinePath.rejectsInvalidStateAtomically") {
    SplinePath path;
    const auto revision = path.revision();
    CHECK(!path.setKindResult("nurbs").ok());
    CHECK_EQ(path.revision(), revision);
    CHECK(!path.evaluateResult(0.5f).ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    CHECK(!path.addPointResult(point(NAN, 0.f, 0.f)).ok());
    CHECK_EQ(path.pointCount(), 1);
}

TEST_CASE("procgen.splinePath.parallelTransportFramesStayOrthonormal") {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(1.f, 2.f, 0.5f)).ok());
    REQUIRE(path.addPointResult(point(-1.f, 4.f, 1.5f)).ok());
    REQUIRE(path.addPointResult(point(0.5f, 6.f, 0.f)).ok());
    auto frames = path.sampleFramesResult(32, true, 17.f, 32);
    REQUIRE(frames.ok());
    CHECK_EQ(frames.value().size(), std::size_t(33));
    for (const auto& frame : frames.value()) {
        const float sideLength =
            std::sqrt(frame.sideX * frame.sideX + frame.sideY * frame.sideY + frame.sideZ * frame.sideZ);
        const float upLength    = std::sqrt(frame.upX * frame.upX + frame.upY * frame.upY + frame.upZ * frame.upZ);
        const float sideTangent = frame.sideX * frame.sample.tangentX + frame.sideY * frame.sample.tangentY +
                                  frame.sideZ * frame.sample.tangentZ;
        const float upTangent =
            frame.upX * frame.sample.tangentX + frame.upY * frame.sample.tangentY + frame.upZ * frame.sample.tangentZ;
        CHECK(std::abs(sideLength - 1.f) < 1e-5f);
        CHECK(std::abs(upLength - 1.f) < 1e-5f);
        CHECK(std::abs(sideTangent) < 1e-5f);
        CHECK(std::abs(upTangent) < 1e-5f);
    }
}

TEST_CASE("procgen.splinePath.closedTransportFramesCorrectHolonomy") {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    path.setClosed(true);
    REQUIRE(path.addPointResult(point(-2.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, 1.f, 2.f)).ok());
    REQUIRE(path.addPointResult(point(2.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, -1.f, -2.f)).ok());
    auto frames = path.sampleFramesResult(48, true);
    REQUIRE(frames.ok());
    const auto& first = frames.value().front();
    const auto& last  = frames.value().back();
    CHECK(std::abs(first.sample.x - last.sample.x) < 1e-6f);
    CHECK(std::abs(first.sideX - last.sideX) < 1e-6f);
    CHECK(std::abs(first.sideY - last.sideY) < 1e-6f);
    CHECK(std::abs(first.sideZ - last.sideZ) < 1e-6f);
    CHECK(std::abs(first.upX - last.upX) < 1e-6f);
    CHECK(std::abs(first.upY - last.upY) < 1e-6f);
    CHECK(std::abs(first.upZ - last.upZ) < 1e-6f);
    CHECK(std::abs(last.sample.normalizedDistance - 1.f) < 1e-6f);
}

TEST_CASE("procgen.splinePath.interpolatesPointRollAndProfileScale") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 4.f)).ok());
    REQUIRE(path.setPointProfileResult(0, 0.f, 1.f, 1.f).ok());
    REQUIRE(path.setPointProfileResult(1, 90.f, 3.f, 2.f).ok());
    auto midpoint = path.evaluateResult(0.5f);
    REQUIRE(midpoint.ok());
    CHECK(std::abs(midpoint.value().rollDegrees - 45.f) < 1e-6f);
    CHECK(std::abs(midpoint.value().scaleX - 2.f) < 1e-6f);
    CHECK(std::abs(midpoint.value().scaleY - 1.5f) < 1e-6f);
    const auto revision = path.revision();
    CHECK(!path.setPointProfileResult(1, 0.f, 0.f, 1.f).ok());
    CHECK_EQ(path.revision(), revision);
}

TEST_CASE("procgen.splinePath.pointRotationProducesCompleteLocalOrientation") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 4.f)).ok());
    REQUIRE(path.setPointRotationResult(0, 90.f, 0.f, 0.f).ok());
    REQUIRE(path.setPointRotationResult(1, 90.f, 0.f, 0.f).ok());
    auto frames = path.sampleFramesResult(4, true);
    REQUIRE(frames.ok());
    const auto& frame = frames.value()[2];
    CHECK(std::abs(frame.forwardY + 1.f) < 1e-5f);
    CHECK(std::abs(frame.forwardX * frame.sideX + frame.forwardY * frame.sideY + frame.forwardZ * frame.sideZ) < 1e-5f);
    const auto revision = path.revision();
    CHECK(!path.setPointRotationResult(0, std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    CHECK_EQ(path.revision(), revision);
}

TEST_CASE("procgen.splinePath.travelWrapModesAreDistanceDeterministic") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(10.f, 0.f, 0.f)).ok());
    auto clamped = path.travelResult(12.f, "clamp");
    auto looped  = path.travelResult(12.f, "loop");
    auto bounced = path.travelResult(12.f, "pingPong");
    REQUIRE(clamped.ok());
    REQUIRE(looped.ok());
    REQUIRE(bounced.ok());
    CHECK(std::abs(clamped.value().x - 10.f) < 1e-5f);
    CHECK(std::abs(looped.value().x - 2.f) < 1e-5f);
    CHECK(std::abs(bounced.value().x - 8.f) < 1e-5f);
    auto negative = path.travelResult(-2.f, "loop");
    REQUIRE(negative.ok());
    CHECK(std::abs(negative.value().x - 8.f) < 1e-5f);
    CHECK(!path.travelResult(1.f, "teleport").ok());
    auto frame = path.travelFrameResult(5.f, "clamp", 24);
    REQUIRE(frame.ok());
    CHECK(std::abs(frame.value().sample.x - 5.f) < 1e-5f);
    const float sideLength =
        std::sqrt(frame.value().sideX * frame.value().sideX + frame.value().sideY * frame.value().sideY +
                  frame.value().sideZ * frame.value().sideZ);
    CHECK(std::abs(sideLength - 1.f) < 1e-5f);
    CHECK(std::abs(frame.value().sideX * frame.value().sample.tangentX +
                   frame.value().sideY * frame.value().sample.tangentY +
                   frame.value().sideZ * frame.value().sample.tangentZ) < 1e-5f);
}

TEST_CASE("procgen.splinePath.distributesOwningSamplesWithoutClosedDuplicate") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(9.f, 0.f, 0.f)).ok());
    auto open = path.distributeResult(4, true);
    REQUIRE(open.ok());
    CHECK_EQ(open.value().count(), 4);
    CHECK(std::abs(open.value().sampleResult(1).value().x - 3.f) < 1e-5f);
    CHECK(std::abs(open.value().sampleResult(3).value().x - 9.f) < 1e-5f);
    auto openFrame = open.value().frameResult(1);
    REQUIRE(openFrame.ok());
    CHECK(std::abs(openFrame.value().sample.x - 3.f) < 1e-5f);
    CHECK(std::abs(openFrame.value().sideX * openFrame.value().upX + openFrame.value().sideY * openFrame.value().upY +
                   openFrame.value().sideZ * openFrame.value().upZ) < 1e-5f);
    CHECK(!open.value().sampleResult(4).ok());
    CHECK(!open.value().frameResult(4).ok());

    path.setClosed(true);
    auto closed = path.distributeResult(4, true);
    REQUIRE(closed.ok());
    CHECK_EQ(closed.value().count(), 4);
    CHECK(closed.value().sampleResult(3).value().normalizedDistance < 1.f);
}

TEST_CASE("procgen.splinePath.shapePresetsReplaceAtomically") {
    SplinePath path;
    REQUIRE(path.applyShapePresetResult("circle", 12, 3.f, 0.f).ok());
    CHECK(path.isClosed());
    CHECK_EQ(path.pointCount(), 12);
    auto start = path.evaluateResult(0.f);
    auto end   = path.evaluateResult(1.f);
    REQUIRE(start.ok());
    REQUIRE(end.ok());
    CHECK(std::abs(start.value().x - end.value().x) < 1e-5f);
    const auto revision = path.revision();
    CHECK(!path.applyShapePresetResult("spiral", 1, 3.f, 4.f, 2.f).ok());
    CHECK_EQ(path.revision(), revision);
    CHECK_EQ(path.pointCount(), 12);
    REQUIRE(path.applyShapePresetResult("spiral", 16, 3.f, 4.f, 2.f).ok());
    CHECK(!path.isClosed());
    CHECK_EQ(path.kind(), std::string_view("catmullRom"));
    auto tip = path.evaluateResult(1.f);
    REQUIRE(tip.ok());
    CHECK(std::abs(tip.value().y - 4.f) < 1e-5f);
}

TEST_CASE("procgen.splinePath.buildsRendererNeutralOpenAndClosedPolylines") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(4.f, 0.f, 0.f)).ok());
    auto open = path.polylineResult(5, true, 24);
    REQUIRE(open.ok());
    CHECK_EQ(open.value().count(), 5);
    CHECK(!open.value().isClosed());
    CHECK(std::abs(open.value().pointResult(4).value().x - 4.f) < 1e-6f);
    CHECK(!open.value().pointResult(5).ok());

    path.setClosed(true);
    auto closed = path.polylineResult(8, true, 24);
    REQUIRE(closed.ok());
    CHECK(closed.value().isClosed());
    CHECK_EQ(closed.value().count(), 8);
    const auto first = closed.value().pointResult(0);
    const auto last  = closed.value().pointResult(7);
    REQUIRE(first.ok());
    REQUIRE(last.ok());
    CHECK(std::abs(first.value().x - last.value().x) > 1e-4f);
}

TEST_CASE("procgen.splinePath.disconnectedChunksExcludeGapFromLengthAndPolyline") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(1.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(10.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(11.f, 0.f, 0.f)).ok());
    REQUIRE(path.setPointChunkBreakResult(2, true).ok());
    CHECK_EQ(path.chunkCount(), 2);
    CHECK_EQ(path.segmentCount(), 2);
    auto length = path.lengthResult(16);
    REQUIRE(length.ok());
    CHECK(std::abs(length.value() - 2.f) < 1e-5f);
    auto afterBreak = path.evaluateDistanceResult(1.1f, 16);
    REQUIRE(afterBreak.ok());
    CHECK(std::abs(afterBreak.value().x - 10.1f) < 1e-4f);
    CHECK_EQ(afterBreak.value().chunkIndex, 1);
    auto polyline = path.polylineResult(8, true, 16);
    REQUIRE(polyline.ok());
    CHECK_EQ(polyline.value().chunkCount(), 2);
    CHECK_EQ(polyline.value().chunkPointCount(0), 4);
    CHECK_EQ(polyline.value().chunkPointCount(1), 4);
    CHECK(std::abs(polyline.value().chunkPointResult(0, 3).value().x - 1.f) < 1e-5f);
    CHECK(std::abs(polyline.value().chunkPointResult(1, 0).value().x - 10.f) < 1e-5f);
    REQUIRE(path.setPointChunkBreakResult(2, false).ok());
    CHECK_EQ(path.chunkCount(), 1);
}
