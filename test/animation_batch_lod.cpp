#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimBatch.h"
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace eve::animation;

TEST_CASE("animation.batch.parallelEvaluationAndBoneLod") {
    AnimSkeleton skeleton;
    const int root = skeleton.addBone("root");
    const int spine = skeleton.addBone("spine", root);
    const int finger = skeleton.addBone("finger", spine);
    skeleton.setBindPosition(finger, 3.f, 0.f, 0.f);
    skeleton.setBoneLodLimit(finger, 0);

    AnimClip clip("move");
    clip.setDuration(1.f);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 1.f, 10.f, 0.f, 0.f);
    clip.addPositionKey(finger, 0.f, 3.f, 0.f, 0.f);
    clip.addPositionKey(finger, 1.f, 9.f, 0.f, 0.f);

    std::vector<std::unique_ptr<AnimPose>> poses;
    AnimBatch batch;
    for (int i = 0; i < 8; ++i) {
        poses.push_back(std::make_unique<AnimPose>());
        batch.add(&clip, &skeleton, poses.back().get(), 0.25f, 1);
    }
    batch.evaluate(4);

    CHECK(batch.getLastWorkerCount() == 4);
    for (const auto& pose : poses) {
        CHECK(std::fabs(pose->getLocalPositionX(root) - 2.5f) < 1e-5f);
        CHECK(std::fabs(pose->getLocalPositionX(finger) - 3.f) < 1e-5f);
    }
}

TEST_CASE("animation.pose.simdBlendMatchesTrsBlend") {
    AnimPose a(1), b(1), out;
    a.setLocalPosition(0, 0.f, 2.f, 4.f);
    a.setLocalScale(0, 1.f, 2.f, 3.f);
    b.setLocalPosition(0, 10.f, 6.f, 0.f);
    b.setLocalScale(0, 3.f, 4.f, 5.f);
    out.blendFrom(&a, &b, 0.25f);
    CHECK(std::fabs(out.getLocalPositionX(0) - 2.5f) < 1e-6f);
    CHECK(std::fabs(out.getLocalPositionY(0) - 3.f) < 1e-6f);
    CHECK(std::fabs(out.getLocalScaleZ(0) - 3.5f) < 1e-6f);
}
