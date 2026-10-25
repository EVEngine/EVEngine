// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "climbing"
}
*/
// clang-format on
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "climbing/ClimbingPose.h"

#include <cmath>
#include <limits>

namespace {
struct ContactRig {
    eve::animation::AnimSkeleton       skeleton;
    eve::animation::AnimPose           pose{6};
    eve::climbing::ClimbingPoseBinding binding;
    eve::climbing::ClimbingAdvance     advance;

    ContactRig() {
        skeleton.addBone("left_upper", -1);
        skeleton.addBone("left_lower", 0);
        skeleton.addBone("left_hand", 1);
        skeleton.addBone("right_upper", -1);
        skeleton.addBone("right_lower", 3);
        skeleton.addBone("right_hand", 4);
        for (int bone : {1, 2, 4, 5}) skeleton.setBindPosition(bone, 0.f, 0.5f, 0.f);
        skeleton.applyBindPose(&pose);
        binding.leftArm         = {0, 1, 2};
        binding.rightArm        = {3, 4, 5};
        advance.leftHandAnchor  = {-0.2f, 0.8f, 0.2f};
        advance.rightHandAnchor = {0.2f, 0.8f, 0.2f};
    }
};

void checkUnchanged(const eve::animation::AnimPose& pose) {
    for (int bone = 0; bone < pose.getBoneCount(); ++bone) {
        CHECK_EQ(pose.getLocalRotationX(bone), 0.f);
        CHECK_EQ(pose.getLocalRotationY(bone), 0.f);
        CHECK_EQ(pose.getLocalRotationZ(bone), 0.f);
        CHECK_EQ(pose.getLocalRotationW(bone), 1.f);
    }
}
}  // namespace

TEST_CASE("climbing.poseContacts.nonHandContactsDoNotPinReleasedHands") {
    for (int contact = 0; contact < 3; ++contact) {
        ContactRig rig;
        rig.advance.contactWeight = 1.f;
        if (contact == 0) rig.advance.leftFootWeight = 1.f;
        if (contact == 1) rig.advance.rightFootWeight = 1.f;
        if (contact == 2) rig.advance.pelvisWeight = 1.f;
        auto applied = eve::climbing::applyClimbingPose(rig.skeleton, rig.pose, rig.advance, rig.binding);
        REQUIRE(applied.ok());
        CHECK_EQ(applied.code(), eve::StatusCode::NoOp);
        CHECK(!applied.value().leftHandApplied);
        CHECK(!applied.value().rightHandApplied);
        CHECK_EQ(applied.value().weight, 0.f);
        checkUnchanged(rig.pose);
    }
}

TEST_CASE("climbing.poseContacts.disabledHandsAreNoOp") {
    ContactRig rig;
    rig.advance.leftHandWeight = 1.f;
    rig.binding.enableLeftHand = false;
    auto applied               = eve::climbing::applyClimbingPose(rig.skeleton, rig.pose, rig.advance, rig.binding);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.code(), eve::StatusCode::NoOp);
    CHECK_EQ(applied.value().weight, 0.f);
    checkUnchanged(rig.pose);
}

TEST_CASE("climbing.poseContacts.rejectsNonfiniteInputBeforeChangingEitherHand") {
    for (int input = 0; input < 4; ++input) {
        ContactRig rig;
        rig.advance.leftHandWeight  = 1.f;
        rig.advance.rightHandWeight = 1.f;
        const float invalid         = std::numeric_limits<float>::quiet_NaN();
        if (input == 0) rig.advance.rightHandAnchor.x = invalid;
        if (input == 1) rig.binding.modelWorldOrigin.y = invalid;
        if (input == 2) rig.advance.rightHandWeight = invalid;
        if (input == 3) rig.advance.pelvisWeight = std::numeric_limits<float>::infinity();
        auto applied = eve::climbing::applyClimbingPose(rig.skeleton, rig.pose, rig.advance, rig.binding);
        REQUIRE(!applied.ok());
        checkUnchanged(rig.pose);
    }
}

TEST_CASE("climbing.poseContacts.rejectsDisconnectedChainAndMismatchedPose") {
    ContactRig rig;
    rig.advance.contactWeight = 1.f;
    rig.binding.rightArm      = {3, 1, 5};
    auto chain                = eve::climbing::applyClimbingPose(rig.skeleton, rig.pose, rig.advance, rig.binding);
    REQUIRE(!chain.ok());
    checkUnchanged(rig.pose);
    rig.binding.rightArm = {3, 4, 5};
    eve::animation::AnimPose shortPose{3};
    auto                     size = eve::climbing::applyClimbingPose(rig.skeleton, shortPose, rig.advance, rig.binding);
    CHECK(!size.ok());
}

TEST_CASE("climbing.poseContacts.aggregateOnlyContactRespectsModelPlacement") {
    ContactRig rig;
    rig.advance.contactWeight    = 1.f;
    rig.binding.modelWorldOrigin = {10.f, 2.f, -3.f};
    rig.binding.modelYawRadians  = 1.57079632679f;
    rig.advance.leftHandAnchor   = {10.2f, 2.8f, -2.8f};
    rig.advance.rightHandAnchor  = {10.2f, 2.8f, -3.2f};
    auto applied                 = eve::climbing::applyClimbingPose(rig.skeleton, rig.pose, rig.advance, rig.binding);
    REQUIRE(applied.ok());
    REQUIRE(applied.value().leftHandApplied);
    REQUIRE(applied.value().rightHandApplied);
    CHECK(std::fabs(rig.pose.getWorldPositionX(2) + 0.2f) < 1e-4f);
    CHECK(std::fabs(rig.pose.getWorldPositionY(2) - 0.8f) < 1e-4f);
    CHECK(std::fabs(rig.pose.getWorldPositionZ(2) - 0.2f) < 1e-4f);
    CHECK(std::fabs(rig.pose.getWorldPositionX(5) - 0.2f) < 1e-4f);
}

TEST_CASE("climbing.poseContacts.feetRequireTargetsAndRemainIndependent") {
    ContactRig rig;
    rig.binding.enableLeftHand  = false;
    rig.binding.enableRightHand = false;
    rig.binding.leftLeg         = {0, 1, 2};
    rig.binding.rightLeg        = {3, 4, 5};
    rig.binding.enableLeftFoot  = true;
    rig.binding.enableRightFoot = true;
    rig.advance.leftFootWeight  = 1.f;
    rig.advance.rightFootWeight = 1.f;
    rig.advance.contactWeight   = 1.f;
    auto missing                = eve::climbing::applyClimbingPose(rig.skeleton, rig.pose, rig.advance, rig.binding);
    REQUIRE(missing.ok());
    CHECK_EQ(missing.code(), eve::StatusCode::NoOp);
    checkUnchanged(rig.pose);

    rig.advance.leftFootAnchor    = {-0.2f, 0.8f, 0.2f};
    rig.advance.hasLeftFootAnchor = true;
    auto applied                  = eve::climbing::applyClimbingPose(rig.skeleton, rig.pose, rig.advance, rig.binding);
    REQUIRE(applied.ok());
    CHECK(applied.value().leftFootApplied);
    CHECK(!applied.value().rightFootApplied);
    CHECK(!applied.value().leftHandApplied);
    CHECK(std::fabs(rig.pose.getWorldPositionX(2) + 0.2f) < 1e-4f);
    CHECK(std::fabs(rig.pose.getWorldPositionY(2) - 0.8f) < 1e-4f);
    CHECK(std::fabs(rig.pose.getWorldPositionZ(2) - 0.2f) < 1e-4f);
    CHECK_EQ(rig.pose.getWorldPositionX(5), 0.f);
}

TEST_CASE("climbing.poseContacts.invalidFootRejectsBeforeHandMutation") {
    ContactRig rig;
    rig.binding.enableRightHand    = false;
    rig.binding.rightLeg           = {3, 4, 5};
    rig.binding.enableRightFoot    = true;
    rig.advance.leftHandWeight     = 1.f;
    rig.advance.rightFootWeight    = 1.f;
    rig.advance.hasRightFootAnchor = true;
    rig.advance.rightFootAnchor.x  = std::numeric_limits<float>::infinity();
    auto result                    = eve::climbing::applyClimbingPose(rig.skeleton, rig.pose, rig.advance, rig.binding);
    REQUIRE(!result.ok());
    checkUnchanged(rig.pose);
}
