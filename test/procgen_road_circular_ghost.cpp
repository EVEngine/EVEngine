#include <cmath>
#include <limits>
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplineCircularArc.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.road.authoring.circularNeighborUsesMeasuredThreeDimensionalChordMagnitude") {
    for (float scale : {.5f, 1.f, 2.f})
        for (float reflection : {-1.f, 1.f}) {
            SplinePath path;
            REQUIRE(path.setKindResult("catmullRom").ok());
            const float xs[] = {-24, -8, 8, 24}, ys[] = {0, 2, 5, 1}, zs[] = {0, 8, 8, 0};
            for (int i = 0; i < 4; ++i) {
                SplinePoint p;
                p.x = xs[i] * scale;
                p.y = ys[i] * scale;
                p.z = zs[i] * scale * reflection;
                if (i == 1) p.spanControl = SplineSpanControl::Circular;
                REQUIRE(path.addPointResult(p).ok());
            }
            auto midpoint = path.evaluateResult(5.f / 6.f);
            REQUIRE(midpoint.ok());
            // Recover the Hermite start derivative from u=.5, then its virtual predecessor vector.
            // Actual zero-bump reference meshes measured lengths 8.139410 / 16.278821 / 32.557641.
            const auto&  m             = midpoint.value();
            const double coordinates[] = {m.x, m.y, m.z};
            double       square        = 0.;
            for (int axis = 0; axis < 3; ++axis) {
                const double a          = axis == 0 ? 8. * scale : axis == 1 ? 5. * scale : 8. * scale * reflection;
                const double b          = axis == 0 ? 24. * scale : axis == 1 ? 1. * scale : 0.;
                const double derivative = 8. * (coordinates[axis] - (a + b) * .5) + (b - a) * .5;
                const double ghost      = 2. * derivative - (b - a);
                square += ghost * ghost;
            }
            REQUIRE(std::fabs(std::sqrt(square) - 16.278821 * scale) < 1e-4);
        }
}

TEST_CASE("procgen.road.authoring.circularNeighborRejectsUnrepresentableGeometry") {
    detail::CircularArcSampleXZ tangent{0, 0, 3, 4};
    auto                        scaled = detail::scaleCircularTangentToChord(tangent, 0., {6., 8., 0.});
    REQUIRE(scaled.ok());
    REQUIRE(scaled.value()[0] == 6.f);
    REQUIRE(scaled.value()[1] == 0.f);
    REQUIRE(scaled.value()[2] == 8.f);
    REQUIRE(!detail::scaleCircularTangentToChord({}, 0., {1., 0., 0.}).ok());
    REQUIRE(!detail::scaleCircularTangentToChord(tangent, 0., {}).ok());
    REQUIRE(!detail::scaleCircularTangentToChord(tangent, std::numeric_limits<double>::infinity(), {1., 0., 0.}).ok());
    REQUIRE(!detail::scaleCircularTangentToChord(tangent, 0., {1e40, 0., 0.}).ok());
    using namespace eve::procgen::road;
    RoadNetwork network;
    auto        a = network.addNode(-24, 0, 0), b = network.addNode(24, 1, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    std::vector<RoadControlPoint> points = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
    points[1].pathControl                = RoadPathControl::Circular;
    auto edge                            = network.addEdge(a.value(), b.value(), points, 2);
    REQUIRE(edge.ok());
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    points[1].y = std::numeric_limits<float>::max();
    points[2].y = -std::numeric_limits<float>::max();
    REQUIRE(!network.setEdgeControlPoints(edge.value(), points).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
}
