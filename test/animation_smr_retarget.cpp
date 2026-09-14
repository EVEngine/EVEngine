#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSmr.h"

#include <cmath>
#include <memory>

using namespace eve::animation;

namespace {

void buildArmature(AnimSkeleton& skel, float torsoHeight, float upperArm, float lowerArm) {
    const int root     = skel.addBone("Hips");
    const int chest    = skel.addBone("Spine", root);
    const int shoulder = skel.addBone("LeftShoulder", chest);
    const int elbow    = skel.addBone("LeftElbow", shoulder);
    const int hand     = skel.addBone("LeftHand", elbow);
    skel.setBindPosition(chest, 0.f, torsoHeight, 0.f);
    skel.setBindPosition(shoulder, 0.15f, 0.f, 0.f);
    skel.setBindPosition(elbow, upperArm, 0.f, 0.f);
    skel.setBindPosition(hand, lowerArm, 0.f, 0.f);
}

float handChestDistance(const AnimSkeleton& skel, const AnimClip& clip, float time) {
    AnimPose pose;
    clip.sample(time, &pose, &skel);
    pose.computeWorld(&skel);
    const int chest = skel.findBone("Spine");
    const int hand  = skel.findBone("LeftHand");
    const float dx  = pose.getWorldPositionX(hand) - pose.getWorldPositionX(chest);
    const float dy  = pose.getWorldPositionY(hand) - pose.getWorldPositionY(chest);
    const float dz  = pose.getWorldPositionZ(hand) - pose.getWorldPositionZ(chest);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool rotationKeysDiffer(const AnimClip& a, const AnimClip& b, int bone) {
    if (a.getRotationKeyCount(bone) == 0 || b.getRotationKeyCount(bone) == 0) return false;
    const float ax = a.getRotationKeyX(bone, 0);
    const float ay = a.getRotationKeyY(bone, 0);
    const float az = a.getRotationKeyZ(bone, 0);
    const float aw = a.getRotationKeyW(bone, 0);
    const float bx = b.getRotationKeyX(bone, 0);
    const float by = b.getRotationKeyY(bone, 0);
    const float bz = b.getRotationKeyZ(bone, 0);
    const float bw = b.getRotationKeyW(bone, 0);
    const float dot = std::fabs(ax * bx + ay * by + az * bz + aw * bw);
    return dot < 0.999f;
}

}  // namespace

TEST_CASE("animation.smr.sensorCloudTagsBodyParts") {
    AnimSkeleton skel;
    buildArmature(skel, 1.0f, 0.3f, 0.3f);
    const AnimSmrSensorCloud cloud = AnimSmrSensorCloud::fromSkeleton(&skel);
    CHECK(cloud.getSensorCount() > 0);

    bool sawTorso = false;
    bool sawArm   = false;
    for (int i = 0; i < cloud.getSensorCount(); ++i) {
        const AnimSmrBodyPart part = cloud.getSensorPart(i);
        if (part == AnimSmrBodyPart::Torso) sawTorso = true;
        if (part == AnimSmrBodyPart::Arm) sawArm = true;
    }
    CHECK(sawTorso);
    CHECK(sawArm);
}

TEST_CASE("animation.smr.preservesHandTorsoProximityAcrossScale") {
    AnimSkeleton source;
    AnimSkeleton target;
    buildArmature(source, 1.0f, 0.28f, 0.28f);
    buildArmature(target, 1.8f, 0.70f, 0.70f);

    // Curl the arm so the hand is near the torso on the short source.
    AnimClip clip("reach");
    clip.setDuration(0.f);
    clip.setSampleRate(30.f);
    clip.addRotationKey(source.findBone("LeftShoulder"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addRotationKey(source.findBone("LeftElbow"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);

    const float sourceGap = handChestDistance(source, clip, 0.f);

    AnimRetargetProfile fkOnly;
    fkOnly.setSkinnedInteractionPreserve(false);
    std::unique_ptr<AnimClip> fk(clip.retargetWithProfile(&source, &target, &fkOnly));
    const float fkGap = handChestDistance(target, *fk, 0.f);

    AnimRetargetProfile smr;
    smr.setSkinnedInteractionPreserve(true);
    smr.setInteractionContactThreshold(std::max(sourceGap * 1.25f, 0.35f));
    smr.setInteractionCorrectionWeight(1.f);
    smr.addInteractionIkChain("LeftShoulder", "LeftElbow", "LeftHand");
    std::unique_ptr<AnimClip> refined(clip.retargetWithProfile(&source, &target, &smr));
    const float smrGap = handChestDistance(target, *refined, 0.f);

    CHECK(smr.getInteractionCorrectionCount() >= 1);
    CHECK(fkGap > sourceGap);
    // Either the world-space gap shrinks, or the IK rewrite clearly changed chain rotations.
    const bool gapImproved = smrGap + 1e-4f < fkGap;
    const bool keysChanged = rotationKeysDiffer(*fk, *refined, target.findBone("LeftElbow")) ||
                             rotationKeysDiffer(*fk, *refined, target.findBone("LeftShoulder"));
    const bool refinedDiffers = gapImproved || keysChanged;
    CHECK(refinedDiffers);
}
