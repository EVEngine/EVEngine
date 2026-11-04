// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "math.cpp",
    "math_smooth_max.cpp",
    "math_steering.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_math)
#include "PrivateTestScope.h"
#include "common/Exception.h"
#include "math/Mat4.h"
#include "math/Math.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>

// math.cpp
namespace math_contract {
using namespace eve::math;

bool near(float actual, float expected, float tolerance = 1e-5f) { return std::fabs(actual - expected) <= tolerance; }

TEST_CASE("math.scalar.interpolationAndBoundaryRules") {
    Math math;
    REQUIRE_EQ(math.clamp(5.f, 0.f, 3.f), 3.f);
    CHECK_EQ(math.clamp(-1.f, 3.f, 0.f), 0.f);
    CHECK_EQ(math.lerp(2.f, 10.f, 0.f), 2.f);
    CHECK_EQ(math.lerp(2.f, 10.f, 1.f), 10.f);
    CHECK_EQ(math.lerp(2.f, 10.f, .25f), 4.f);
    CHECK_EQ(math.smoothstep(2.f, 2.f, 1.f), 0.f);
    CHECK_EQ(math.smoothstep(2.f, 2.f, 2.f), 1.f);
    CHECK(near(math.smoothstep(0.f, 1.f, .5f), .5f));
    CHECK(near(math.smootherstep(0.f, 1.f, .5f), .5f));
    CHECK_EQ(math.remap(1.f, 0.f, 0.f, 3.f, 9.f), 3.f);
    CHECK_EQ(math.remap(5.f, 0.f, 10.f, 0.f, 100.f), 50.f);
    CHECK_EQ(math.inverseLerp(0.f, 10.f, 5.f), .5f);
}

TEST_CASE("math.scalar.wrapQuantizeAnglesAndEase") {
    Math            math;
    constexpr float pi = 3.14159265358979323846f;
    REQUIRE(near(math.degToRad(180.f), pi));
    CHECK(near(math.radToDeg(pi), 180.f, 1e-4f));
    CHECK_EQ(math.sign(-1.f), -1.f);
    CHECK_EQ(math.sign(0.f), 0.f);
    CHECK_EQ(math.sign(1.f), 1.f);
    CHECK(near(math.fract(3.25f), .25f));
    CHECK_EQ(math.approach(0.f, 10.f, 3.f), 3.f);
    CHECK_EQ(math.wrap(5.f, 0.f, 4.f), 1.f);
    CHECK_EQ(math.pingPong(3.5f, 2.f), .5f);
    CHECK_EQ(math.step(.5f, .49f), 0.f);
    CHECK_EQ(math.step(.5f, .5f), 1.f);
    CHECK_EQ(math.quantize(13.f, 5.f), 10.f);
    CHECK_EQ(math.snap(13.f, 5.f), 15.f);
    CHECK_EQ(math.quantize(1.3f, 0.f), 1.3f);
    CHECK_EQ(math.ease(.5f, "linear"), .5f);
    CHECK(math.ease(.5f, "inQuad") < .5f);
    CHECK(math.ease(.5f, "outQuad") > .5f);
    bool rejected = false;
    try {
        (void)math.ease(.5f, "unknown");
    } catch (const eve::Exception&) {
        rejected = true;
    }
    CHECK(rejected);
}

TEST_CASE("math.geometry.componentsAndZeroSafeNormalization") {
    Math math;
    REQUIRE_EQ(math.length2(3.f, 4.f), 5.f);
    CHECK_EQ(math.length3(1.f, 2.f, 2.f), 3.f);
    CHECK_EQ(math.distance2(0.f, 0.f, 3.f, 4.f), 5.f);
    CHECK_EQ(math.distance3(0.f, 0.f, 0.f, 1.f, 2.f, 2.f), 3.f);
    CHECK_EQ(math.dot2(1.f, 0.f, 0.f, 1.f), 0.f);
    CHECK_EQ(math.dot3(1.f, 0.f, 0.f, 0.f, 1.f, 0.f), 0.f);
    CHECK_EQ(math.cross2(1.f, 0.f, 0.f, 1.f), 1.f);
    CHECK(near(math.normalize2X(3.f, 4.f), .6f));
    CHECK(near(math.normalize2Y(3.f, 4.f), .8f));
    CHECK_EQ(math.normalize3X(0.f, 0.f, 0.f), 0.f);
    CHECK(near(math.normalize3Y(0.f, 3.f, 4.f), .6f));
    CHECK(near(math.rotate2X(1.f, 0.f, 1.570796327f), 0.f));
    CHECK(near(math.rotate2Y(1.f, 0.f, 1.570796327f), 1.f));
}

TEST_CASE("math.geometry.inclusiveShapesAndRayParameters") {
    Math math;
    REQUIRE(math.pointInCircle(1.f, 0.f, 0.f, 0.f, 1.f));
    CHECK(!math.pointInCircle(2.f, 0.f, 0.f, 0.f, 1.f));
    CHECK(math.pointInRect(2.f, 2.f, 0.f, 0.f, 2.f, 2.f));
    CHECK(!math.pointInRect(3.f, 2.f, 0.f, 0.f, 2.f, 2.f));
    CHECK(math.circlesOverlap(0.f, 0.f, 1.f, 2.f, 0.f, 1.f));
    CHECK(!math.circlesOverlap(0.f, 0.f, 1.f, 3.f, 0.f, 1.f));
    CHECK(math.segmentsIntersect(0.f, 0.f, 2.f, 2.f, 0.f, 2.f, 2.f, 0.f));
    CHECK(near(math.raycastCircle2(0.f, 0.f, 1.f, 0.f, 5.f, 0.f, 1.f), 4.f));
    CHECK(math.raycastCircle2(0.f, 0.f, -1.f, 0.f, 5.f, 0.f, 1.f) < 0.f);
    CHECK(near(math.raycastRect2(0.f, 0.f, 1.f, 0.f, 2.f, -1.f, 2.f, 2.f), 2.f));
    CHECK(math.pointInSphere(1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f));
    CHECK(math.pointInBox(1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f));
    CHECK(near(math.raycastSphere(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f), 4.f));
    CHECK(near(math.raycastBox(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 2.f, -1.f, -1.f, 4.f, 1.f, 1.f), 2.f));
    CHECK(near(math.raycastPlane(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f), 5.f));
    CHECK(math.raycastPlane(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f) < 0.f);
}

TEST_CASE("math.objects.ownedVectorsAndMatricesTransformValues") {
    Math                  math;
    std::unique_ptr<Vec2> point(math.newVec2(1.f, 2.f));
    std::unique_ptr<Vec3> x(math.newVec3(1.f, 0.f, 0.f));
    std::unique_ptr<Vec3> y(math.newVec3(0.f, 1.f, 0.f));
    std::unique_ptr<Vec3> z(x->cross(y.get()));
    CHECK_EQ(z->getZ(), 1.f);
    std::unique_ptr<Mat4> translation(math.newMat4Translation(10.f, 20.f, 0.f));
    std::unique_ptr<Vec2> moved(translation->transformPoint2(point.get()));
    REQUIRE_EQ(moved->getX(), 11.f);
    CHECK_EQ(moved->getY(), 22.f);
    CHECK_EQ(translation->get(12), 10.f);
}

TEST_CASE("math.random.reseedRepeatsSequenceAndNoiseIsPure") {
    Math math;
    math.setRandomSeed(42u);
    const float first  = math.random();
    const float ranged = math.randomRange(10.f, 20.f);
    const int   die    = math.randomInt(1, 6);
    math.setRandomSeed(42u);
    REQUIRE(math.random() == first);
    CHECK_EQ(math.randomRange(10.f, 20.f), ranged);
    CHECK_EQ(math.randomInt(1, 6), die);
    CHECK(first >= 0.f);
    CHECK(first < 1.f);
    CHECK(ranged >= 10.f);
    CHECK(ranged <= 20.f);
    CHECK(die >= 1);
    CHECK(die <= 6);
    CHECK_EQ(math.hash2(3.f, 7.f), math.hash2(3.f, 7.f));
    CHECK_EQ(math.noise2(.2f, .7f), math.noise2(.2f, .7f));
    const float noise = math.noise2(.2f, .7f);
    CHECK(noise >= 0.f);
    CHECK(noise <= 1.f);
}

TEST_CASE("math.curves.endpointsAndLinearControlPoints") {
    Math math;
    REQUIRE_EQ(math.bezierQuadratic(0.f, 1.f, 5.f, 9.f), 1.f);
    CHECK_EQ(math.bezierQuadratic(1.f, 1.f, 5.f, 9.f), 9.f);
    CHECK(near(math.bezierQuadratic(.5f, 1.f, 5.f, 9.f), 5.f));
    CHECK_EQ(math.bezierCubic(0.f, 0.f, 3.f, 6.f, 9.f), 0.f);
    CHECK_EQ(math.bezierCubic(1.f, 0.f, 3.f, 6.f, 9.f), 9.f);
    CHECK(near(math.bezierCubic(.5f, 0.f, 3.f, 6.f, 9.f), 4.5f));
}

}  // namespace math_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_math_smooth_max) || \
    defined(EVE_TEST_ORIGIN_math_steering)
#include "PrivateTestScope.h"
#include "common/SmoothMax.h"
#include "math/Math.h"
#include "math/Steering.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace math_advanced_contract {

// math_smooth_max.cpp
TEST_CASE("math.smoothMax.exactOutsideBandAndSymmetricInside") {
    using eve::math::smoothMax;
    CHECK_EQ(smoothMax(3.f, 7.f, 0.f), 7.f);
    CHECK_EQ(smoothMax(3.f, 8.f, 4.f), 8.f);
    REQUIRE_EQ(smoothMax(10.f, 10.f, 4.f), 11.f);
    for (int i = -40; i <= 40; ++i) {
        const float a     = static_cast<float>(i) * .1f;
        const float value = smoothMax(a, 2.f, 4.f);
        CHECK_EQ(value, smoothMax(2.f, a, 4.f));
        CHECK(value >= std::max(a, 2.f));
        CHECK(value <= std::max(a, 2.f) + 1.f);
    }
    const float largest = std::numeric_limits<float>::max();
    CHECK_EQ(smoothMax(largest, -largest, largest), largest);
    CHECK(std::isinf(smoothMax(largest, largest, largest)));
}

// math_steering.cpp
TEST_CASE("math.steering.seekFleeArriveHaveExplicit2DAnd3DOutputs") {
    namespace steering = eve::math::steering;
    const auto seek2   = steering::seek(steering::Vector2{}, {3.f, 4.f}, 10.f);
    REQUIRE(std::abs(seek2.x - 6.f) < 1e-4f);
    CHECK(std::abs(seek2.y - 8.f) < 1e-4f);
    const auto flee2 = steering::flee(steering::Vector2{}, {3.f, 4.f}, 5.f);
    CHECK(std::abs(flee2.x + 3.f) < 1e-4f);
    CHECK(std::abs(flee2.y + 4.f) < 1e-4f);
    CHECK_EQ(steering::arrive(steering::Vector2{}, {.1f, 0.f}, 5.f, 4.f, .5f).x, 0.f);
    const auto seek3 = steering::seek(steering::Vector3{}, {2.f, 3.f, 6.f}, 7.f);
    CHECK_EQ(seek3.x, 2.f);
    CHECK_EQ(seek3.y, 3.f);
    CHECK_EQ(seek3.z, 6.f);
    CHECK_EQ(steering::flee(steering::Vector3{}, {0.f, 0.f, 2.f}, 3.f).z, -3.f);
}

TEST_CASE("math.steering.pathAndInvalidInputsHaveStableFallbacks") {
    namespace steering = eve::math::steering;
    const std::array path{steering::Vector2{0.f, 0.f}, steering::Vector2{5.f, 0.f}};
    REQUIRE_EQ(steering::pathTarget({}, path, 0, .2f), 1);
    CHECK_EQ(steering::pathTarget(steering::Vector2{}, std::array<steering::Vector2, 0>{}, 0, .2f), -1);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_EQ(steering::seek(steering::Vector2{}, {1.f, 1.f}, -1.f).x, 0.f);
    CHECK_EQ(steering::seek(steering::Vector3{}, {1.f, nan, 1.f}, 1.f).z, 0.f);
    CHECK_EQ(steering::arrive(steering::Vector2{}, {1.f, 1.f}, 1.f, 0.f, 2.f).x, 0.f);
    eve::math::Math                  math;
    std::unique_ptr<eve::math::Vec2> adapted(math.steeringSeek2(0.f, 0.f, 3.f, 4.f, 10.f));
    CHECK(std::abs(adapted->getX() - 6.f) < 1e-4f);
}

}  // namespace math_advanced_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_math)
#include "math/ConvexHull.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <random>

namespace convex_hull_contract {
using namespace eve::math;

TEST_CASE("math.convexHull2D.boundaryAndDegeneracies") {
    const std::vector<HullPoint2> points{{1, 1}, {0, 0}, {1, 0}, {0, 1}, {.5f, .5f}, {0, 0}, {.5f, 0}};
    auto                          result = convexHull2D(points);
    REQUIRE(result.ok());
    const std::vector<HullPoint2> expected{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    CHECK(result.value().vertices == expected);
    CHECK(result.value().dimension == HullDimension::Plane);
    auto empty = convexHull2D({});
    REQUIRE(empty.ok());
    CHECK(empty.value().dimension == HullDimension::Empty);
    const std::vector<HullPoint2> line{{0, 0}, {1, 1}, {2, 2}, {1, 1}};
    auto                          segment = convexHull2D(line);
    REQUIRE(segment.ok());
    CHECK_EQ(segment.value().vertices.size(), 2U);
    CHECK(segment.value().dimension == HullDimension::Segment);
    auto point = convexHull2D(std::vector<HullPoint2>{{1, 2}, {1, 2}});
    REQUIRE(point.ok());
    CHECK(point.value().dimension == HullDimension::Point);
}

TEST_CASE("math.convexHull3D.degenerateInputs") {
    auto empty = convexHull3D({});
    REQUIRE(empty.ok());
    CHECK(empty.value().dimension == HullDimension::Empty);
    auto point = convexHull3D(std::vector<HullPoint3>{{1, 2, 3}, {1, 2, 3}});
    REQUIRE(point.ok());
    CHECK(point.value().dimension == HullDimension::Point);
    auto line = convexHull3D(std::vector<HullPoint3>{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}});
    REQUIRE(line.ok());
    CHECK(line.value().dimension == HullDimension::Segment);
    CHECK_EQ(line.value().vertices.size(), 2U);
    CHECK(line.value().faces.empty());
    auto plane = convexHull3D(std::vector<HullPoint3>{{0, 0, 0}, {1, 0, 1}, {1, 1, 2}, {0, 1, 1}, {.5f, .5f, 1}});
    REQUIRE(plane.ok());
    CHECK(plane.value().dimension == HullDimension::Plane);
    CHECK_EQ(plane.value().vertices.size(), 4U);
    CHECK_EQ(plane.value().faces.size(), 2U);
    auto nearPlane =
        convexHull3D(std::vector<HullPoint3>{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {.5f, .5f, 1e-8f}}, 1e-6);
    REQUIRE(nearPlane.ok());
    CHECK(nearPlane.value().dimension == HullDimension::Plane);
}

using DPoint = std::array<double, 3>;
DPoint difference(const HullPoint3& a, const HullPoint3& b) {
    return {double(a[0]) - b[0], double(a[1]) - b[1], double(a[2]) - b[2]};
}
DPoint cross(const DPoint& a, const DPoint& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const DPoint& a, const DPoint& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

double checkSolid(const std::vector<HullPoint3>& points) {
    auto result = convexHull3D(points);
    REQUIRE(result.ok());
    const auto& hull = result.value();
    REQUIRE(hull.dimension == HullDimension::Volume);
    std::map<std::pair<std::size_t, std::size_t>, int> edges;
    double                                             volume = 0;
    const auto                                         origin = hull.vertices.front();
    for (const auto& f : hull.faces) {
        for (auto index : f) REQUIRE(index < hull.vertices.size());
        const auto a = hull.vertices[f[0]], b = hull.vertices[f[1]], c = hull.vertices[f[2]];
        const auto n = cross(difference(b, a), difference(c, a));
        REQUIRE(dot(n, n) > 0);
        for (const auto& p : points) CHECK(dot(n, difference(p, a)) <= 1e-7);
        volume += dot(difference(a, origin), cross(difference(b, origin), difference(c, origin))) / 6;
        for (std::size_t i = 0; i < 3; ++i) ++edges[{f[i], f[(i + 1) % 3]}];
    }
    for (const auto& entry : edges) {
        CHECK_EQ(entry.second, 1);
        const auto reverse = edges.find({entry.first.second, entry.first.first});
        REQUIRE(reverse != edges.end());
        CHECK_EQ(reverse->second, 1);
    }
    CHECK_EQ(hull.vertices.size() + hull.faces.size(), edges.size() / 2 + 2);
    CHECK(volume > 0);
    auto reversed = points;
    std::reverse(reversed.begin(), reversed.end());
    auto again = convexHull3D(reversed);
    REQUIRE(again.ok());
    CHECK(again.value().vertices == hull.vertices);
    CHECK(again.value().faces == hull.faces);
    return volume;
}

TEST_CASE("math.convexHull3D.closedOrientedCubeAndTetrahedron") {
    std::vector<HullPoint3> cube;
    for (float x : {0.f, 1.f})
        for (float y : {0.f, 1.f})
            for (float z : {0.f, 1.f}) cube.push_back({x, y, z});
    cube.push_back({.5f, .5f, .5f});
    cube.push_back({0, 0, 0});
    CHECK(std::abs(checkSolid(cube) - 1) < 1e-10);
    CHECK(std::abs(checkSolid({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}) - 1.0 / 6) < 1e-10);
}

TEST_CASE("math.convexHull3D.randomCloudTopology") {
    std::mt19937                          random(42);
    std::uniform_real_distribution<float> coordinate(-1, 1);
    for (int trial = 0; trial < 30; ++trial) {
        std::vector<HullPoint3> points;
        for (int i = 0; i < 60; ++i) points.push_back({coordinate(random), coordinate(random), coordinate(random)});
        CHECK(checkSolid(points) > 0);
    }
}

TEST_CASE("math.convexHull.invalidArgumentsAndScale") {
    auto bad2 = convexHull2D(std::vector<HullPoint2>{{0, std::numeric_limits<float>::quiet_NaN()}});
    REQUIRE(!bad2.ok());
    CHECK(bad2.error()->code() == eve::DiagnosticCode::InvalidArgument);
    auto bad3 = convexHull3D(std::vector<HullPoint3>{{0, 0, std::numeric_limits<float>::infinity()}});
    REQUIRE(!bad3.ok());
    CHECK(bad3.error()->code() == eve::DiagnosticCode::InvalidArgument);
    for (double tolerance : {-1., 1., std::numeric_limits<double>::quiet_NaN()}) {
        auto bad = convexHull3D({}, tolerance);
        REQUIRE(!bad.ok());
        CHECK(bad.error()->path() == "tolerance");
    }
    for (float scale : {1e-30f, 1e30f}) {
        auto hull = convexHull3D(std::vector<HullPoint3>{{0, 0, 0}, {scale, 0, 0}, {0, scale, 0}, {0, 0, scale}});
        REQUIRE(hull.ok());
        CHECK(hull.value().dimension == HullDimension::Volume);
        CHECK_EQ(hull.value().vertices.size(), 4U);
        CHECK_EQ(hull.value().faces.size(), 4U);
    }
}
}  // namespace convex_hull_contract
#endif
