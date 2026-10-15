#include "physics/forces/ForceModels.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::physics;
namespace {
template <class T>
T take(eve::Result<T> result) {
    const bool ok = result.ok();
    REQUIRE(ok);
    return std::move(result).takeValue();
}
}  // namespace
TEST_CASE("physics.forces.directionAndRegionAreMassScaled") {
    DirectionalField field{{{}, 10, 2}, {0, 5, 0}};
    CHECK_EQ(take(ForceModels::directional(field, {}, 2)).y, 10);
    CHECK_EQ(take(ForceModels::directional(field, {9, 0, 0}, 2)).y, 5);
    CHECK_EQ(take(ForceModels::directional(field, {10, 0, 0}, 2)).y, 0);
    CHECK_EQ(take(ForceModels::directional(field, {0, 0, 11}, 2)).y, 0);
}
TEST_CASE("physics.forces.radialSoftCoreAndRepulsion") {
    RadialField field;
    field.strength = 10;
    auto center    = take(ForceModels::radial(field, {}, 2));
    CHECK_EQ(center.x, 0);
    auto a = take(ForceModels::radial(field, {2, 0, 0}, 2));
    CHECK_LT(a.x, 0);
    CHECK(std::abs(a.x + 20 * 2 / std::pow(4.01, 1.5)) < 1e-10);
    field.strength = -10;
    auto b         = take(ForceModels::radial(field, {0, 0, 2}, 2));
    CHECK(std::abs(a.x + b.z) < 1e-10);
    auto near = take(ForceModels::radial(field, {1e-12, 0, 0}, 1));
    CHECK(std::isfinite(near.x));
}
TEST_CASE("physics.forces.buoyancyDisplacedVolume") {
    BuoyancyModel m;
    m.displacedVolume = 0.25;
    CHECK(std::abs(take(ForceModels::buoyancy(m)).y - 2452.5) < 1e-8);
    m.displacedVolume = 0;
    CHECK_EQ(take(ForceModels::buoyancy(m)).y, 0);
}
TEST_CASE("physics.forces.dampingDissipatesWithoutOvershoot") {
    for (double dt : {0.001, 0.02, 10.0}) {
        const auto force = take(ForceModels::damping({10, 2}, {20, -10, 5}, 2, dt));
        CHECK_LT(force.x, 0);
        CHECK_GT(force.y, 0);
        CHECK(20 + force.x * dt / 2 >= 0);
        CHECK(-10 + force.y * dt / 2 <= 0);
        CHECK(5 + force.z * dt / 2 >= 0);
    }
    CHECK_EQ(take(ForceModels::damping({}, {10, 0, 0}, 1, 0.1)).x, 0);
    CHECK_EQ(take(ForceModels::damping({3, 4}, {}, 1, 0.1)).x, 0);
}
TEST_CASE("physics.forces.wingLiftDragStallAndWind") {
    WingModel  m;
    const auto level = take(ForceModels::wing(m, {20, 0, 0}));
    CHECK_EQ(level.force.y, 0);
    CHECK_LT(level.force.x, 0);
    const auto positive = take(ForceModels::wing(m, {20, -2, 0}));
    const auto negative = take(ForceModels::wing(m, {20, 2, 0}));
    CHECK_GT(positive.force.y, 0);
    CHECK_LT(negative.force.y, 0);
    CHECK(std::abs(positive.force.y + negative.force.y) < 1e-8);
    CHECK(positive.force.x * 20 - positive.force.y * 2 < 0);
    const auto fast = take(ForceModels::wing(m, {40, -4, 0}));
    CHECK(std::abs(fast.force.y / positive.force.y - 4) < 1e-10);
    const auto stall  = take(ForceModels::wing(m, {20 * std::cos(m.stallAngle), -20 * std::sin(m.stallAngle), 0}));
    const auto beyond = take(ForceModels::wing(m, {10, -17.320508, 0}));
    CHECK_LT(beyond.liftCoefficient, stall.liftCoefficient);
    CHECK_GT(beyond.dragCoefficient, stall.dragCoefficient);
    CHECK_EQ(take(ForceModels::wing(m, {})).force.x, 0);
    CHECK_EQ(take(ForceModels::wing(m, {0, 0, 100})).force.x, 0);
}
TEST_CASE("physics.forces.frictionStickSlipAndNoContact") {
    FrictionModel m{0.9, 0.7};
    CHECK_EQ(take(ForceModels::friction(m, {}, {8, 0, 0}, 10)).x, -8);
    CHECK_EQ(take(ForceModels::friction(m, {}, {10, 0, 0}, 10)).x, -7);
    const auto sliding = take(ForceModels::friction(m, {3, 4, 0}, {}, 10));
    CHECK(std::abs(std::hypot(sliding.x, sliding.y) - 7) < 1e-10);
    CHECK(sliding.x * 3 + sliding.y * 4 < 0);
    CHECK_EQ(take(ForceModels::friction(m, {3, 4, 0}, {}, 0)).x, 0);
}
TEST_CASE("physics.forces.tireCombinedSlipAndWheelTorque") {
    TireModel m;
    TireInput in;
    in.normalLoad           = 1000;
    in.longitudinalVelocity = 10;
    in.lateralVelocity      = 3;
    in.angularVelocity      = 50;
    auto out                = take(ForceModels::tire(m, in, 0.001));
    CHECK_GT(out.longitudinalForce, 0);
    CHECK_LT(out.lateralForce, 0);
    CHECK(std::hypot(out.longitudinalForce, out.lateralForce) <= 1000.000001);
    CHECK_EQ(out.reactionTorque, -out.longitudinalForce * m.radius);
    CHECK_LT(out.angularVelocity, in.angularVelocity);
    in.longitudinalVelocity = -10;
    in.lateralVelocity      = -3;
    in.angularVelocity      = -50;
    auto reverse            = take(ForceModels::tire(m, in, 0.001));
    CHECK(std::abs(reverse.longitudinalForce + out.longitudinalForce) < 1e-8);
    CHECK(std::abs(reverse.angularVelocity + out.angularVelocity) < 1e-8);
}
TEST_CASE("physics.forces.tireAirborneFreeRollingAndBrakeLock") {
    TireModel m;
    TireInput in;
    in.angularVelocity = 10;
    in.driveTorque     = 5;
    auto airborne      = take(ForceModels::tire(m, in, 0.1));
    CHECK_EQ(airborne.longitudinalForce, 0);
    CHECK_EQ(airborne.angularVelocity, 10.5);
    in.brakeTorque = 1e6;
    CHECK_EQ(take(ForceModels::tire(m, in, 0.1)).angularVelocity, 0);
    in                      = {};
    in.normalLoad           = 1000;
    in.longitudinalVelocity = 3;
    in.angularVelocity      = 10;
    auto rolling            = take(ForceModels::tire(m, in, 0.01));
    // Rolling resistance now transfers drag to the chassis in the same coupled step.
    CHECK_LT(rolling.slipRatio, 0);
    CHECK_LT(rolling.longitudinalForce, 0);
    CHECK_LT(rolling.resistanceTorque, 0);
    CHECK_LT(rolling.angularVelocity, 10);
    m.rollingResistance = 0;
    auto freeRolling    = take(ForceModels::tire(m, in, 0.01));
    CHECK_EQ(freeRolling.slipRatio, 0);
    CHECK_EQ(freeRolling.longitudinalForce, 0);
    CHECK_EQ(freeRolling.angularVelocity, 10);
}
TEST_CASE("physics.forces.invalidAndOverflowInputsReject") {
    const double nan       = std::numeric_limits<double>::quiet_NaN();
    auto         direction = ForceModels::directional({}, {}, 0);
    CHECK(!direction.ok());
    RadialField radial;
    radial.softening = 0;
    CHECK(!ForceModels::radial(radial, {}, 1).ok());
    BuoyancyModel buoyancy;
    buoyancy.displacedVolume = -1;
    CHECK(!ForceModels::buoyancy(buoyancy).ok());
    CHECK(!ForceModels::damping({}, {nan, 0, 0}, 1, 0.1).ok());
    CHECK(!ForceModels::damping({}, {}, 1, 0).ok());
    WingModel wing;
    wing.normal = wing.chord;
    CHECK(!ForceModels::wing(wing, {}).ok());
    CHECK(!ForceModels::friction({0.1, 0.9}, {}, {}, 1).ok());
    TireInput input;
    input.brakeTorque = -1;
    CHECK(!ForceModels::tire({}, input, 0.1).ok());
    input            = {};
    input.normalLoad = nan;
    CHECK(!ForceModels::tire({}, input, 0.1).ok());
    CHECK(!ForceModels::directional({{}, {1e308, 0, 0}}, {}, 1e308).ok());
}

TEST_CASE("physics.forces.constantDirectionAndRadialCaps") {
    DirectionalForceField constant{{{}, 10, 2}, {0, 12, 0}};
    CHECK_EQ(take(ForceModels::directionalForce(constant, {})).y, 12);
    CHECK_EQ(take(ForceModels::directionalForce(constant, {9, 0, 0})).y, 6);
    RadialField radial;
    radial.strength        = 1e300;
    radial.softening       = 1e-300;
    radial.maxAcceleration = 3;
    radial.maxForce        = 10;
    auto light             = take(ForceModels::radial(radial, {1e-300, 0, 0}, 2));
    CHECK(std::abs(light.x + 6) < 1e-10);
    auto heavy = take(ForceModels::radial(radial, {0, 0, 1e-300}, 100));
    CHECK(std::abs(heavy.z + 10) < 1e-10);
    radial.strength = -1e300;
    auto repelled   = take(ForceModels::radial(radial, {1e-320, 0, 0}, 2));
    CHECK(std::abs(repelled.x - 6) < 1e-10);
    radial.maxForce = 0;
    CHECK_EQ(take(ForceModels::radial(radial, {1, 0, 0}, 2)).x, 0);
    radial.maxForce = -1;
    CHECK(!ForceModels::radial(radial, {1, 0, 0}, 2).ok());
    radial.maxForce        = 1;
    radial.maxAcceleration = std::numeric_limits<double>::infinity();
    CHECK(!ForceModels::radial(radial, {1, 0, 0}, 2).ok());
}

TEST_CASE("physics.forces.boxAndRectangleRegionFade") {
    DirectionalForceField field;
    field.force              = {10, 0, 0};
    field.region.kind        = FieldRegionKind::Box;
    field.region.center      = {10, 20, 30};
    field.region.halfExtents = {2, 3, 4};
    field.region.edgeWidth   = 1;
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 20, 30})).x, 10);
    CHECK_EQ(take(ForceModels::directionalForce(field, {11.5, 20, 30})).x, 5);
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 22.5, 30})).x, 5);
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 20, 33.5})).x, 5);
    CHECK_EQ(take(ForceModels::directionalForce(field, {12, 20, 30})).x, 0);
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 20, 35})).x, 0);
    field.region.kind          = FieldRegionKind::RectangleXY;
    field.region.halfExtents.z = 0;
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 20, 3000})).x, 10);
    CHECK_EQ(take(ForceModels::directionalForce(field, {8.5, 20, 0})).x, 5);
    field.region.edgeWidth = 3;
    CHECK(!ForceModels::directionalForce(field, {}).ok());
    field.region.kind = static_cast<FieldRegionKind>(99);
    CHECK(!ForceModels::directionalForce(field, {}).ok());
    field.region.kind = FieldRegionKind::Global;
    CHECK_EQ(take(ForceModels::directionalForce(field, {1000, 1000, 1000})).x, 10);
}

TEST_CASE("physics.forces.angularDampingDissipatesRelativeRotation") {
    AngularDampingModel model{10, 2};
    for (double dt : {.001, .02, 10.0}) {
        auto scalar = ForceModels::angularDamping2D(model, -4, 3, dt);
        REQUIRE(scalar.ok());
        const double next = -4 + scalar.value() * dt / 3;
        CHECK(next >= -4);
        CHECK(next <= 0);
        ForceVector w{4, -2, 1}, inertia{1, 3, 9};
        auto        torque = ForceModels::angularDamping3D(model, w, inertia, dt);
        REQUIRE(torque.ok());
        const auto  t = torque.value();
        ForceVector n{w.x + t.x * dt / inertia.x, w.y + t.y * dt / inertia.y, w.z + t.z * dt / inertia.z};
        CHECK(n.x >= 0);
        CHECK(n.y <= 0);
        CHECK(n.z >= 0);
        CHECK(n.x * n.x + 3 * n.y * n.y + 9 * n.z * n.z <= 37);
        CHECK(t.x * w.x + t.y * w.y + t.z * w.z <= 0);
    }
    // A faster rotating medium spins the body up while reducing body-minus-medium rotation.
    auto medium = ForceModels::angularDamping2D(model, 2 - 3, 1, .1);
    REQUIRE(medium.ok());
    CHECK(medium.value() > 0);
    CHECK(!ForceModels::angularDamping2D(model, 1, 0, .1).ok());
    CHECK(!ForceModels::angularDamping3D(model, {1, 2, 3}, {1, -1, 1}, .1).ok());
    CHECK(!ForceModels::angularDamping3D(model, {1, 2, 3}, {1, 1, 1}, 0).ok());
    auto zero = ForceModels::angularDamping3D({}, {1, 2, 3}, {1, 1, 1}, 1);
    REQUIRE(zero.ok());
    CHECK_EQ(zero.value().x, 0);
    CHECK_EQ(zero.value().y, 0);
    CHECK_EQ(zero.value().z, 0);
}
