#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
SplinePath splineFor(const std::vector<RoadControlPoint>& points) {
    SplinePath spline;
    REQUIRE(spline.setKindResult("catmullRom").ok());
    for (const auto& point : points) {
        SplinePoint anchor;
        anchor.x               = point.x;
        anchor.y               = point.y;
        anchor.z               = point.z;
        anchor.spanControl     = static_cast<SplineSpanControl>(point.pathControl);
        anchor.circularFromEnd = point.circularFromEnd;
        REQUIRE(spline.addPointResult(anchor).ok());
    }
    return spline;
}

std::vector<RoadControlPoint> circularAnchors(float lastX, float lastZ) {
    auto points           = std::vector<RoadControlPoint>{{271.9253845f, 1.102417f, -542.8322144f},
                                                          {258.5404968f, .3176093f, -524.8121948f},
                                                          {280.6824646f, .3175180f, -471.7786255f},
                                                          {lastX, .3124084f, lastZ}};
    points[1].pathControl = RoadPathControl::Circular;
    return points;
}

RoadNetwork networkFor(const std::vector<RoadControlPoint>& points) {
    RoadNetwork network;
    auto        start = network.addNode(points.front().x, points.front().y, points.front().z);
    auto        end   = network.addNode(points.back().x, points.back().y, points.back().z);
    REQUIRE(start.ok());
    REQUIRE(end.ok());
    REQUIRE(network.addEdge(start.value(), end.value(), points, 2).ok());
    return network;
}

void expectSameGeometryReversed(const SplinePath& before, const SplinePath& after) {
    for (int i = 0; i <= 128; ++i) {
        auto first  = before.evaluateResult(i / 128.f);
        auto second = after.evaluateResult(1.f - i / 128.f);
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        REQUIRE(std::fabs(first.value().x - second.value().x) < .0002f);
        REQUIRE(std::fabs(first.value().y - second.value().y) < .0002f);
        REQUIRE(std::fabs(first.value().z - second.value().z) < .0002f);
    }
}
}  // namespace

TEST_CASE("procgen.road.circular.spec.leadAndArcMatchAuthoredSceneGeometry") {
    auto  points   = circularAnchors(314.4939575f, -506.4530334f);
    auto  spline   = splineFor(points);
    float minimumX = 1000.f;
    for (int i = 0; i <= 1024; ++i) {
        auto sample = spline.evaluateResult((1.f + i / 1024.f) / 3.f);
        REQUIRE(sample.ok());
        minimumX = std::min(minimumX, sample.value().x);
    }
    REQUIRE(std::fabs(minimumX - 238.11505f) < .02f);
    auto lead = spline.evaluateResult((1.f + .1f) / 3.f);
    REQUIRE(lead.ok());
    const double dx        = lead.value().x - points[1].x;
    const double dz        = lead.value().z - points[1].z;
    const double incomingX = points[1].x - points[0].x;
    const double incomingZ = points[1].z - points[0].z;
    REQUIRE(std::fabs(dx * incomingZ - dz * incomingX) < .001);
}

TEST_CASE("procgen.road.circular.spec.chordFallbackProducesLinearCircularSpan") {
    auto points = circularAnchors(280.6824646f, -431.7786255f);
    auto spline = splineFor(points);
    for (int i = 0; i <= 16; ++i) {
        float u      = i / 16.f;
        auto  sample = spline.evaluateResult((1.f + u) / 3.f);
        REQUIRE(sample.ok());
        REQUIRE(std::fabs(sample.value().x - std::lerp(points[1].x, points[2].x, u)) < .0001f);
        REQUIRE(std::fabs(sample.value().z - std::lerp(points[1].z, points[2].z, u)) < .0001f);
    }
}

TEST_CASE("procgen.road.circular.spec.reverseAndSnapshotPreserveBothCurveShapes") {
    for (const auto& points :
         {circularAnchors(314.4939575f, -506.4530334f), circularAnchors(280.6824646f, -431.7786255f)}) {
        auto       network = networkFor(points);
        auto       before  = splineFor(points);
        const auto edgeId  = network.edges().front().id;
        REQUIRE(network.reverseEdge(edgeId).ok());
        auto snapshot = captureRoadNetwork(network);
        REQUIRE(snapshot.ok());
        auto restored = restoreRoadNetwork(snapshot.value());
        REQUIRE(restored.ok());
        REQUIRE(restored.value().edges().size() == 1);
        auto after = splineFor(restored.value().edges().front().controlPoints);
        expectSameGeometryReversed(before, after);
    }
}
