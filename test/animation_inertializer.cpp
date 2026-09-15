#include <array>
#include <cmath>
#include <limits>
#include "animation/AnimInertializer.h"
#include "animation/AnimPose.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::animation;

TEST_CASE("animation.inertializer.ownsHistoryAndHonorsBoneDurations") {
    AnimInertializer inertia;
    AnimPose         source(3), previous(3), target(3), priorTarget(3);
    for (int i = 0; i < 3; ++i) source.setLocalPosition(i, 2.f, 0.f, 0.f);
    previous.copyFrom(&source);
    std::array<float, 3> factors{0.f, .75f, 1.f};
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f, factors).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 0.f);
    CHECK(inertia.pose().getLocalPositionX(1) == 2.f);
    source.setLocalPosition(1, 100.f, 0.f, 0.f);
    factors[1] = 1.f;
    REQUIRE(inertia.evaluate(target, .16f).ok());
    CHECK(inertia.pose().getLocalPositionX(1) == 0.f);
    CHECK(inertia.pose().getLocalPositionX(2) > 0.f);
    REQUIRE(inertia.evaluate(target, .2f).ok());
    CHECK(inertia.pose().getLocalPositionX(2) == 0.f);
    // Evaluating absolute time again has no integration/order dependence.
    REQUIRE(inertia.evaluate(target, .16f).ok());
    float atSixteen = inertia.pose().getLocalPositionX(2);
    REQUIRE(inertia.evaluate(target, .03f).ok());
    REQUIRE(inertia.evaluate(target, .16f).ok());
    CHECK(inertia.pose().getLocalPositionX(2) == atSixteen);
}

TEST_CASE("animation.inertializer.invalidInputsLeaveTransitionAndOutputUnchanged") {
    AnimInertializer inertia;
    AnimPose         source(1), previous(1), target(1), priorTarget(1), wrong(2);
    CHECK(!inertia.evaluate(target, 0.f).ok());
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.copyFrom(&source);
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f).ok());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!inertia.begin(source, previous, wrong, priorTarget, .01f, .2f).ok());
    CHECK(!inertia.begin(source, previous, target, priorTarget, 0.f, .2f).ok());
    CHECK(!inertia.begin(source, previous, target, priorTarget, .01f, nan).ok());
    std::array<float, 1> factors{-1.f};
    CHECK(!inertia.begin(source, previous, target, priorTarget, .01f, .2f, factors).ok());
    CHECK(!inertia.evaluate(target, -1.f).ok());
    CHECK(!inertia.evaluate(wrong, 0.f).ok());
    target.setLocalPosition(0, nan, 0.f, 0.f);
    CHECK(!inertia.evaluate(target, .1f).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 2.f);
    target.setLocalPosition(0, 0.f, 0.f, 0.f);
    REQUIRE(inertia.evaluate(target, .2f).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 0.f);
}

TEST_CASE("animation.inertializer.interruptionPreservesMovingRotationAndVelocity") {
    AnimInertializer inertia;
    AnimPose         source(1), previous(1), target(1), priorTarget(1);
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.setLocalPosition(0, 1.97f, 0.f, 0.f);
    source.setLocalRotation(0, 0.f, std::sin(.4f), 0.f, std::cos(.4f));
    previous.setLocalRotation(0, 0.f, std::sin(.39f), 0.f, std::cos(.39f));
    target.setLocalRotation(0, 0.f, std::sin(.1f), 0.f, std::cos(.1f));
    priorTarget.setLocalRotation(0, 0.f, std::sin(.095f), 0.f, std::cos(.095f));
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f).ok());
    target.setLocalRotation(0, 0.f, std::sin(.10005f), 0.f, std::cos(.10005f));
    REQUIRE(inertia.evaluate(target, .0001f).ok());
    auto q = inertia.pose().local(0);
    CHECK(std::abs((2.f * std::atan2(q.qy, q.qw) - .8f) / .0001f - 2.f) < .02f);
    CHECK(std::abs((q.px - 2.f) / .0001f - 3.f) < .02f);
    REQUIRE(inertia.evaluate(target, .08f).ok());
    previous.copyFrom(&inertia.pose());
    REQUIRE(inertia.evaluate(target, .09f).ok());
    source.copyFrom(&inertia.pose());
    float expectedVelocity = (source.local(0).px - previous.local(0).px) / .01f;
    target.setLocalPosition(0, -3.f, 0.f, 0.f);
    priorTarget.copyFrom(&target);
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .15f).ok());
    CHECK(std::abs(inertia.pose().local(0).px - source.local(0).px) < 1e-5f);
    REQUIRE(inertia.evaluate(target, .0001f).ok());
    CHECK(std::abs((inertia.pose().local(0).px - source.local(0).px) / .0001f - expectedVelocity) < .03f);
}
