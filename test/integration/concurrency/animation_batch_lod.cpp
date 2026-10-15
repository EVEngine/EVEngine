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
