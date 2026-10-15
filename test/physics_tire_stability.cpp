#include <cmath>
#include <limits>
#include "physics/forces/ForceModels.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;

TEST_CASE("physics.forces.tireStiffLowSpeedConservesImpulseAndDissipates") {
    TireModel model;
    model.longitudinalStiffness = 1e8;
    model.corneringStiffness    = 1e8;
    model.rollingResistance     = 0;
    for (double dt : {.001, .01, .1, 1.0}) {
        TireInput input;
        input.angularVelocity         = .1;
        input.lateralVelocity         = .02;
        input.normalLoad              = 1000;
        input.longitudinalInverseMass = .01;
        input.lateralInverseMass      = .01;
        auto sampled                  = ForceModels::tire(model, input, dt);
        REQUIRE(sampled.ok());
        const auto   s  = sampled.value();
        const double vx = s.longitudinalForce * dt * .01, vy = .02 + s.lateralForce * dt * .01;
        CHECK(vx >= 0);
        CHECK(vy >= -1e-12);
        CHECK(s.angularVelocity >= 0);
        CHECK(model.radius * s.angularVelocity - vx >= -1e-12);
        CHECK(std::abs(100 * vx + model.inertia * s.angularVelocity / model.radius -
                       model.inertia * .1 / model.radius) < 1e-10);
        CHECK(100 * (vx * vx + vy * vy) + model.inertia * s.angularVelocity * s.angularVelocity <=
              100 * .02 * .02 + model.inertia * .1 * .1 + 1e-12);
        CHECK(std::hypot(s.longitudinalForce, s.lateralForce) <= model.friction * input.normalLoad + 1e-8);
    }
}
TEST_CASE("physics.forces.tireDriveAndBrakeSolveTogether") {
    TireModel model;
    model.rollingResistance = 0;
    TireInput input;
    input.normalLoad              = 1000;
    input.longitudinalInverseMass = .01;
    input.driveTorque             = 10;
    auto driven                   = ForceModels::tire(model, input, .02);
    REQUIRE(driven.ok());
    CHECK(driven.value().longitudinalForce > 0);
    CHECK(driven.value().angularVelocity > 0);
    CHECK(std::abs(driven.value().angularVelocity -
                   (input.driveTorque + driven.value().reactionTorque + driven.value().resistanceTorque) * .02 /
                       model.inertia) < 1e-10);
    input.driveTorque = -10;
    auto reverse      = ForceModels::tire(model, input, .02);
    REQUIRE(reverse.ok());
    CHECK(std::abs(reverse.value().longitudinalForce + driven.value().longitudinalForce) < 1e-10);
    CHECK(std::abs(reverse.value().angularVelocity + driven.value().angularVelocity) < 1e-10);
    input.driveTorque = 10;
    input.brakeTorque = 100;
    auto held         = ForceModels::tire(model, input, .02);
    REQUIRE(held.ok());
    CHECK_EQ(held.value().angularVelocity, 0);
    CHECK(std::abs(held.value().longitudinalForce) < 1e-8);
    CHECK(std::abs(held.value().resistanceTorque + 10) < 1e-8);
    input.driveTorque          = 0;
    input.longitudinalVelocity = 10;
    input.brakeTorque          = 1000;
    auto locked                = ForceModels::tire(model, input, .02);
    REQUIRE(locked.ok());
    CHECK_EQ(locked.value().angularVelocity, 0);
    CHECK(locked.value().longitudinalForce < 0);
    CHECK(std::abs(locked.value().resistanceTorque) <= input.brakeTorque);
    CHECK(std::abs(locked.value().reactionTorque + locked.value().resistanceTorque) < 1e-8);
    input.longitudinalInverseMass = -1;
    CHECK(!ForceModels::tire(model, input, .02).ok());
}
TEST_CASE("physics.forces.tireRefinementConvergesToSlipDecay") {
    TireModel model;
    model.rollingResistance     = 0;
    model.longitudinalStiffness = 500;
    double previousError        = 1;
    for (int count : {5, 10, 20, 40}) {
        const double dt = .05 / count;
        TireInput    input;
        input.angularVelocity         = 1;
        input.normalLoad              = 1000;
        input.longitudinalInverseMass = .1;
        for (int i = 0; i < count; ++i) {
            auto sampled = ForceModels::tire(model, input, dt);
            REQUIRE(sampled.ok());
            input.angularVelocity = sampled.value().angularVelocity;
            input.longitudinalVelocity += sampled.value().longitudinalForce * dt * input.longitudinalInverseMass;
        }
        const double rate =
            model.longitudinalStiffness / model.minimumSlipSpeed * (model.radius * model.radius / model.inertia + .1);
        const double exactSlip = .3 * std::exp(-rate * .05);
        const double error = std::abs(model.radius * input.angularVelocity - input.longitudinalVelocity - exactSlip);
        CHECK(error < previousError);
        previousError = error;
    }
}
