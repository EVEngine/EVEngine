#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <cmath>
#include <memory>

using namespace eve::animation;

TEST_CASE("animation.compression.preservesCurvesWithinTolerance") {
    AnimClip clip("linear");
    clip.setDuration(1.f);
    for (int i = 0; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.f;
        clip.addPositionKey(0, t, 2.f * t, -t, 0.5f * t);
        clip.addRotationKey(0, t, 0.f, 0.f, 0.f, 1.f);
        clip.addScaleKey(0, t, 1.f + t, 1.f, 1.f);
    }

    const int removed = clip.compress(1e-5f, 0.01f, 1e-5f);
    CHECK(removed == 27);
    CHECK(clip.getPositionKeyCount(0) == 2);
    CHECK(clip.getRotationKeyCount(0) == 2);
    CHECK(clip.getScaleKeyCount(0) == 2);

    AnimPose pose;
    clip.sample(0.35f, &pose);
    CHECK(std::fabs(pose.getLocalPositionX(0) - 0.7f) < 1e-5f);
    CHECK(std::fabs(pose.getLocalPositionY(0) + 0.35f) < 1e-5f);
}

TEST_CASE("animation.retarget.preservesBindRelativeMotion") {
    AnimSkeleton source;
    const int sourceRoot = source.addBone("root");
    const int sourceHand = source.addBone("hand", sourceRoot);
    source.setBindPosition(sourceHand, 0.f, 1.f, 0.f);

    AnimSkeleton target;
    const int targetRoot = target.addBone("root");
    const int targetHand = target.addBone("hand", targetRoot);
    target.setBindPosition(targetHand, 0.f, 2.f, 0.f);

    AnimClip clip("wave");
    clip.setDuration(1.f);
    clip.setLoop(false);
    clip.addPositionKey(sourceHand, 0.f, 0.f, 1.f, 0.f);
    clip.addPositionKey(sourceHand, 1.f, 0.5f, 1.f, 0.f);
    clip.addRotationKey(sourceHand, 0.f, 0.f, 0.f, 0.f, 1.f);
    clip.addRotationKey(sourceHand, 1.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addEvent(0.5f, "contact");

    std::unique_ptr<AnimClip> retargeted(clip.retarget(&source, &target));
    CHECK(retargeted->getName() == "wave_retargeted");
    CHECK(retargeted->getEventCount() == 1);

    AnimPose pose;
    retargeted->sample(1.f, &pose, &target);
    CHECK(std::fabs(pose.getLocalPositionX(targetHand) - 1.f) < 1e-5f);
    CHECK(std::fabs(pose.getLocalPositionY(targetHand) - 2.f) < 1e-5f);
    CHECK(std::fabs(pose.getLocalRotationZ(targetHand) - 0.70710678f) < 1e-5f);
}

TEST_CASE("animation.retarget.profileMapsNamesAndReportsCoverage") {
    AnimSkeleton source;
    const int sourceRoot = source.addBone("mixamorig:Root");
    const int sourceHips = source.addBone("mixamorig:Hips", sourceRoot);
    source.setBindPosition(sourceHips, 0.f, 1.f, 0.f);

    AnimSkeleton target;
    const int targetRoot = target.addBone("root");
    const int targetPelvis = target.addBone("pelvis", targetRoot);
    const int targetAccessory = target.addBone("weapon_socket", targetPelvis);
    target.setBindPosition(targetPelvis, 0.f, 2.f, 0.f);

    AnimClip clip("walk");
    clip.setDuration(1.f);
    clip.setLoop(false);
    clip.addPositionKey(sourceHips, 0.f, 0.f, 1.f, 0.f);
    clip.addPositionKey(sourceHips, 1.f, 1.f, 1.f, 0.f);

    AnimRetargetProfile profile;
    profile.addBoneMapping("mixamorig:Hips", "pelvis");
    profile.setRootBones("mixamorig:Hips", "pelvis");
    std::unique_ptr<AnimClip> retargeted(clip.retargetWithProfile(&source, &target, &profile));

    CHECK(profile.getMatchedBoneCount() == 2);  // Root is found by normalized matching; pelvis is explicit.
    CHECK(profile.getUnmatchedBoneCount() == 1);
    CHECK(profile.getUnmatchedTargetBone(0) == "weapon_socket");
    AnimPose pose;
    retargeted->sample(1.f, &pose, &target);
    CHECK(std::fabs(pose.getLocalPositionX(targetPelvis) - 2.f) < 1e-5f);
    CHECK(std::fabs(pose.getLocalPositionY(targetPelvis) - 2.f) < 1e-5f);
    CHECK(targetAccessory == 2);
}

TEST_CASE("animation.retarget.profileCorrectsDifferentBindAxesInSkeletonSpace") {
    constexpr float halfSqrt = 0.70710678f;
    AnimSkeleton source;
    const int sourceRoot = source.addBone("root");
    const int sourceParent = source.addBone("shoulder", sourceRoot);
    const int sourceHand = source.addBone("hand", sourceParent);
    source.setBindRotation(sourceParent, 0.f, 0.f, halfSqrt, halfSqrt);

    AnimSkeleton target;
    const int targetRoot = target.addBone("root");
    const int targetHand = target.addBone("hand", targetRoot);  // Deliberately different hierarchy and axes.

    AnimClip clip("axis_test");
    clip.setDuration(1.f);
    clip.setLoop(false);
    // Source hand world rotation is bind-parent Z90 followed by local X90.
    clip.addRotationKey(sourceHand, 0.f, halfSqrt, 0.f, 0.f, halfSqrt);
    clip.addRotationKey(sourceHand, 1.f, halfSqrt, 0.f, 0.f, halfSqrt);

    AnimRetargetProfile profile;
    std::unique_ptr<AnimClip> retargeted(clip.retargetWithProfile(&source, &target, &profile));
    AnimPose pose;
    retargeted->sample(0.5f, &pose, &target);
    CHECK(std::fabs(pose.getLocalRotationX(targetHand) - halfSqrt) < 1e-4f);
    CHECK(std::fabs(pose.getLocalRotationY(targetHand)) < 1e-4f);
    CHECK(std::fabs(pose.getLocalRotationZ(targetHand)) < 1e-4f);
}
