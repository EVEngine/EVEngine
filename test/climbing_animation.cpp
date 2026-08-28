#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimGraph.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "climbing/ClimbingAnimation.h"

#include <cmath>

namespace {

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

}  // namespace

TEST_CASE("climbing.animation.extractsRootMotionAndSemanticNotifies") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("mantle");
    clip.setDuration(1.f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 1.f, 1.f, 0.f, 0.f);
    clip.addEvent(0.1f, "contact.left_hand");
    clip.addEvent(0.15f, "collision.compact");
    clip.addEvent(0.2f, "contact.right_hand");
    clip.addEvent(0.22f, "studio.camera_cue");

    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, root).ok());
    auto frame =
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(1), seconds(0.25)}, {0.f, 0.f, 1.f});
    REQUIRE(frame.ok());
    CHECK(std::fabs(frame.value().motion.rootTranslation.x - 0.25f) < 0.001f);
    CHECK(frame.value().motion.hasRootMotion);
    CHECK_EQ(frame.value().recognizedNotifyCount, std::uint32_t(3));
    CHECK_EQ(frame.value().ignoredNotifyCount, std::uint32_t(1));
    REQUIRE_EQ(frame.value().motion.notifies.size(), std::size_t(3));
}

TEST_CASE("climbing.animation.graphProjectionTriggersOnceAndDegradesExplicitly") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     idle("idle");
    idle.setDuration(1.f);
    idle.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimClip shot("climb");
    shot.setDuration(0.5f);
    shot.setLoop(false);
    shot.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    shot.addPositionKey(root, 0.5f, 0.f, 1.f, 0.f);

    eve::animation::AnimGraph graph(&skeleton);
    const int                 base    = graph.addClip(&idle);
    const int                 action  = graph.addClip(&shot);
    const int                 oneShot = graph.addOneShot(base, action, 0.05f, 0.05f);
    graph.setRoot(oneShot);
    eve::climbing::ClimbingGraphState state;
    auto applied = eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(9), state);
    REQUIRE(applied.ok());
    CHECK(graph.isOneShotActive(oneShot));
    auto duplicate = eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(9), state);
    REQUIRE(duplicate.ok());
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(eve::StatusCode::NoOp));

    CHECK_EQ(static_cast<int>(eve::climbing::useDirectClimbingPose(eve::climbing::ClimbingExecutionId(10), state)),
             static_cast<int>(eve::climbing::ClimbingGraphProvider::DirectPoseConstraints));
    CHECK_EQ(state.activeExecutionId.value(), std::uint64_t(10));
}

TEST_CASE("climbing.animation.bindingValidatesClipIdentityAndResolvesRootBone") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("Root", -1);
    eve::animation::AnimClip     clip("anim:mantle");
    clip.setDuration(0.5f);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);

    eve::climbing::ClimbingAnimationBinding binding{"anim:mantle", "climbing.mantle", "Root", true};
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, binding).ok());

    binding.clipId = "anim:other";
    auto wrongClip = eve::climbing::beginClimbingAnimation(player, clip, binding);
    CHECK(!wrongClip.ok());
    binding.clipId   = "anim:mantle";
    binding.rootBone = "Missing";
    auto missingRoot = eve::climbing::beginClimbingAnimation(player, clip, binding);
    CHECK(!missingRoot.ok());
}

TEST_CASE("climbing.animation.rejectsInvalidGraphAndDuplicatePlayerTicks") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("climb");
    clip.setDuration(1.f);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, root).ok());
    REQUIRE(
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(3), seconds(0.1)}, {0.f, 0.f, 1.f}).ok());
    auto duplicate =
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(3), seconds(0.1)}, {0.f, 0.f, 1.f});
    CHECK(!duplicate.ok());

    eve::animation::AnimGraph graph(&skeleton);
    const int                 clipNode = graph.addClip(&clip);
    graph.setRoot(clipNode);
    eve::climbing::ClimbingGraphState state;
    auto invalid = eve::climbing::driveClimbingGraph(graph, clipNode, eve::climbing::ClimbingExecutionId(1), state);
    CHECK(!invalid.ok());
    CHECK(state.activeExecutionId.isZero());
}

TEST_CASE("climbing.animation.motionMatchingYieldsToActionAndResumesByTick") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     locomotion("run");
    locomotion.setDuration(1.f);
    locomotion.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    locomotion.addPositionKey(root, 1.f, 0.f, 0.f, 2.f);
    eve::animation::MotionDatabase database(&skeleton);
    database.addClip(&locomotion);
    database.bake();
    eve::animation::MotionMatcher     matcher(&skeleton, &database);
    eve::climbing::ClimbingGraphState state;
    auto                              locomotionProvider = eve::climbing::driveClimbingMotionMatching(
        matcher, {eve::SimulationTick(1), seconds(0.1)}, {0.f, 0.f, 2.f}, 0.f, state);
    REQUIRE(locomotionProvider.ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::MotionMatching));
    CHECK(state.activeExecutionId.isZero());

    eve::animation::AnimGraph graph(&skeleton);
    const int                 base    = graph.addClip(&locomotion);
    const int                 action  = graph.addClip(&locomotion);
    const int                 oneShot = graph.addOneShot(base, action, 0.05f, 0.05f);
    graph.setRoot(oneShot);
    REQUIRE(eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(42), state).ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::AnimGraph));

    auto resumed = eve::climbing::driveClimbingMotionMatching(matcher, {eve::SimulationTick(3), seconds(0.1)},
                                                              {1.f, 0.f, 0.f}, 1.5707963f, state);
    REQUIRE(resumed.ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::MotionMatching));
    CHECK(state.activeExecutionId.isZero());
    CHECK(std::fabs(matcher.getDesiredVelocityX() - 1.f) < 0.0001f);
}
