#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "climbing/ClimbingPose.h"

#include <cmath>

TEST_CASE("climbing.pose.appliesBuiltInTwoBoneHandContacts") {
    eve::animation::AnimSkeleton skeleton;
    const int                    leftRoot  = skeleton.addBone("left_upper", -1);
    const int                    leftMid   = skeleton.addBone("left_lower", leftRoot);
    const int                    leftTip   = skeleton.addBone("left_hand", leftMid);
    const int                    rightRoot = skeleton.addBone("right_upper", -1);
    const int                    rightMid  = skeleton.addBone("right_lower", rightRoot);
    const int                    rightTip  = skeleton.addBone("right_hand", rightMid);
    skeleton.setBindPosition(leftMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(leftTip, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightTip, 0.f, 0.5f, 0.f);

    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);
    eve::climbing::ClimbingAdvance advance;
    advance.phase           = eve::climbing::ClimbingPhase::Hanging;
    advance.leftHandAnchor  = {-0.2f, 0.9f, 0.2f};
    advance.rightHandAnchor = {0.2f, 0.9f, 0.2f};
    advance.contactWeight   = 1.f;
    eve::climbing::ClimbingPoseBinding binding;
    binding.leftArm  = {leftRoot, leftMid, leftTip};
    binding.rightArm = {rightRoot, rightMid, rightTip};

    auto applied = eve::climbing::applyClimbingPose(skeleton, pose, advance, binding);
    REQUIRE(applied.ok());
    CHECK(applied.value().leftHandApplied);
    CHECK(applied.value().rightHandApplied);
    CHECK_EQ(applied.value().provider, std::string("animation.two_bone"));
    CHECK(std::fabs(applied.value().weight - 1.f) < 1e-6f);
}

TEST_CASE("climbing.pose.reportsInvalidBoneChainsAndNoOpWeight") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root", -1);
    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);
    eve::climbing::ClimbingAdvance     advance;
    eve::climbing::ClimbingPoseBinding invalid;
    invalid.leftArm         = {0, 1, 2};
    invalid.enableRightHand = false;
    auto rejected           = eve::climbing::applyClimbingPose(skeleton, pose, advance, invalid);
    CHECK(!rejected.ok());

    invalid.enableLeftHand = false;
    auto noOp              = eve::climbing::applyClimbingPose(skeleton, pose, advance, invalid);
    REQUIRE(noOp.ok());
    CHECK_EQ(static_cast<int>(noOp.code()), static_cast<int>(eve::StatusCode::NoOp));
}

TEST_CASE("climbing.pose.appliesOnlyTheHandWhoseContactWindowIsActive") {
    eve::animation::AnimSkeleton skeleton;
    const int                    leftRoot  = skeleton.addBone("left_upper", -1);
    const int                    leftMid   = skeleton.addBone("left_lower", leftRoot);
    const int                    leftTip   = skeleton.addBone("left_hand", leftMid);
    const int                    rightRoot = skeleton.addBone("right_upper", -1);
    const int                    rightMid  = skeleton.addBone("right_lower", rightRoot);
    const int                    rightTip  = skeleton.addBone("right_hand", rightMid);
    skeleton.setBindPosition(leftMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(leftTip, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightTip, 0.f, 0.5f, 0.f);
    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);

    eve::climbing::ClimbingAdvance advance;
    advance.leftHandAnchor  = {-0.2f, 0.9f, 0.2f};
    advance.rightHandAnchor = {0.2f, 0.9f, 0.2f};
    advance.leftHandWeight  = 1.f;
    advance.rightHandWeight = 0.f;
    eve::climbing::ClimbingPoseBinding binding;
    binding.leftArm  = {leftRoot, leftMid, leftTip};
    binding.rightArm = {rightRoot, rightMid, rightTip};
    auto applied     = eve::climbing::applyClimbingPose(skeleton, pose, advance, binding);
    REQUIRE(applied.ok());
    CHECK(applied.value().leftHandApplied);
    CHECK(!applied.value().rightHandApplied);
}
