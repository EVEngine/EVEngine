#include <array>
#include <cmath>
#include <limits>
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/OrientationWarping.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::animation;

namespace {
constexpr float kPi = 3.14159265359f;

float yawOf(const AnimPose& pose, int bone) {
    const auto& t = pose.world(bone);
    return std::atan2(2.f * (t.qw * t.qy + t.qx * t.qz), 1.f - 2.f * (t.qx * t.qx + t.qy * t.qy));
}

AnimSkeleton makeWarpSkeleton() {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("spine", 0);
    skeleton.addBone("head", 1);
    skeleton.addBone("foot", 0);
    return skeleton;
}

AnimPose identityPose(int bones) {
    AnimPose pose(bones);
    return pose;
}
}  // namespace

TEST_CASE("animation.orientationWarping.rotatesRootTowardLocomotionAndLeavesMatcherUntouched") {
    AnimSkeleton skeleton = makeWarpSkeleton();
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    AnimPose pose = identityPose(4);
    pose.setLocalPosition(0, 0.f, 0.f, 1.f);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 2.f, 1.f / 60.f).ok());
    pose.computeWorld(&skeleton);
    CHECK(std::fabs(yawOf(pose, 0) - kPi * 0.5f) < 0.02f);
    CHECK(std::fabs(warp.getAppliedAngle() - kPi * 0.5f) < 0.02f);
}

TEST_CASE("animation.orientationWarping.distributesSpineShareAndRestoresIkWorldYaw") {
    AnimSkeleton skeleton = makeWarpSkeleton();
    OrientationWarping warp;
    const std::array<int, 2> spine{1, 2};
    const std::array<int, 1> ik{3};
    REQUIRE(warp.configure(skeleton, 0, spine, ik).ok());
    REQUIRE(warp.setDistributedAlpha(0.5f).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    pose.computeWorld(&skeleton);
    CHECK(std::fabs(yawOf(pose, 0) - kPi * 0.25f) < 0.02f);
    CHECK(std::fabs(yawOf(pose, 2) - kPi * 0.5f) < 0.03f);
    CHECK(std::fabs(yawOf(pose, 3)) < 0.02f);
}

TEST_CASE("animation.orientationWarping.thresholdAndLowSpeedTargetZero") {
    AnimSkeleton skeleton = makeWarpSkeleton();
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    REQUIRE(warp.setAngleThreshold(kPi * 0.5f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, -1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK(std::fabs(warp.getTargetAngle()) < 1e-5f);
    CHECK(std::fabs(warp.getAppliedAngle()) < 1e-5f);
    REQUIRE(warp.setAngleThreshold(kPi).ok());
    REQUIRE(warp.setMinRootMotionSpeed(1.f).ok());
    REQUIRE(warp.apply(pose, 0.2f, 0.f, 0.f, 0.2f, 0.016f).ok());
    CHECK(std::fabs(warp.getTargetAngle()) < 1e-5f);
}

TEST_CASE("animation.orientationWarping.invalidInputsAndDisabledApplyLeavePoseUnchanged") {
    AnimSkeleton skeleton = makeWarpSkeleton();
    OrientationWarping warp;
    AnimPose pose = identityPose(4);
    pose.setLocalPosition(0, 3.f, 0.f, 0.f);
    CHECK(!warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK_EQ(pose.getLocalPositionX(0), 3.f);
    const std::array<int, 1> bad{9};
    CHECK(!warp.configure(skeleton, 0, bad).ok());
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    const float warped = pose.getLocalRotationY(0);
    CHECK(std::fabs(warped) > 0.1f);
    warp.setEnabled(false);
    AnimPose disabled = identityPose(4);
    disabled.setLocalPosition(0, 3.f, 0.f, 0.f);
    auto skipped = warp.apply(disabled, 1.f, 0.f, 0.f, 1.f, 0.016f);
    REQUIRE(skipped.ok());
    CHECK_EQ(disabled.getLocalPositionX(0), 3.f);
    CHECK_EQ(disabled.getLocalRotationY(0), 0.f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!warp.setDistributedAlpha(nan).ok());
    CHECK(!warp.setAngleThreshold(0.f).ok());
    CHECK(!warp.apply(pose, nan, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK_EQ(pose.getLocalRotationY(0), warped);
}

TEST_CASE("animation.orientationWarping.interpolationUsesInjectedTime") {
    AnimSkeleton skeleton = makeWarpSkeleton();
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(8.f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.05f).ok());
    const float first = warp.getAppliedAngle();
    CHECK(first > 0.f);
    CHECK(first < kPi * 0.5f);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.05f).ok());
    CHECK(warp.getAppliedAngle() > first);
}
