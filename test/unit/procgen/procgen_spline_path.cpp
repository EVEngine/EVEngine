// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_spline_path.cpp"
  ]
}
*/
// clang-format on

#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

namespace {
using namespace eve::procgen;

SplinePoint point(float x, float y, float z) {
    SplinePoint result;
    result.x = x;
    result.y = y;
    result.z = z;
    return result;
}

// Distance contract: a polyline measures each segment in world units;
// evaluateDistance interpolates along cumulative length, not point index.
TEST_CASE("procgen.spline.spec.linear_arc_length_and_distance") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(3.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(3.f, 4.f, 0.f)).ok());
    auto length = path.lengthResult();
    auto half   = path.evaluateDistanceResult(3.5f);
    REQUIRE(length.ok());
    REQUIRE(half.ok());
    CHECK(std::abs(length.value() - 7.f) < 1e-5f);
    CHECK(std::abs(half.value().x - 3.f) < 1e-4f);
    CHECK(std::abs(half.value().y - .5f) < 1e-4f);
    CHECK(std::abs(half.value().normalizedDistance - .5f) < 1e-5f);
}

// Curve contract: cubic and quadratic Bézier use their explicit authored
// handles; closest-point queries return a point on the same curve.
TEST_CASE("procgen.spline.spec.bezier_interpolation_and_closest") {
    SplinePoint start = point(0.f, 0.f, 0.f);
    SplinePoint end   = point(2.f, 0.f, 0.f);
    start.outY        = 2.f;
    end.inY           = 2.f;
    SplinePath cubic;
    REQUIRE(cubic.setKindResult("bezier").ok());
    REQUIRE(cubic.addPointResult(start).ok());
    REQUIRE(cubic.addPointResult(end).ok());
    auto midpoint = cubic.evaluateResult(.5f);
    auto nearest  = cubic.closestPointResult(1.f, 1.4f, 0.f, 64);
    REQUIRE(midpoint.ok());
    REQUIRE(nearest.ok());
    CHECK(std::abs(midpoint.value().x - 1.f) < 1e-5f);
    CHECK(midpoint.value().y > 1.49f);
    CHECK(std::abs(nearest.value().x - 1.f) < .05f);
    SplinePath quadratic;
    REQUIRE(quadratic.setKindResult("quadraticBezier").ok());
    REQUIRE(quadratic.addPointResult(start).ok());
    REQUIRE(quadratic.addPointResult(end).ok());
    auto quadMid = quadratic.evaluateResult(.5f);
    REQUIRE(quadMid.ok());
    CHECK(std::abs(quadMid.value().x - 1.f) < 1e-5f);
    CHECK(std::abs(quadMid.value().y - 1.f) < 1e-5f);
}

// Authoring contract: malformed kind, nonfinite control point, invalid scale
// and infinite rotation reject without changing count or revision.
TEST_CASE("procgen.spline.spec.invalid_authoring_is_atomic") {
    SplinePath path;
    const auto emptyRevision = path.revision();
    CHECK(!path.setKindResult("nurbs").ok());
    CHECK(!path.evaluateResult(.5f).ok());
    CHECK_EQ(path.revision(), emptyRevision);
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    CHECK(!path.addPointResult(point(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f)).ok());
    CHECK_EQ(path.pointCount(), 1);
    const auto authoredRevision = path.revision();
    CHECK(!path.setPointProfileResult(0, 0.f, 0.f, 1.f).ok());
    CHECK(!path.setPointRotationResult(0, std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    CHECK_EQ(path.revision(), authoredRevision);
}

// Frame contract: transported side and up vectors remain unit length and
// perpendicular to the tangent. A closed loop has no orientation seam.
TEST_CASE("procgen.spline.spec_transport_basis_and_closed_seam") {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    path.setClosed(true);
    REQUIRE(path.addPointResult(point(-2.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, 1.f, 2.f)).ok());
    REQUIRE(path.addPointResult(point(2.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, -1.f, -2.f)).ok());
    auto frames = path.sampleFramesResult(48, true);
    REQUIRE(frames.ok());
    REQUIRE_EQ(frames.value().size(), std::size_t{49});
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
    const auto& first = frames.value().front();
    const auto& last  = frames.value().back();
    CHECK(std::abs(first.sample.x - last.sample.x) < 1e-6f);
    CHECK(std::abs(first.sideX - last.sideX) < 1e-6f);
    CHECK(std::abs(first.sideY - last.sideY) < 1e-6f);
    CHECK(std::abs(first.upZ - last.upZ) < 1e-6f);
    CHECK(std::abs(last.sample.normalizedDistance - 1.f) < 1e-6f);
}

// Profile contract: roll and cross-section scales interpolate with the
// control-point parameter; explicit point rotation affects the full frame.
TEST_CASE("procgen.spline.spec_profile_and_orientation") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 4.f)).ok());
    REQUIRE(path.setPointProfileResult(0, 0.f, 1.f, 1.f).ok());
    REQUIRE(path.setPointProfileResult(1, 90.f, 3.f, 2.f).ok());
    auto middle = path.evaluateResult(.5f);
    REQUIRE(middle.ok());
    CHECK(std::abs(middle.value().rollDegrees - 45.f) < 1e-6f);
    CHECK(std::abs(middle.value().scaleX - 2.f) < 1e-6f);
    CHECK(std::abs(middle.value().scaleY - 1.5f) < 1e-6f);
    REQUIRE(path.setPointRotationResult(0, 90.f, 0.f, 0.f).ok());
    REQUIRE(path.setPointRotationResult(1, 90.f, 0.f, 0.f).ok());
    auto frames = path.sampleFramesResult(4, true);
    REQUIRE(frames.ok());
    CHECK(std::abs(frames.value()[2].forwardY + 1.f) < 1e-5f);
}

// Travel and distribution contract: clamp, loop and ping-pong map distance
// deterministically. Owning distribution reports range failures and omits a
// duplicate closed endpoint.
TEST_CASE("procgen.spline.spec_travel_and_owning_distribution") {
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
    CHECK(!path.travelResult(1.f, "teleport").ok());
    auto open = path.distributeResult(4, true);
    REQUIRE(open.ok());
    CHECK_EQ(open.value().count(), 4);
    CHECK(!open.value().sampleResult(4).ok());
    path.setClosed(true);
    auto closed = path.distributeResult(4, true);
    REQUIRE(closed.ok());
    CHECK(closed.value().sampleResult(3).value().normalizedDistance < 1.f);
}

// Preset and topology contract: a rejected preset leaves previous authored
// points intact; a disconnected chunk excludes the physical gap from length.
TEST_CASE("procgen.spline.spec_presets_chunks_and_polyline") {
    SplinePath path;
    REQUIRE(path.applyShapePresetResult("circle", 12, 3.f, 0.f).ok());
    CHECK(path.isClosed());
    CHECK_EQ(path.pointCount(), 12);
    const auto revision = path.revision();
    CHECK(!path.applyShapePresetResult("spiral", 1, 3.f, 4.f, 2.f).ok());
    CHECK_EQ(path.revision(), revision);
    CHECK_EQ(path.pointCount(), 12);
    path.clear();
    path.setClosed(false);
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
    auto polyline = path.polylineResult(8, true, 16);
    REQUIRE(polyline.ok());
    CHECK_EQ(polyline.value().chunkCount(), 2);
    CHECK_EQ(polyline.value().chunkPointCount(0), 4);
    CHECK_EQ(polyline.value().chunkPointCount(1), 4);
}
}  // namespace
