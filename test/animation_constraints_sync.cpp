#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimConstraintStack.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSyncGroup.h"

#include <cmath>

using namespace eve::animation;

TEST_CASE("animation.constraints.footIkPlantsAndOrders") {
    AnimSkeleton skeleton;
    const int hip = skeleton.addBone("hip");
    const int knee = skeleton.addBone("knee", hip);
    const int foot = skeleton.addBone("foot", knee);
    skeleton.setBindPosition(knee, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(foot, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    AnimConstraintStack stack(&skeleton);
    stack.addFootIK(hip, knee, foot, -1.5f, 0.f, 1.f, 0.f, 0.1f, 1.f);
    CHECK(stack.getCount() == 1);
    stack.apply(&pose);
    CHECK(std::fabs(pose.getWorldPositionY(foot) + 1.4f) < 0.12f);
}

TEST_CASE("animation.syncGroup.alignsNormalizedPhase") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip walk("walk"), run("run");
    walk.setDuration(1.f);
    run.setDuration(2.f);
    walk.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    walk.addPositionKey(0, 1.f, 1.f, 0.f, 0.f);
    run.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    run.addPositionKey(0, 2.f, 2.f, 0.f, 0.f);
    AnimPlayer leader(&skeleton), follower(&skeleton);
    leader.play(&walk);
    follower.play(&run);

    AnimSyncGroup group;
    group.addPlayer(&leader);
    group.addPlayer(&follower);
    group.update(0.25f);
    CHECK(std::fabs(group.getPhase() - 0.25f) < 1e-5f);
    CHECK(std::fabs(follower.getTime() - 0.5f) < 1e-5f);
    CHECK(!group.getUsedMarkerSync());
}

TEST_CASE("animation.syncGroup.alignsCommonFootMarkers") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip walk("walk"), run("run");
    walk.setDuration(2.f);
    run.setDuration(1.f);
    walk.addSyncMarker(0.2f, "left_plant");
    walk.addSyncMarker(1.2f, "right_plant");
    run.addSyncMarker(0.3f, "left_plant");
    run.addSyncMarker(0.8f, "right_plant");
    AnimPlayer leader(&skeleton), follower(&skeleton);
    leader.play(&walk);
    follower.play(&run);

    AnimSyncGroup group;
    group.addPlayer(&leader);
    group.addPlayer(&follower);
    group.update(0.7f);  // Halfway from left plant to right plant.
    CHECK(group.getUsedMarkerSync());
    CHECK(std::fabs(group.getPhase() - 0.35f) < 1e-5f);
    CHECK(std::fabs(follower.getTime() - 0.55f) < 1e-5f);  // Normalized fallback would be 0.35.

    group.update(1.f);  // Halfway through the wrapping right-to-left interval.
    CHECK(std::fabs(follower.getTime() - 0.05f) < 1e-5f);
}
