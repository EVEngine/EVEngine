#include "math/Steering.h"
#include "math/Math.h"
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <array>
#include <cmath>
#include <limits>
#include <memory>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace steering = eve::math::steering;

TEST_CASE("math.steering2.seekFleeAndArrive") {
    const auto seek = steering::seek(steering::Vector2{}, {3.f, 4.f}, 10.f);
    CHECK(std::abs(seek.x - 6.f) < .001f);
    CHECK(std::abs(seek.y - 8.f) < .001f);

    const auto flee = steering::flee(steering::Vector2{}, {3.f, 4.f}, 5.f);
    CHECK(flee.x < 0.f);
    CHECK(flee.y < 0.f);
    CHECK_EQ(steering::arrive(steering::Vector2{}, {.1f, 0.f}, 5.f, 4.f, .5f).x,
             0.f);
}

TEST_CASE("math.steering3.seekFleeAndArrive") {
    const auto seek = steering::seek(steering::Vector3{}, {2.f, 3.f, 6.f}, 7.f);
    CHECK(std::abs(seek.x - 2.f) < .001f);
    CHECK(std::abs(seek.y - 3.f) < .001f);
    CHECK(std::abs(seek.z - 6.f) < .001f);

    const auto flee = steering::flee(steering::Vector3{}, {0.f, 0.f, 2.f}, 3.f);
    CHECK_EQ(flee.x, 0.f);
    CHECK_EQ(flee.y, 0.f);
    CHECK_EQ(flee.z, -3.f);
    CHECK_EQ(steering::arrive(steering::Vector3{}, {0.f, .1f, 0.f}, 5.f, 4.f, .5f).y,
             0.f);
}

TEST_CASE("math.steering2.pathSeparationAndAvoidance") {
    const std::array points{steering::Vector2{0.f, 0.f}, steering::Vector2{5.f, 0.f},
                            steering::Vector2{10.f, 0.f}};
    CHECK_EQ(steering::pathTarget({}, points, 0, .2f), 1);

    const std::array neighbors{steering::Vector2{1.f, 0.f}, steering::Vector2{-3.f, 0.f}};
    const auto       separated = steering::separation({}, neighbors, 2.f, 4.f);
    CHECK(separated.x < 0.f);

    const auto avoided = steering::avoid(steering::Vector2{}, steering::Vector2{1.f, 0.f},
                                         steering::Vector2{2.f, 0.f}, 1.f, 2.f, 3.f);
    CHECK(std::isfinite(avoided.x));
}

TEST_CASE("math.steering3.pathSeparationAndAvoidance") {
    const std::array points{steering::Vector3{0.f, 0.f, 0.f},
                            steering::Vector3{0.f, 0.f, 5.f},
                            steering::Vector3{0.f, 0.f, 10.f}};
    CHECK_EQ(steering::pathTarget({}, points, 0, .2f), 1);

    const std::array neighbors{steering::Vector3{0.f, 0.f, 1.f},
                               steering::Vector3{0.f, 0.f, -3.f}};
    const auto       separated = steering::separation({}, neighbors, 2.f, 4.f);
    CHECK(separated.z < 0.f);

    const auto avoided =
        steering::avoid({}, {0.f, 0.f, 1.f}, {0.f, 0.f, 2.f}, 1.f, 2.f, 3.f);
    CHECK(std::isfinite(avoided.z));
}

TEST_CASE("math.steering.invalidInputsReturnNeutral") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_EQ(steering::seek(steering::Vector2{}, {1.f, 1.f}, -1.f).x, 0.f);
    CHECK_EQ(steering::seek(steering::Vector3{}, {1.f, nan, 1.f}, 1.f).z, 0.f);
    CHECK_EQ(steering::arrive(steering::Vector2{}, {1.f, 1.f}, 1.f, 0.f, 2.f).x,
             0.f);
    CHECK_EQ(steering::pathTarget(steering::Vector3{}, {}, 0, 1.f), -1);
}

TEST_CASE("math.steering.scriptAdaptersExposeBothDimensions") {
    eve::math::Math math;
    std::unique_ptr<eve::math::Vec2> seek2(math.steeringSeek2(0.f, 0.f, 3.f, 4.f, 10.f));
    std::unique_ptr<eve::math::Vec3> seek3(
        math.steeringSeek3(0.f, 0.f, 0.f, 2.f, 3.f, 6.f, 7.f));
    CHECK(std::abs(seek2->getX() - 6.f) < .001f);
    CHECK(std::abs(seek3->getZ() - 6.f) < .001f);

    std::unique_ptr<eve::math::Vec3> separated(
        math.steeringSeparation3(0.f, 0.f, 0.f, "0:0:1,0:0:-3", 2.f, 4.f));
    CHECK(separated->getZ() < 0.f);
    CHECK_EQ(math.steeringPathTarget3(0.f, 0.f, 0.f, "0:0:0,0:0:5", 0, .2f), 1);
}
