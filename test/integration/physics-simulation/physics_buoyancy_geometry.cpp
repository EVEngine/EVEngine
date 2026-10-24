// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "module": "physics"
}
*/
// clang-format on

#include <cmath>
#include <memory>
#include <numbers>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/BuoyancyGeometry.h"
#include "physics/forces/ForceBatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;

TEST_CASE("physics.forces.immersionHalfDryFullAndThickness") {
    const double pi     = std::numbers::pi;
    auto         sphere = BuoyancyGeometry::sphere3D({1, 2, 3}, 2, 2);
    REQUIRE(sphere.ok());
    CHECK(std::abs(sphere.value().volume - 16 * pi / 3) < 1e-12);
    CHECK_EQ(sphere.value().fraction, .5);
    CHECK_EQ(sphere.value().centroid.y, 1.25);
    auto circle = BuoyancyGeometry::circle2D({1, 2, 0}, 2, .3, 2);
    REQUIRE(circle.ok());
    CHECK(std::abs(circle.value().volume - .6 * pi) < 1e-12);
    CHECK(std::abs(circle.value().centroid.y - (2 - 8 / (3 * pi))) < 1e-12);
    auto box = BuoyancyGeometry::box3D({0, 0, 0}, {2, 4, 6}, 0);
    REQUIRE(box.ok());
    CHECK_EQ(box.value().volume, 24);
    CHECK_EQ(box.value().centroid.y, -1);
    auto rectangle = BuoyancyGeometry::rectangle2D({}, 2, 4, .3, 0);
    REQUIRE(rectangle.ok());
    CHECK_EQ(rectangle.value().volume, 1.2);
    CHECK_EQ(rectangle.value().centroid.y, -1);
    auto dry = BuoyancyGeometry::sphere3D({}, 2, -2);
    REQUIRE(dry.ok());
    CHECK_EQ(dry.value().volume, 0);
    CHECK_EQ(dry.value().centroid.y, 0);
    auto full = BuoyancyGeometry::circle2D({}, 2, .3, 2);
    REQUIRE(full.ok());
    CHECK_EQ(full.value().fraction, 1);
    CHECK_EQ(full.value().centroid.y, 0);
    CHECK(!BuoyancyGeometry::circle2D({}, 1, 0, 0).ok());
    CHECK(!BuoyancyGeometry::rectangle2D({0, 0, 1}, 2, 4, .3, 0).ok());
    CHECK(!BuoyancyGeometry::box3D({}, {2, -4, 6}, 0).ok());
    CHECK(!BuoyancyGeometry::sphere3D({}, 1e200, 0).ok());
}
TEST_CASE("physics.forces.immersionMatchesIndependentSliceIntegration") {
    constexpr int slices = 20000;
    for (double level : {-1.8, -.8, 0.0, .8, 1.8}) {
        const double dy   = (level + 2) / slices;
        double       area = 0, areaMoment = 0, volume = 0, volumeMoment = 0;
        for (int i = 0; i < slices; ++i) {
            const double y     = -2 + (i + .5) * dy;
            const double width = 2 * std::sqrt(4 - y * y);
            const double disc  = std::numbers::pi * (4 - y * y);
            area += width * dy;
            areaMoment += y * width * dy;
            volume += disc * dy;
            volumeMoment += y * disc * dy;
        }
        auto circle = BuoyancyGeometry::circle2D({}, 2, .7, level);
        REQUIRE(circle.ok());
        auto sphere = BuoyancyGeometry::sphere3D({}, 2, level);
        REQUIRE(sphere.ok());
        CHECK(std::abs(circle.value().volume - area * .7) < 1e-5);
        CHECK(std::abs(circle.value().centroid.y - areaMoment / area) < 1e-5);
        CHECK(std::abs(sphere.value().volume - volume) < 1e-6);
        CHECK(std::abs(sphere.value().centroid.y - volumeMoment / volume) < 1e-6);
    }
    auto cap = BuoyancyGeometry::circle2D({}, 1, 1, -1 + 1e-10);
    REQUIRE(cap.ok());
    CHECK(cap.value().volume > 0);
    CHECK(std::abs(cap.value().centroid.y + 1) < 1e-9);
    auto complement = BuoyancyGeometry::circle2D({}, 1, 1, 1 - 1e-10);
    REQUIRE(complement.ok());
    CHECK(std::abs(cap.value().volume + complement.value().volume - std::numbers::pi) < 1e-12);
}
TEST_CASE("physics.forces.geometricBuoyancyBalancesGravity2DAnd3D") {
    for (float meter : {10.f, 100.f}) {
        World                    world(0, -9.81f * meter, false, meter);
        std::unique_ptr<Body>    body(world.newBody("dynamic", 0, 0));
        std::unique_ptr<Fixture> fixture(body->newCircleFixture(meter));
        fixture->setDensity(500 * .4f);
        auto immersion = BuoyancyGeometry::circle2D({}, 1, .4, 0);
        REQUIRE(immersion.ok());
        auto force = ForceModels::buoyancy({{0, -9.81, 0}, 1000, immersion.value().volume});
        REQUIRE(force.ok());
        REQUIRE(applyForceSI(*body, force.value(), immersion.value().centroid).ok());
        REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
        CHECK(std::abs(body->getLinearVelocityY() / meter) < 1e-6);
        CHECK(std::abs(body->getAngularVelocity()) < 1e-6);
    }
    World3D                  world(0, -9.81f, 0, false);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(1));
    shape->setDensity(500);
    auto immersion = BuoyancyGeometry::sphere3D({}, 1, 0);
    REQUIRE(immersion.ok());
    auto force = ForceModels::buoyancy({{0, -9.81, 0}, 1000, immersion.value().volume});
    REQUIRE(force.ok());
    REQUIRE(applyForceSI(*body, force.value(), immersion.value().centroid).ok());
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(std::abs(body->getLinearVelocityY()) < 1e-6);
    // An offset buoyant volume applies a moment instead of silently forcing through body center.
    auto offset = BuoyancyGeometry::box3D({1, 0, 0}, {1, 1, 1}, 0);
    REQUIRE(offset.ok());
    auto lift = ForceModels::buoyancy({{0, -9.81, 0}, 1000, offset.value().volume});
    REQUIRE(lift.ok());
    REQUIRE(applyForceSI(*body, lift.value(), offset.value().centroid).ok());
    REQUIRE(world.step({eve::SimulationTick{2}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(body->getAngularVelocityZ() > 0);
}
