#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSmr.h"
#include "animation/AnimSmrNeural.h"
#include "animation/tensor/AnimationTensor.h"
#include "animation/tensor/SmrFeatures.h"
#include "common/Capability.h"

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

}  // namespace

TEST_CASE("animation.smr.neural.rot6dRoundTrip") {
    float out[6] = {};
    quatToRot6d(0.f, 0.f, 0.70710678f, 0.70710678f, out);
    float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
    rot6dToQuat(out, qx, qy, qz, qw);
    const float dot = std::fabs(qx * 0.f + qy * 0.f + qz * 0.70710678f + qw * 0.70710678f);
    CHECK(dot > 0.999f);
}

TEST_CASE("animation.smr.neural.featureBatchShapes") {
    AnimSkeleton source;
    AnimSkeleton target;
    buildArmature(source, 1.0f, 0.28f, 0.28f);
    buildArmature(target, 1.8f, 0.70f, 0.70f);

    AnimClip clip("reach");
    clip.setDuration(0.1f);
    clip.setSampleRate(10.f);
    clip.addRotationKey(source.findBone("LeftShoulder"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addRotationKey(source.findBone("LeftElbow"), 0.05f, 0.f, 0.f, 0.70710678f, 0.70710678f);

    const SmrFeatureBatch batch = buildSmrFeatures(clip, &source, &target, nullptr, nullptr, 2, 4);
    CHECK(batch.frames >= 1);
    CHECK(batch.joints == target.getBoneCount());
    CHECK(batch.sensors >= 1);
    CHECK(static_cast<int>(batch.sourceRot6d.size()) == batch.frames * batch.joints * 6);
    CHECK(static_cast<int>(batch.sourceGeom.size()) == batch.sensors * 7);
    CHECK(static_cast<int>(batch.targetGeom.size()) == batch.sensors * 7);
    CHECK(static_cast<int>(batch.sourceDmi.size()) == batch.frames * std::max(batch.pairs, 1) * 10);
}

TEST_CASE("animation.smr.neural.tensorBackendRewritesClip") {
    AnimationTensor* module = AnimationTensor::create();
    CHECK(module != nullptr);
    CHECK(eve::cap::query<ISmrNeuralRetarget>() != nullptr);

    AnimSkeleton source;
    AnimSkeleton target;
    buildArmature(source, 1.0f, 0.28f, 0.28f);
    buildArmature(target, 1.8f, 0.70f, 0.70f);

    AnimClip clip("reach");
    clip.setDuration(0.f);
    clip.setSampleRate(30.f);
    clip.addRotationKey(source.findBone("LeftShoulder"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addRotationKey(source.findBone("LeftElbow"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);

    AnimRetargetProfile neural;
    neural.setSkinnedInteractionPreserve(true);
    neural.setNeuralRetargetEnabled(true);
    neural.setNeuralBackend("tensor");
    neural.addInteractionIkChain("LeftShoulder", "LeftElbow", "LeftHand");

    std::unique_ptr<AnimClip> out(clip.retargetWithProfile(&source, &target, &neural));
    CHECK(out != nullptr);
    CHECK(neural.getNeuralInferenceCount() >= 1);
    CHECK(neural.getInteractionCorrectionCount() >= 1);
    CHECK(out->getRotationKeyCount(target.findBone("LeftElbow")) >= 1);
}

TEST_CASE("animation.smr.neural.fallsBackToClassicalWhenDisabled") {
    AnimationTensor* module = AnimationTensor::create();
    CHECK(module != nullptr);

    AnimSkeleton source;
    AnimSkeleton target;
    buildArmature(source, 1.0f, 0.28f, 0.28f);
    buildArmature(target, 1.8f, 0.70f, 0.70f);

    AnimClip clip("reach");
    clip.setDuration(0.f);
    clip.setSampleRate(30.f);
    clip.addRotationKey(source.findBone("LeftShoulder"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addRotationKey(source.findBone("LeftElbow"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);

    AnimRetargetProfile classical;
    classical.setSkinnedInteractionPreserve(true);
    classical.setNeuralRetargetEnabled(false);
    classical.setInteractionContactThreshold(0.4f);
    classical.setInteractionCorrectionWeight(1.f);
    classical.addInteractionIkChain("LeftShoulder", "LeftElbow", "LeftHand");

    std::unique_ptr<AnimClip> out(clip.retargetWithProfile(&source, &target, &classical));
    CHECK(out != nullptr);
    CHECK(classical.getNeuralInferenceCount() == 0);
    CHECK(classical.getInteractionCorrectionCount() >= 1);
}
