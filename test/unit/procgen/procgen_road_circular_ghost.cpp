#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplineCircularArc.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace eve::procgen;

namespace {
SplinePath ascendingCircularSpan(float scale, float reflection) {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    const float xs[] = {-24, -8, 8, 24};
    const float ys[] = {0, 2, 5, 1};
    const float zs[] = {0, 8, 8, 0};
    for (int i = 0; i < 4; ++i) {
        SplinePoint point;
        point.x = xs[i] * scale;
        point.y = ys[i] * scale;
        point.z = zs[i] * scale * reflection;
        if (i == 1) point.spanControl = SplineSpanControl::Circular;
        REQUIRE(path.addPointResult(point).ok());
    }
    return path;
}
}  // namespace

TEST_CASE("Circular road neighbor geometry and replacement contract") {
    SUB_CASE("circular neighbor contract: tangent direction is preserved and magnitude becomes full 3D chord") {
        detail::CircularArcSampleXZ tangent{0, 0, 3, 4};
        auto                        result = detail::scaleCircularTangentToChord(tangent, 0., {6., 8., 0.});
        REQUIRE(result.ok());
        REQUIRE_EQ(result.value()[0], 6.f);
        REQUIRE_EQ(result.value()[1], 0.f);
        REQUIRE_EQ(result.value()[2], 8.f);

        auto vertical = detail::scaleCircularTangentToChord({0, 0, 0, 0}, 5., {0., 12., 0.});
        REQUIRE(vertical.ok());
        REQUIRE_EQ(vertical.value()[0], 0.f);
        REQUIRE_EQ(vertical.value()[1], 12.f);
        REQUIRE_EQ(vertical.value()[2], 0.f);

        auto mixed = detail::scaleCircularTangentToChord(tangent, 12., {3., 4., 12.});
        REQUIRE(mixed.ok());
        const double length = std::hypot(double(mixed.value()[0]), double(mixed.value()[1]), double(mixed.value()[2]));
        REQUIRE(std::abs(length - 13.) < 1e-5);
        REQUIRE(mixed.value()[0] > 0.f);
        REQUIRE(mixed.value()[1] > 0.f);
        REQUIRE(mixed.value()[2] > 0.f);
    };

    SUB_CASE("circular neighbor contract: zero and unrepresentable geometry is rejected") {
        detail::CircularArcSampleXZ tangent{0, 0, 3, 4};
        REQUIRE(!detail::scaleCircularTangentToChord({}, 0., {1., 0., 0.}).ok());
        REQUIRE(!detail::scaleCircularTangentToChord(tangent, 0., {}).ok());
        REQUIRE(
            !detail::scaleCircularTangentToChord(tangent, std::numeric_limits<double>::infinity(), {1., 0., 0.}).ok());
        REQUIRE(!detail::scaleCircularTangentToChord(tangent, 0., {1e40, 0., 0.}).ok());
        REQUIRE(!detail::scaleCircularTangentToChord({0, 0, 1e-20, 0}, 0., {1., 0., 0.}).ok());
    };

    SUB_CASE("circular neighbor contract: spline integration scales mirrored and resized chords equally") {
        for (float scale : {.5f, 1.f, 2.f}) {
            for (float reflection : {-1.f, 1.f}) {
                auto path     = ascendingCircularSpan(scale, reflection);
                auto midpoint = path.evaluateResult(5.f / 6.f);
                REQUIRE(midpoint.ok());
                const auto&  m             = midpoint.value();
                const double coordinates[] = {m.x, m.y, m.z};
                double       squared       = 0.;
                for (int axis = 0; axis < 3; ++axis) {
                    const double start      = axis == 0 ? 8. * scale : axis == 1 ? 5. * scale : 8. * scale * reflection;
                    const double end        = axis == 0 ? 24. * scale : axis == 1 ? 1. * scale : 0.;
                    const double derivative = 8. * (coordinates[axis] - (start + end) * .5) + (end - start) * .5;
                    const double ghost      = 2. * derivative - (end - start);
                    squared += ghost * ghost;
                }
                REQUIRE(std::abs(std::sqrt(squared) - 16.278821 * scale) < 1e-4);
            }
        }
    };

    SUB_CASE("circular neighbor contract: invalid road control replacement leaves serialized network intact") {
        using namespace eve::procgen::road;
        RoadNetwork network;
        auto        a = network.addNode(-24, 0, 0);
        auto        b = network.addNode(24, 1, 0);
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
        auto after = captureRoadNetwork(network);
        REQUIRE(after.ok());
        REQUIRE_EQ(after.value(), before.value());
    };
}
