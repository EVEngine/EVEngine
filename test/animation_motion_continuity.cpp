#include "animation/AnimClip.h"
#include "animation/PoseInertiaInternal.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::animation;

namespace {
void locomotion(AnimClip& clip, float speed) {
    clip.setDuration(0.8f);
    clip.setLoop(true);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 0.8f, 0.f, 0.f, speed * 0.8f);
    clip.addPositionKey(1, 0.f, 0.f, 0.1f, -0.2f);
    clip.addPositionKey(1, 0.4f, 0.f, 0.3f, 0.2f);
    clip.addPositionKey(1, 0.8f, 0.f, 0.1f, -0.2f);
}
}  // namespace

TEST_CASE("animation.motionMatching.candidateIntervalsAreAtomicAndConstrainContinuation") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip idle("idle"), walk("walk");
    locomotion(idle, 0.f);
    locomotion(walk, 2.f);
    MotionDatabase db(&skeleton);
    db.addFeatureBone(1);
    db.addClip(&idle);
    db.addClip(&walk);
    db.bake();
    MotionMatcher                  matcher(&skeleton, &db);
    std::vector<MotionSearchRange> first{{1, 0.2f, 0.5f, -0.2f, true}};
    auto                           selected = matcher.setCandidateRanges(first);
    REQUIRE(selected.ok());
    REQUIRE(selected.value() > 0);
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
    CHECK(matcher.getMatchedTime() >= 0.19999f);
    CHECK(matcher.getMatchedTime() <= 0.50001f);
    for (auto bad : std::vector<MotionSearchRange>{
             {9, 0.f, 0.5f}, {0, 0.6f, 0.2f}, {0, 0.f, 9.f}, {0, 0.f, 0.1f, std::numeric_limits<float>::quiet_NaN()}}) {
        auto failed = matcher.setCandidateRanges(std::span(&bad, 1));
        CHECK(!failed.ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == 1);
    }
    auto empty = matcher.setCandidateRanges({});
    CHECK(!empty.ok());
    std::vector<MotionSearchRange> second{{0, 0.f, 0.1f}};
    auto                           changed = matcher.setCandidateRanges(second);
    REQUIRE(changed.ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 0);
    CHECK(matcher.getMatchedTime() <= 0.10001f);
}

TEST_CASE("animation.motionMatching.externalPoseAndTrajectoryInputsAreCopied") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip left("left"), right("right");
    for (auto* clip : {&left, &right}) {
        clip->setDuration(1.f);
        clip->setLoop(true);
    }
    left.addPositionKey(1, 0.f, -1.f, 0.f, 0.f);
    right.addPositionKey(1, 0.f, 1.f, 0.f, 0.f);
    MotionDatabase db(&skeleton);
    db.addFeatureBone(1);
    db.addClip(&left);
    db.addClip(&right);
    db.bake();
    MotionMatcher matcher(&skeleton, &db);
    matcher.setPoseWeight(1.f);
    AnimPose input(2);
    input.setLocalPosition(1, 1.f, 0.f, 0.f);
    ASSERT(matcher.setQueryPose(input).ok());
    AnimPose wrongSkeleton(1);
    ASSERT(!matcher.setQueryPose(wrongSkeleton).ok());
    input.setLocalPosition(1, -1.f, 0.f, 0.f);
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
    std::array<MotionTrajectorySample, 3> trajectory{};
    auto                                  configured = matcher.setTrajectory(trajectory);
    CHECK(configured.ok());
    trajectory[1].yaw = std::numeric_limits<float>::infinity();
    auto invalid      = matcher.setTrajectory(trajectory);
    CHECK(!invalid.ok());
    auto shortInput = matcher.setTrajectory(std::span(trajectory).first(2));
    CHECK(!shortInput.ok());
}

TEST_CASE("animation.motionMatching.constantRootTrackReceivesPlanarTravel") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip clip("in-place");
    clip.setDuration(1.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.2f, 0.f);
    clip.applyPlanarRootMotion(0, 0.f, 1.7f);
    AnimPose pose;
    clip.sample(0.5f, &pose, &skeleton);
    CHECK(std::abs(pose.getLocalPositionZ(0) - 0.85f) < 0.0001f);
    CHECK(std::abs(pose.getLocalPositionY(0) - 0.2f) < 0.0001f);
}

TEST_CASE("animation.motionMatching.loopFeaturesPreserveForwardTravel") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip walk("walk");
    locomotion(walk, 1.7f);
    MotionDatabase database(&skeleton);
    database.addFeatureBone(1);
    database.addClip(&walk);
    database.bake();
    for (int i = 0; i < database.getFrameCount(); ++i) {
        CHECK(std::abs(database.frameAt(i).velZ - 1.7f) < 0.001f);
        // Constant velocity means every phase has identical trajectory features,
        // even when a one-second horizon crosses more than one 0.8s cycle.
        for (int f = 2; f < 8; ++f) CHECK(std::abs(database.frameAt(i).feature[f]) < 0.01f);
    }
}

TEST_CASE("animation.motionMatching.continuesCyclesAndChangesSpeedAndFacing") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip idle("idle"), walk("walk"), run("run");
    locomotion(idle, 0.f);
    locomotion(walk, 1.7f);
    locomotion(run, 4.2f);
    MotionDatabase database(&skeleton);
    database.addFeatureBone(1);
    database.addClip(&idle);
    database.addClip(&walk);
    database.addClip(&run);
    database.bake();
    MotionMatcher matcher(&skeleton, &database);
    matcher.setSearchInterval(0.08f);
    matcher.setBlendTime(0.14f);
    matcher.setVelocityWeight(2.f);
    matcher.setPoseWeight(0.15f);
    for (int clip = 0; clip < 3; ++clip) {
        const float speed = clip == 0 ? 0.f : (clip == 1 ? 1.7f : 4.2f);
        for (int direction = 0; direction < 4; ++direction) {
            const float yaw = direction * 1.57079632679f;
            matcher.setDesiredYaw(yaw);
            matcher.setDesiredVelocity(std::sin(yaw) * speed, std::cos(yaw) * speed);
            matcher.search();
            REQUIRE(matcher.getMatchedClipIndex() == clip);
            float time = matcher.getMatchedTime();
            for (int frame = 0; frame < 180; ++frame) {
                matcher.update(1.f / 60.f);
                CHECK(matcher.getMatchedClipIndex() == clip);
                CHECK(std::abs(matcher.getMatchedTime() - time - 1.f / 60.f) < 0.0001f);
                time = matcher.getMatchedTime();
            }
        }
    }
    matcher.setDesiredVelocity(0.f, 0.f);
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 0);
}


TEST_CASE("animation.motionMatching.sparseFeaturesMatchFullPose") {
    AnimSkeleton skeleton;
    skeleton.addBone("ancestor");
    skeleton.addBone("root", 0);
    skeleton.addBone("foot", 1);
    skeleton.addBone("tip", 2);
    skeleton.addBone("unrelated", 0);
    skeleton.setBindScale(0, 1.2f, 0.9f, 1.4f);
    skeleton.setBindRotation(0, 0.f, std::sin(0.15f), 0.f, std::cos(0.15f));
    skeleton.setBindPosition(2, 0.2f, 0.4f, -0.3f);
    skeleton.setBindPosition(3, 0.1f, 0.2f, 0.3f);
    AnimClip clip("hierarchy");
    clip.setDuration(0.6f);
    clip.setSampleRate(10.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 0.6f, 0.2f, 0.1f, 0.3f);
    clip.addPositionKey(1, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(1, 0.6f, 0.3f, 0.2f, 0.9f);
    clip.addRotationKey(1, 0.f, 0.f, 0.f, 0.f, 1.f);
    clip.addRotationKey(1, 0.6f, 0.f, std::sin(0.2f), 0.f, std::cos(0.2f));
    AnimPose pose;
    auto     world = [&](int bone, float time) {
        clip.sample(time, &pose, &skeleton);
        pose.computeWorld(&skeleton);
        return pose.world(bone);
    };
    auto yaw = [](const TransformTRS& t) { return std::atan2(2.f * t.qw * t.qy, 1.f - 2.f * t.qy * t.qy); };
    for (int copies : {1, 16})
        for (bool loop : {false, true}) {
            clip.setLoop(false);
            const auto start = world(1, 0.f), end = world(1, clip.getDuration());
            clip.setLoop(loop);
            MotionDatabase db(&skeleton);
            db.setRootBone(1);
            db.addFeatureBone(3);
            for (int copy = 0; copy < copies; ++copy) db.addClip(&clip);
            db.bake();
            for (int i = 0; i < db.getFrameCount(); ++i) {
                const auto& frame = db.frameAt(i);
                const float time  = frame.time;
                const auto  root  = world(1, time);
                const float angle = yaw(root), cs = std::cos(angle), sn = std::sin(angle);
                auto        future = [&](float t) {
                    auto        value  = world(1, t);
                    const float cycles = loop ? std::floor(t / clip.getDuration()) : 0.f;
                    value.px += cycles * (end.px - start.px);
                    value.pz += cycles * (end.pz - start.pz);
                    return value;
                };
                const auto  next = future(time + 0.1f);
                const float vx   = (next.px - root.px) / 0.1f;
                const float vz   = (next.pz - root.pz) / 0.1f;
                CHECK(std::abs(frame.rootX - root.px) < 0.00001f);
                CHECK(std::abs(frame.rootZ - root.pz) < 0.00001f);
                CHECK(std::abs(frame.velX - vx) < 0.00001f);
                CHECK(std::abs(frame.velZ - vz) < 0.00001f);
                std::vector<float> expected(13);
                expected[0] = vx * cs - vz * sn;
                expected[1] = vx * sn + vz * cs;
                int offset  = 2;
                for (float horizon : {0.33f, 0.66f, 1.f}) {
                    const auto  f  = future(time + horizon);
                    const float dx = f.px - root.px, dz = f.pz - root.pz;
                    expected[offset++] = dx * cs - dz * sn;
                    expected[offset++] = dx * sn + dz * cs;
                }
                yawToForward(yaw(world(1, time + 1.f)) - angle, expected[8], expected[9]);
                const auto foot = world(3, time);
                expected[10]    = (foot.px - root.px) * cs - (foot.pz - root.pz) * sn;
                expected[11]    = foot.py;
                expected[12]    = (foot.px - root.px) * sn + (foot.pz - root.pz) * cs;
                db.normalizeFeature(expected);
                for (int f = 0; f < 13; ++f) CHECK(std::abs(expected[f] - frame.feature[f]) < 0.001f);
            }
        }
}

TEST_CASE("animation.motionMatching.inertiaPreservesVelocityAndInterruptedPose") {
    detail::PoseInertia inertia;
    AnimPose source(1), previous(1), target(1), previousTarget(1), output(1);
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.setLocalPosition(0, 1.97f, 0.f, 0.f);
    target.setLocalPosition(0, 7.f, 0.f, 0.f);
    previousTarget.setLocalPosition(0, 6.99f, 0.f, 0.f);
    inertia.remember(previous, .01f);
    inertia.begin(source, target, previousTarget, .3f);
    inertia.apply(target, 0.f, output);
    CHECK(std::abs(output.getLocalPositionX(0)-2.f)<1e-5f);
    target.setLocalPosition(0,7.0001f,0.f,0.f);
    inertia.apply(target,.0001f,output);
    CHECK(std::abs((output.getLocalPositionX(0)-2.f)/.0001f-3.f)<.02f);
    inertia.apply(target,.1f,output);
    AnimPose interrupted(1); interrupted.copyFrom(&output);
    inertia.remember(source,.1f);
    target.setLocalPosition(0,-4.f,0.f,0.f);
    previousTarget.copyFrom(&target);
    inertia.begin(interrupted,target,previousTarget,.2f);
    inertia.apply(target,0.f,output);
    CHECK(std::abs(output.getLocalPositionX(0)-interrupted.getLocalPositionX(0))<1e-5f);
    inertia.apply(target,.2f,output);
    CHECK(output.getLocalPositionX(0)==-4.f);
}

TEST_CASE("animation.motionMatching.inertiaQuaternionHemisphereAndConvergence") {
    detail::PoseInertia inertia;
    AnimPose source(1), target(1), previousTarget(1), output(1);
    source.setLocalRotation(0,0.f,std::sin(.8f),0.f,std::cos(.8f));
    target.setLocalRotation(0,0.f,0.f,0.f,-1.f);
    previousTarget.copyFrom(&target);
    inertia.begin(source,target,previousTarget,.2f);
    inertia.apply(target,0.f,output);
    CHECK(std::abs(std::abs(output.local(0).qy)-std::sin(.8f))<1e-5f);
    for(int i=0;i<=20;++i) {
        inertia.apply(target,i*.01f,output);
        auto q=output.local(0);
        CHECK(std::abs(q.qx*q.qx+q.qy*q.qy+q.qz*q.qz+q.qw*q.qw-1.f)<1e-5f);
    }
    CHECK(std::abs(output.local(0).qy)<1e-5f);
    inertia.begin(source,target,previousTarget,0.f);
    inertia.apply(target,0.f,output);
    CHECK(output.local(0).qw==-1.f);
}

TEST_CASE("animation.motionMatching.inertiaRunsThroughMatcherSearchAndAdvance") {
    AnimSkeleton skeleton;
    skeleton.addBone("root"); skeleton.addBone("foot",0);
    AnimClip first("first"), second("second");
    for(auto* clip : {&first,&second}) {clip->setDuration(2.f);clip->setLoop(false);}
    first.addPositionKey(1,0.f,0.f,0.f,0.f);
    first.addPositionKey(1,2.f,6.f,0.f,0.f);
    second.addPositionKey(1,0.f,7.f,0.f,0.f);
    second.addPositionKey(1,2.f,9.f,0.f,0.f);
    MotionDatabase db(&skeleton); db.addFeatureBone(1);db.addClip(&first);db.addClip(&second);db.bake();
    MotionMatcher matcher(&skeleton,&db);matcher.setSearchInterval(10.f);matcher.setBlendTime(.3f);
    std::vector<MotionSearchRange> candidates{{0,0.f,1.f}};
    REQUIRE(matcher.setCandidateRanges(candidates).ok());matcher.search();
    matcher.update(.01f);matcher.update(.01f);
    float before=matcher.getPose()->getLocalPositionX(1);
    candidates[0].clipIndex=1;
    REQUIRE(matcher.setCandidateRanges(candidates).ok());matcher.search();
    CHECK(std::abs(matcher.getPose()->getLocalPositionX(1)-before)<1e-5f);
    matcher.update(.0001f);
    CHECK(std::abs((matcher.getPose()->getLocalPositionX(1)-before)/.0001f-3.f)<.02f);
    matcher.update(.3f);
    AnimPose expected;
    second.sample(matcher.getMatchedTime(),&expected,&skeleton);
    CHECK(std::abs(matcher.getPose()->getLocalPositionX(1)-expected.getLocalPositionX(1))<1e-5f);
}
