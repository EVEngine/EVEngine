// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_road_bundled_circular.cpp"
  ]
}
*/
// clang-format on

#include <array>
#include <cmath>
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
SplinePath makeBundledPath(const std::vector<RoadControlPoint>& input) {
    SplinePath path;
    path.setKindResult("catmullRom").expect("test path kind");
    for (const auto& p : input) {
        SplinePoint q;
        q.x               = p.x;
        q.y               = p.y;
        q.z               = p.z;
        q.spanControl     = static_cast<SplineSpanControl>(p.pathControl);
        q.circularFromEnd = p.circularFromEnd;
        path.addPointResult(q).expect("test path anchor");
    }
    return path;
}
}  // namespace

TEST_CASE("procgen.road.authoring.bundledCircularLeadMatchesPublicSdkAndRoundTrip") {
    // Original bundled scene main / Default Road 0045, reflected into native Z.
    // Public SDK probe: circular body radius 24.80765 m; minimum X 238.11505 m.
    const std::vector<RoadControlPoint> anchors = {{271.9253845f, 1.102417f, -542.8322144f},
                                                   {258.5404968f, .3176093f, -524.8121948f},
                                                   {280.6824646f, .3175180f, -471.7786255f},
                                                   {314.4939575f, .3124084f, -506.4530334f}};
    RoadNetwork                         network;
    auto                                a = network.addNode(anchors.front().x, anchors.front().y, anchors.front().z);
    auto                                b = network.addNode(anchors.back().x, anchors.back().y, anchors.back().z);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto points           = anchors;
    points[1].pathControl = RoadPathControl::Circular;
    auto edge             = network.addEdge(a.value(), b.value(), points, 2);
    REQUIRE(edge.ok());
    auto  original = makeBundledPath(points);
    float minimumX = 1000;
    for (int i = 0; i <= 1024; ++i) {
        auto sample = original.evaluateResult((1.f + i / 1024.f) / 3.f);
        REQUIRE(sample.ok());
        minimumX = std::min(minimumX, sample.value().x);
    }
    REQUIRE(std::abs(minimumX - 238.11505f) < .02f);
    // A measured point on the straight lead, before the circle starts.
    const auto lead = original.evaluateResult((1.f + .1f) / 3.f);
    REQUIRE(lead.ok());
    const double dx = lead.value().x - points[1].x, dz = lead.value().z - points[1].z;
    const double incomingX = points[1].x - points[0].x, incomingZ = points[1].z - points[0].z;
    REQUIRE(std::abs(dx * incomingZ - dz * incomingX) < .001);
    REQUIRE(network.reverseEdge(edge.value()).ok());
    auto snapshot = captureRoadNetwork(network);
    REQUIRE(snapshot.ok());
    auto restored = restoreRoadNetwork(snapshot.value());
    REQUIRE(restored.ok());
    auto reversed = makeBundledPath(restored.value().edges()[0].controlPoints);
    for (int i = 0; i <= 256; ++i) {
        auto p = original.evaluateResult(i / 256.f);
        auto q = reversed.evaluateResult(1.f - i / 256.f);
        REQUIRE(p.ok());
        REQUIRE(q.ok());
        REQUIRE(std::abs(p.value().x - q.value().x) < .0002f);
        REQUIRE(std::abs(p.value().y - q.value().y) < .0002f);
        REQUIRE(std::abs(p.value().z - q.value().z) < .0002f);
    }
}

TEST_CASE("procgen.road.authoring.bundledNegativeLeadUsesSdkChordAndReversesNeighbors") {
    // SDK derivative exit-south emits exactly the chord midpoint in its circular span.
    std::vector<RoadControlPoint> points = {{271.9253845f, 1.102417f, -542.8322144f},
                                            {258.5404968f, .3176093f, -524.8121948f},
                                            {280.6824646f, .3175180f, -471.7786255f},
                                            {280.6824646f, .3175180f, -431.7786255f}};
    points[1].pathControl                = RoadPathControl::Circular;
    RoadNetwork network;
    auto        a = network.addNode(points.front().x, points.front().y, points.front().z);
    auto        b = network.addNode(points.back().x, points.back().y, points.back().z);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge = network.addEdge(a.value(), b.value(), points, 2);
    REQUIRE(edge.ok());
    auto original = makeBundledPath(points);
    for (int i = 0; i <= 32; ++i) {
        const float u = i / 32.f;
        auto        p = original.evaluateResult((1 + u) / 3);
        REQUIRE(p.ok());
        REQUIRE(std::abs(p.value().x - std::lerp(points[1].x, points[2].x, u)) < .0001f);
        REQUIRE(std::abs(p.value().z - std::lerp(points[1].z, points[2].z, u)) < .0001f);
    }
    auto incoming = original.evaluateResult(1.f / 6);
    REQUIRE(incoming.ok());
    REQUIRE(std::abs((incoming.value().x - points[0].x) * (points[1].z - points[0].z) -
                     (incoming.value().z - points[0].z) * (points[1].x - points[0].x)) < .001f);
    REQUIRE(network.reverseEdge(edge.value()).ok());
    auto state = captureRoadNetwork(network);
    REQUIRE(state.ok());
    auto restored = restoreRoadNetwork(state.value());
    REQUIRE(restored.ok());
    auto reversed = makeBundledPath(restored.value().edges()[0].controlPoints);
    for (int i = 0; i <= 256; ++i) {
        auto p = original.evaluateResult(i / 256.f);
        auto q = reversed.evaluateResult(1 - i / 256.f);
        REQUIRE(p.ok());
        REQUIRE(q.ok());
        REQUIRE(std::abs(p.value().x - q.value().x) < .0002f);
        REQUIRE(std::abs(p.value().y - q.value().y) < .0002f);
        REQUIRE(std::abs(p.value().z - q.value().z) < .0002f);
    }
}
