#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/Animation.h"
#include "animation/MotionBuilder.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "animation/MotionSequence.h"
#include "animation/MotionTypes.h"
#include "animation/PoseInertiaInternal.h"
#include "common/Time.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// animation_motion.cpp
using namespace eve::animation;

namespace {

eve::SimulationStep stepAt(std::uint64_t tick, double seconds) {
    return eve::SimulationStep{eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("motion test dt")};
}

}  // namespace

TEST_CASE("animation.motion.floatBindLinear") {
    auto            *anim = Animation::create();
    float            out  = -1.f;
    FloatPointerSink sink(&out);

    auto handle = anim->motion(0.f, 100.f, 1.f).ease("linear").bind(sink).expect("spawn");
    CHECK(anim->motions().isActive(handle));
    CHECK(std::fabs(out - 0.f) < 1e-5f);

    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out - 50.f) < 1e-3f);

    {
        auto advanced = anim->advance(stepAt(2, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out - 100.f) < 1e-3f);
    CHECK(!anim->motions().isActive(handle));
    CHECK_EQ(anim->getMotionCount(), 0);
}

TEST_CASE("animation.motion.completeAndCancel") {
    auto            *anim = Animation::create();
    float            out  = 0.f;
    FloatPointerSink sink(&out);
    int              completed = 0;
    int              cancelled = 0;

    auto a = anim->motion(0.f, 10.f, 1.f).onComplete([&] { ++completed; }).bind(sink).expect("spawn a");
    {
        auto advanced = anim->advance(stepAt(1, 0.25));
        REQUIRE(advanced.ok());
    }
    {
        auto done = anim->motions().complete(a);
        REQUIRE(done.ok());
    }
    CHECK(std::fabs(out - 10.f) < 1e-4f);
    CHECK_EQ(completed, 1);
    CHECK(!anim->motions().isActive(a));

    auto b = anim->motion(0.f, 10.f, 1.f).onCancel([&] { ++cancelled; }).bind(sink).expect("spawn b");
    {
        auto advanced = anim->advance(stepAt(2, 0.1));
        REQUIRE(advanced.ok());
    }
    {
        auto cancelledResult = anim->motions().cancel(b);
        REQUIRE(cancelledResult.ok());
    }
    CHECK_EQ(cancelled, 1);
    CHECK(!anim->motions().isActive(b));
}

TEST_CASE("animation.motion.yoyoLoopsAndVec2") {
    auto           *anim = Animation::create();
    float           x = 0.f, y = 0.f;
    Vec2PointerSink sink(&x, &y);

    auto handle = anim->motionVec2(MotionVec2{0.f, 0.f}, MotionVec2{10.f, 20.f}, 1.f)
                      .ease("linear")
                      .loops(2, MotionLoopMode::Yoyo)
                      .bind(sink)
                      .expect("spawn vec2");

    {
        auto advanced = anim->advance(stepAt(1, 1.0));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(x - 10.f) < 1e-3f);
    CHECK(std::fabs(y - 20.f) < 1e-3f);
    CHECK(anim->motions().isActive(handle));

    {
        auto advanced = anim->advance(stepAt(2, 1.0));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(x - 0.f) < 1e-3f);
    CHECK(std::fabs(y - 0.f) < 1e-3f);
    CHECK(!anim->motions().isActive(handle));
}

TEST_CASE("animation.motion.staleHandleRejected") {
    auto *anim   = Animation::create();
    auto  handle = anim->motion(0.f, 1.f, 0.1f).run().expect("spawn");
    {
        auto advanced = anim->advance(stepAt(1, 0.1));
        REQUIRE(advanced.ok());
    }
    CHECK(!anim->motions().isActive(handle));
    auto value = anim->motions().floatValue(handle);
    CHECK(!value.ok());
    value.ignore("expected stale handle");
}

TEST_CASE("animation.motion.easeBackElasticBounce") {
    CHECK(evaluateMotionEase(0.f, "inBack") == 0.f);
    CHECK(evaluateMotionEase(1.f, "outBack") == 1.f);
    CHECK(evaluateMotionEase(0.5f, "outBack") > 1.f);
    CHECK(evaluateMotionEase(0.f, "inElastic") == 0.f);
    CHECK(evaluateMotionEase(1.f, "outElastic") == 1.f);
    CHECK(evaluateMotionEase(0.f, "inBounce") == 0.f);
    CHECK(evaluateMotionEase(1.f, "outBounce") == 1.f);
    CHECK(evaluateMotionEase(0.5f, "inOutBounce") > 0.f);
}

TEST_CASE("animation.motion.sequenceAppendJoinInterval") {
    auto            *anim = Animation::create();
    float            a    = -1.f;
    float            b    = -1.f;
    FloatPointerSink sinkA(&a);
    FloatPointerSink sinkB(&b);

    auto seq = anim->sequence();
    {
        auto scheduled = seq.append(std::move(anim->motion(0.f, 10.f, 1.f).ease("linear").to(sinkA)));
        REQUIRE(scheduled.ok());
    }
    {
        auto scheduled = seq.appendInterval(0.5f);
        REQUIRE(scheduled.ok());
    }
    {
        auto scheduled = seq.join(std::move(anim->motion(0.f, 20.f, 1.f).ease("linear").to(sinkB)));
        REQUIRE(scheduled.ok());
    }
    CHECK_EQ(seq.itemCount(), 2);
    CHECK(std::fabs(seq.duration() - 1.5f) < 1e-4f);

    auto playback = seq.run().expect("sequence run");
    CHECK_EQ(playback.childCount(), 2);
    CHECK(playback.isActive());

    // Join starts at the last Append start (t=0), so A and B advance together.
    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(a - 5.f) < 1e-3f);
    CHECK(std::fabs(b - 10.f) < 1e-3f);

    {
        auto advanced = anim->advance(stepAt(2, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(a - 10.f) < 1e-3f);
    CHECK(std::fabs(b - 20.f) < 1e-3f);
    CHECK(!playback.isActive());
}

TEST_CASE("animation.motion.sequenceInsertAndComplete") {
    auto            *anim = Animation::create();
    float            x    = 0.f;
    float            y    = 0.f;
    FloatPointerSink sinkX(&x);
    FloatPointerSink sinkY(&y);

    auto seq = anim->sequence();
    {
        auto scheduled = seq.append(std::move(anim->motion(0.f, 10.f, 1.f).ease("linear").to(sinkX)));
        REQUIRE(scheduled.ok());
    }
    {
        auto scheduled = seq.insert(0.5f, std::move(anim->motion(0.f, 100.f, 0.5f).ease("linear").to(sinkY)));
        REQUIRE(scheduled.ok());
    }

    auto playback = seq.run().expect("sequence run");
    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(x - 5.f) < 1e-3f);
    CHECK(std::fabs(y - 0.f) < 1e-3f);

    {
        auto advanced = anim->advance(stepAt(2, 0.25));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(y - 50.f) < 1e-2f);

    {
        auto done = playback.complete();
        REQUIRE(done.ok());
    }
    CHECK(std::fabs(x - 10.f) < 1e-3f);
    CHECK(std::fabs(y - 100.f) < 1e-3f);
    CHECK(!playback.isActive());
}

TEST_CASE("animation.motion.sequenceRejectsInfiniteLoops") {
    auto *anim   = Animation::create();
    auto  seq    = anim->sequence();
    auto  result = seq.append(std::move(anim->motion(0.f, 1.f, 0.2f).loops(-1, MotionLoopMode::Restart)));
    CHECK(!result.ok());
    result.ignore("expected infinite-loop rejection");
}

TEST_CASE("animation.motion.punchShakeFloat") {
    auto            *anim = Animation::create();
    float            out  = -1.f;
    FloatPointerSink sink(&out);

    auto punch = anim->punch(0.f, 10.f, 1.f).frequency(2).dampingRatio(0.f).bind(sink).expect("punch");
    CHECK(anim->motions().isActive(punch));
    CHECK(std::fabs(out - 0.f) < 1e-5f);  // t=0 envelope is 0

    {
        auto advanced = anim->advance(stepAt(1, 0.25));
        REQUIRE(advanced.ok());
    }
    // frequency=2, undamped: sin(2*pi*0.25)=sin(pi/2)=1 => value ~= 10
    CHECK(std::fabs(out - 10.f) < 1e-2f);

    {
        auto advanced = anim->advance(stepAt(2, 0.75));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out - 0.f) < 1e-3f);  // settles at base
    CHECK(!anim->motions().isActive(punch));

    out        = 0.f;
    auto shake = anim->shake(0.f, 5.f, 0.5f).frequency(4).dampingRatio(0.f).seed(7).bind(sink).expect("shake");
    {
        auto advanced = anim->advance(stepAt(3, 0.1));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out) > 1e-3f);  // moved off base
    {
        auto advanced = anim->advance(stepAt(4, 0.4));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out - 0.f) < 1e-3f);
    CHECK(!anim->motions().isActive(shake));
}

TEST_CASE("animation.motion.colorLerpAndQuatSlerp") {
    auto            *anim = Animation::create();
    float            r = 0, g = 0, b = 0, a = 0;
    ColorPointerSink colorSink(&r, &g, &b, &a);

    auto color = anim->motionColor(MotionColor{0.f, 0.f, 0.f, 1.f}, MotionColor{1.f, 0.f, 0.f, 1.f}, 1.f)
                     .ease("linear")
                     .bind(colorSink)
                     .expect("color");
    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(r - 0.5f) < 1e-3f);
    CHECK(std::fabs(a - 1.f) < 1e-5f);
    {
        auto advanced = anim->advance(stepAt(2, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(r - 1.f) < 1e-3f);
    CHECK(!anim->motions().isActive(color));

    float           qx = 0, qy = 0, qz = 0, qw = 1;
    QuatPointerSink quatSink(&qx, &qy, &qz, &qw);
    // 180 deg about Y: (0,1,0,0) from identity
    auto quat = anim->motionQuat(MotionQuat{0.f, 0.f, 0.f, 1.f}, MotionQuat{0.f, 1.f, 0.f, 0.f}, 1.f)
                    .ease("linear")
                    .bind(quatSink)
                    .expect("quat");
    {
        auto advanced = anim->advance(stepAt(3, 0.5));
        REQUIRE(advanced.ok());
    }
    // slerp mid of identity -> (0,1,0,0) is ~ (0, sin(pi/4), 0, cos(pi/4))
    CHECK(std::fabs(qy) > 0.1f);  // mid-slerp has Y component
    CHECK(std::fabs(std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw) - 1.f) < 1e-3f);
    {
        auto advanced = anim->advance(stepAt(4, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(qy - 1.f) < 1e-3f);
    CHECK(std::fabs(qw - 0.f) < 1e-3f);
    CHECK(!anim->motions().isActive(quat));
}

TEST_CASE("animation.motion.oscillationHelpers") {
    CHECK(std::fabs(evaluateMotionOscillation(0.f, 10, 0.f)) < 1e-6f);
    CHECK(std::fabs(evaluateMotionOscillation(1.f, 10, 0.f)) < 1e-6f);
    CHECK(std::fabs(evaluateMotionOscillation(0.25f, 2, 0.f) - 1.f) < 1e-4f);
    const float a = evaluateMotionShakeSign(42u, 8, 0.3f, 0);
    const float b = evaluateMotionShakeSign(42u, 8, 0.3f, 0);
    CHECK(std::fabs(a - b) < 1e-6f);  // deterministic
    CHECK(a >= -1.f);
    CHECK(a <= 1.f);
}

TEST_CASE("animation.motion.ensureCapacityRecycle") {
    auto *anim = Animation::create();
    anim->ensureMotionCapacity(128);
    CHECK_EQ(anim->getMotionFloatCapacity(), 128);
    CHECK_EQ(anim->getMotionFloatFreeCount(), 128);

    float            out = 0.f;
    FloatPointerSink sink(&out);
    auto             handle = anim->motion(0.f, 1.f, 0.1f).ease("linear").bind(sink).expect("spawn");
    CHECK_EQ(anim->getMotionFloatFreeCount(), 127);
    CHECK(anim->motions().isActive(handle));

    {
        auto advanced = anim->advance(stepAt(1, 0.1));
        REQUIRE(advanced.ok());
    }
    CHECK(!anim->motions().isActive(handle));
    CHECK_EQ(anim->getMotionFloatFreeCount(), 128);  // recycled

    // churn: spawn/complete many times without growing capacity
    for (int i = 0; i < 64; ++i) {
        auto h        = anim->motion(0.f, 1.f, 0.05f).ease("linear").bind(sink).expect("churn");
        auto advanced = anim->advance(stepAt(static_cast<std::uint64_t>(2 + i), 0.05));
        REQUIRE(advanced.ok());
        CHECK(!anim->motions().isActive(h));
    }
    CHECK_EQ(anim->getMotionFloatCapacity(), 128);
}

TEST_CASE("animation.motion.floatBufferSinkBatch") {
    auto                                         *anim = Animation::create();
    std::vector<float>                            buffer(8, -1.f);
    std::vector<std::unique_ptr<FloatBufferSink>> sinks;
    anim->ensureMotionCapacity(8);
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        sinks.push_back(std::make_unique<FloatBufferSink>(buffer.data(), i));
        anim->motion(0.f, static_cast<float>(i + 1), 1.f).ease("linear").bind(*sinks.back()).expect("batch spawn");
    }
    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        CHECK(std::fabs(buffer[i] - 0.5f * static_cast<float>(i + 1)) < 1e-3f);
    }
}

TEST_CASE("animation.motion.bench10kFloatAdvance") {
    if (!std::getenv("EVENGINE_MOTION_BENCH")) return;

    constexpr int kCount  = 10000;
    constexpr int kFrames = 120;
    auto         *anim    = Animation::create();
    anim->ensureMotionCapacity(static_cast<std::size_t>(kCount));

    std::vector<float>                            buffer(static_cast<std::size_t>(kCount), 0.f);
    std::vector<std::unique_ptr<FloatBufferSink>> sinks;
    sinks.reserve(static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        sinks.push_back(std::make_unique<FloatBufferSink>(buffer.data(), static_cast<std::size_t>(i)));
        anim->motion(0.f, 1.f, 1.f).ease("linear").bind(*sinks.back()).expect("bench spawn");
    }
    CHECK_EQ(anim->getMotionCount(), kCount);

    using clock   = std::chrono::steady_clock;
    const auto t0 = clock::now();
    for (int f = 0; f < kFrames; ++f) {
        auto advanced = anim->advance(stepAt(static_cast<std::uint64_t>(1 + f), 1.0 / 60.0));
        REQUIRE(advanced.ok());
    }
    const auto   t1 = clock::now();
    const double ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
    std::cout << "[motion-bench] N=" << kCount << " frames=" << kFrames << " total_ms=" << ms
              << " per_frame_ms=" << (ms / kFrames) << " active=" << anim->getMotionCount() << std::endl;
    CHECK(ms > 0.0);
}

// animation_motion_continuity.cpp
using namespace eve::animation;

namespace {
void locomotion(AnimClip &clip, float speed) {
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
    for (auto *clip : {&left, &right}) {
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
    auto yaw = [](const TransformTRS &t) { return std::atan2(2.f * t.qw * t.qy, 1.f - 2.f * t.qy * t.qy); };
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
                const auto &frame = db.frameAt(i);
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
    AnimPose            source(1), previous(1), target(1), previousTarget(1), output(1);
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.setLocalPosition(0, 1.97f, 0.f, 0.f);
    target.setLocalPosition(0, 7.f, 0.f, 0.f);
    previousTarget.setLocalPosition(0, 6.99f, 0.f, 0.f);
    inertia.remember(previous, .01f);
    inertia.begin(source, target, previousTarget, .3f);
    inertia.apply(target, 0.f, output);
    CHECK(std::abs(output.getLocalPositionX(0) - 2.f) < 1e-5f);
    target.setLocalPosition(0, 7.0001f, 0.f, 0.f);
    inertia.apply(target, .0001f, output);
    CHECK(std::abs((output.getLocalPositionX(0) - 2.f) / .0001f - 3.f) < .02f);
    inertia.apply(target, .1f, output);
    AnimPose interrupted(1);
    interrupted.copyFrom(&output);
    inertia.remember(source, .1f);
    target.setLocalPosition(0, -4.f, 0.f, 0.f);
    previousTarget.copyFrom(&target);
    inertia.begin(interrupted, target, previousTarget, .2f);
    inertia.apply(target, 0.f, output);
    CHECK(std::abs(output.getLocalPositionX(0) - interrupted.getLocalPositionX(0)) < 1e-5f);
    inertia.apply(target, .2f, output);
    CHECK(output.getLocalPositionX(0) == -4.f);
}

TEST_CASE("animation.motionMatching.inertiaQuaternionHemisphereAndConvergence") {
    detail::PoseInertia inertia;
    AnimPose            source(1), target(1), previousTarget(1), output(1);
    source.setLocalRotation(0, 0.f, std::sin(.8f), 0.f, std::cos(.8f));
    target.setLocalRotation(0, 0.f, 0.f, 0.f, -1.f);
    previousTarget.copyFrom(&target);
    inertia.begin(source, target, previousTarget, .2f);
    inertia.apply(target, 0.f, output);
    CHECK(std::abs(std::abs(output.local(0).qy) - std::sin(.8f)) < 1e-5f);
    for (int i = 0; i <= 20; ++i) {
        inertia.apply(target, i * .01f, output);
        auto q = output.local(0);
        CHECK(std::abs(q.qx * q.qx + q.qy * q.qy + q.qz * q.qz + q.qw * q.qw - 1.f) < 1e-5f);
    }
    CHECK(std::abs(output.local(0).qy) < 1e-5f);
    inertia.begin(source, target, previousTarget, 0.f);
    inertia.apply(target, 0.f, output);
    CHECK(output.local(0).qw == -1.f);
}

TEST_CASE("animation.motionMatching.inertiaRunsThroughMatcherSearchAndAdvance") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip first("first"), second("second");
    for (auto *clip : {&first, &second}) {
        clip->setDuration(2.f);
        clip->setLoop(false);
    }
    first.addPositionKey(1, 0.f, 0.f, 0.f, 0.f);
    first.addPositionKey(1, 2.f, 6.f, 0.f, 0.f);
    second.addPositionKey(1, 0.f, 7.f, 0.f, 0.f);
    second.addPositionKey(1, 2.f, 9.f, 0.f, 0.f);
    MotionDatabase db(&skeleton);
    db.addFeatureBone(1);
    db.addClip(&first);
    db.addClip(&second);
    db.bake();
    MotionMatcher matcher(&skeleton, &db);
    matcher.setSearchInterval(10.f);
    matcher.setBlendTime(.3f);
    std::vector<MotionSearchRange> candidates{{0, 0.f, 1.f}};
    REQUIRE(matcher.setCandidateRanges(candidates).ok());
    matcher.search();
    matcher.update(.01f);
    matcher.update(.01f);
    float before            = matcher.getPose()->getLocalPositionX(1);
    candidates[0].clipIndex = 1;
    REQUIRE(matcher.setCandidateRanges(candidates).ok());
    matcher.search();
    CHECK(std::abs(matcher.getPose()->getLocalPositionX(1) - before) < 1e-5f);
    matcher.update(.0001f);
    CHECK(std::abs((matcher.getPose()->getLocalPositionX(1) - before) / .0001f - 3.f) < .02f);
    matcher.update(.3f);
    AnimPose expected;
    second.sample(matcher.getMatchedTime(), &expected, &skeleton);
    CHECK(std::abs(matcher.getPose()->getLocalPositionX(1) - expected.getLocalPositionX(1)) < 1e-5f);
}

// animation_motion_curve_features.cpp
using namespace eve::animation;
namespace {
std::vector<std::byte> curveBytes() {
    std::vector<std::byte> out;
    auto                   integer = [&](std::uint32_t value) {
        for (int i = 0; i < 4; ++i) out.push_back(std::byte((value >> (8 * i)) & 255));
    };
    auto number = [&](float value) { integer(std::bit_cast<std::uint32_t>(value)); };
    auto string = [&](std::string_view value) {
        integer(static_cast<std::uint32_t>(value.size()));
        for (char c : value) out.push_back(std::byte(c));
    };
    integer(0x43465645);
    integer(1);
    integer(3);
    for (const auto name : {"positive", "negative", "absent"}) {
        string(name);
        number(1.f);
        const bool absent = std::string_view(name) == "absent";
        integer(absent ? 0 : 1);
        if (absent) continue;
        string("Phase");
        integer(2);
        const float value = std::string_view(name) == "positive" ? 1.f : -1.f;
        number(0.f);
        number(value);
        number(1.f);
        number(value);
    }
    return out;
}
}  // namespace

TEST_CASE("animation.motionSchema.curveFeaturesChangeSelectionWithIdenticalBonePoses") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip positive("positive"), negative("negative"), absent("absent");
    for (auto *clip : {&positive, &negative, &absent}) clip->setDuration(1.f);
    MotionFeatureChannel phase;
    phase.kind       = MotionFeatureKind::Curve;
    phase.query      = MotionFeatureQuery::Character;
    phase.axes       = 1;
    phase.curve      = "Phase";
    phase.sampleTime = -.0333f;
    MotionDatabase db(&skeleton);
    REQUIRE(db.setFeatureLayout({30, {phase}}).ok());
    db.addClip(&positive);
    db.addClip(&negative);
    db.addClip(&absent);
    auto                             bytes = curveBytes();
    const std::array<std::string, 3> sources{"positive", "negative", "absent"};
    REQUIRE(db.setFeatureCurves(bytes, sources).ok());
    CHECK(!db.setFeatureCurves(std::span(bytes).first(3), sources).ok());
    CHECK(!db.setFeatureCurves(bytes, std::span(sources).first(2)).ok());
    bytes.clear();
    db.bake();
    CHECK(db.getFeatureSize() == 1);
    CHECK(!db.setFeatureCurves(curveBytes(), sources).ok());
    for (int selected = 0; selected < 3; ++selected) {
        MotionMatcher                          matcher(&skeleton, &db);
        const std::array<MotionSearchRange, 3> ranges{{{0, 0.f, 0.f}, {1, 0.f, 0.f}, {2, 0.f, 0.f}}};
        REQUIRE(matcher.setCandidateRanges(ranges).ok());
        AnimPose                                pose(1);
        std::array<MotionFeatureCurveSample, 1> query{{{"Phase", -.0333f,
                                                        selected == 0   ? 1.f
                                                        : selected == 1 ? -1.f
                                                                        : 0.f}}};
        REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}, query).ok());
        CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}).ok());
        query[0].value = std::numeric_limits<float>::infinity();
        CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}, query).ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == selected);
    }
}

TEST_CASE("animation.motionSchema.curveContinuingPolicyUsesEligiblePlayhead") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip positive("positive"), negative("negative");
    positive.setDuration(1.f);
    negative.setDuration(1.f);
    for (auto policy : {MotionFeatureQuery::Character, MotionFeatureQuery::Continuing}) {
        MotionFeatureChannel phase;
        phase.kind  = MotionFeatureKind::Curve;
        phase.axes  = 1;
        phase.curve = "Phase";
        phase.query = policy;
        MotionDatabase db(&skeleton);
        REQUIRE(db.setFeatureLayout({30, {phase}}).ok());
        db.addClip(&positive);
        db.addClip(&negative);
        const std::array<std::string, 2> sources{"positive", "negative"};
        REQUIRE(db.setFeatureCurves(curveBytes(), sources).ok());
        db.bake();
        MotionMatcher                           matcher(&skeleton, &db);
        AnimPose                                pose(1);
        std::array<MotionFeatureCurveSample, 1> query{{{"Phase", 0.f, 1.f}}};
        REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}, query).ok());
        matcher.search();
        REQUIRE(matcher.getMatchedClipIndex() == 0);
        query[0].value = -1.f;
        REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}, query).ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == (policy == MotionFeatureQuery::Continuing ? 0 : 1));
    }
}

// animation_motion_features.cpp
using namespace eve::animation;
namespace {
void bones(AnimSkeleton &skeleton) {
    skeleton.addBone("root");
    skeleton.addBone("left", 0);
    skeleton.addBone("right", 0);
    skeleton.addBone("pelvis", 0);
}
void swing(AnimClip &clip, float direction) {
    clip.setDuration(1.f);
    clip.setSampleRate(60.f);
    clip.addPositionKey(1, 0.f, 0.f, 0.f, -0.5f * direction);
    clip.addPositionKey(1, 1.f, 0.f, 0.f, 0.5f * direction);
}
}  // namespace

TEST_CASE("animation.motionMatching.exhaustedOneShotHandsOffAndDoesNotReselect") {
    AnimSkeleton skeleton;
    bones(skeleton);
    AnimClip start("start"), loop("loop");
    start.setDuration(.2f);
    loop.setDuration(.4f);
    loop.setLoop(true);
    MotionDatabase db(&skeleton);
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    db.addClip(&start);
    db.addClip(&loop);
    db.bake();
    MotionMatcher matcher(&skeleton, &db);
    // An arbitrarily long search interval must not keep an expired one-shot.
    matcher.setSearchInterval(10.f);
    const std::array<MotionSearchRange, 2> ranges{{{0, 0, .2f, -1.f, true}, {1, 0, .4f, 0.f, false}}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    AnimPose                              pose(4);
    std::array<MotionLocomotionSample, 5> samples{};
    REQUIRE(matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    matcher.search();
    REQUIRE(matcher.getMatchedClipIndex() == 0);
    for (int i = 0; i < 15; ++i) {
        matcher.update(1.f / 60.f);
        if (matcher.getMatchedClipIndex() == 0) CHECK(matcher.getMatchedTime() <= start.getDuration());
    }
    CHECK(matcher.getMatchedClipIndex() == 1);
    CHECK(matcher.getMatchedTime() > 0.f);

    MotionMatcher only(&skeleton, &db);
    REQUIRE(only.setCandidateRanges(std::span(ranges).first(1)).ok());
    REQUIRE(only.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    for (int i = 0; i < 60; ++i) only.update(1.f / 60.f);
    CHECK(only.getMatchedClipIndex() == 0);
    CHECK(only.getMatchedTime() == start.getDuration());
    REQUIRE(only.setCandidateRanges(std::span(ranges).last(1)).ok());
    only.update(1.f / 60.f);
    CHECK(only.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.locomotionSeparatesEqualPositionsByFootVelocity") {
    AnimSkeleton skeleton;
    bones(skeleton);
    AnimClip forward("swing-forward"), backward("swing-backward");
    swing(forward, 1.f);
    swing(backward, -1.f);
    MotionDatabase db(&skeleton);
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    db.addClip(&forward);
    db.addClip(&backward);
    db.bake();
    CHECK(db.getFeatureSize() == 30);
    for (int direction : {-1, 1}) {
        MotionMatcher                          matcher(&skeleton, &db);
        const std::array<MotionSearchRange, 2> ranges{{{0, .5f, .5f}, {1, .5f, .5f}}};
        REQUIRE(matcher.setCandidateRanges(ranges).ok());
        AnimPose current(4), previous(4);
        previous.setLocalPosition(1, 0.f, 0.f, -direction / 60.f);
        std::array<MotionLocomotionSample, 5> samples{};
        REQUIRE(matcher.setLocomotionQuery(current, previous, 1.f / 60.f, samples).ok());
        // Mutating the caller's pose cannot alter the stored query.
        previous.setLocalPosition(1, 0.f, 0.f, 100.f);
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
    }
}

TEST_CASE("animation.motionMatching.locomotionSeparatesIntermediateFacing") {
    AnimSkeleton skeleton;
    bones(skeleton);
    AnimClip left("turn-left-return"), right("turn-right-return");
    for (auto *clip : {&left, &right}) {
        const float angle = clip == &left ? .6f : -.6f;
        clip->setDuration(1.1f);
        clip->addRotationKey(0, 0.f, 0.f, 0.f, 0.f, 1.f);
        clip->addRotationKey(0, .35f, 0.f, std::sin(angle * .5f), 0.f, std::cos(angle * .5f));
        clip->addRotationKey(0, .7f, 0.f, 0.f, 0.f, 1.f);
        clip->addRotationKey(0, 1.1f, 0.f, 0.f, 0.f, 1.f);
    }
    MotionDatabase db(&skeleton);
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    db.addClip(&left);
    db.addClip(&right);
    db.bake();
    for (int direction : {-1, 1}) {
        MotionMatcher                          matcher(&skeleton, &db);
        const std::array<MotionSearchRange, 2> ranges{{{0, 0, 0}, {1, 0, 0}}};
        REQUIRE(matcher.setCandidateRanges(ranges).ok());
        AnimPose                              pose(4);
        std::array<MotionLocomotionSample, 5> samples{};
        samples[2].yaw = direction * .6f;
        REQUIRE(matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
    }
}

TEST_CASE("animation.motionMatching.locomotionConfigurationRejectsPartialInputs") {
    AnimSkeleton skeleton;
    bones(skeleton);
    MotionDatabase db(&skeleton);
    REQUIRE(!db.setLocomotionFeatures(1, 1, 3).ok());
    CHECK(!db.hasLocomotionFeatures());
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    AnimClip clip("still");
    clip.setDuration(1.f);
    db.addClip(&clip);
    db.bake();
    REQUIRE(!db.setLocomotionFeatures(2, 1, 3).ok());
    CHECK(db.getFeatureBone(0) == 1);
    MotionMatcher matcher(&skeleton, &db);
    CHECK_THROWS((matcher.search(), false));
    AnimPose                              pose(4), wrong(3);
    std::array<MotionLocomotionSample, 5> samples{};
    REQUIRE(matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    REQUIRE(!matcher.setLocomotionQuery(wrong, pose, 1.f / 60.f, samples).ok());
    REQUIRE(!matcher.setLocomotionQuery(pose, pose, 0.f, samples).ok());
    REQUIRE(!matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, std::span(samples).first(4)).ok());
    samples[3].yaw = std::numeric_limits<float>::infinity();
    REQUIRE(!matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    samples[3].yaw = 0.f;
    pose.setLocalPosition(1, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
    REQUIRE(!matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    matcher.search();
    CHECK(std::isfinite(matcher.getLastSearchCost()));
}

TEST_CASE("animation.motionMatching.locomotionRootTravelDoesNotBecomeFootSwing") {
    AnimSkeleton skeleton;
    bones(skeleton);
    AnimClip clip("root-travel");
    clip.setDuration(.8f);
    clip.setLoop(true);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, .8f, 0.f, 0.f, 1.6f);
    skeleton.setBindPosition(1, .1f, .05f, 0.f);
    skeleton.setBindPosition(2, -.1f, .05f, 0.f);
    MotionDatabase db(&skeleton);
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    db.addClip(&clip);
    db.bake();
    for (int i = 0; i < db.getFrameCount(); ++i) {
        const auto &f = db.frameAt(i);
        CHECK(std::abs(f.velZ - 2.f) < .0001f);
        for (int axis = 22; axis < 28; ++axis) CHECK(std::abs(f.feature[axis]) < .001f);
        for (int axis : {0, 1, 2, 3, 6, 7, 10, 11, 12, 13}) CHECK(std::abs(f.feature[axis]) < .001f);
    }
}
