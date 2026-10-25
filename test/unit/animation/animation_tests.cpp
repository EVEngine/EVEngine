#include "zeroerr/assert.h"

// TestLevels selects these source origins independently in trimmed profiles.
// Each section states one public behavior contract using input -> observable result.
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation)
#include "animation/AnimClip.h"
#include "animation/AnimControlMath.h"
#include "animation/AnimGraph.h"
#include "animation/AnimImporter.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimStateMachine.h"
#include "animation/AnimTrail.h"
#include "animation/Animation.h"
#include "animation/ControlAnim.h"
#include "animation/ControlPose.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "animation/Tween.h"
#include "common/Exception.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace test_animation_core {
using namespace eve::animation;

void close(float actual, float expected, float tolerance = 1e-4f) { CHECK(std::abs(actual - expected) < tolerance); }

TEST_CASE("animation.tween.absoluteRelativeAndMultiplePropertiesContract") {
    SUB_CASE("absolute endpoints interpolate by elapsed duration") {
        Tween tween(1.f);
        tween.setFrom("x", 0.f);
        tween.setTo("x", 100.f);
        tween.setEase("linear");
        tween.start();
        REQUIRE(tween.isRunning());
        close(tween.get("x"), 0.f);
        tween.update(.5f);
        close(tween.get("x"), 50.f);
        close(tween.getProgress(), .5f);
        tween.update(.5f);
        close(tween.get("x"), 100.f);
        CHECK(tween.isFinished());
        CHECK(!tween.isActive());
    };
    SUB_CASE("delta is relative to explicit start") {
        Tween tween(1.f);
        tween.setFrom("y", 10.f);
        tween.setDelta("y", 40.f);
        tween.start();
        close(tween.getTo("y"), 50.f);
        close(tween.getDelta("y"), 40.f);
        tween.update(.25f);
        close(tween.get("y"), 20.f);
        tween.update(.75f);
        close(tween.get("y"), 50.f);
    };
    SUB_CASE("two properties share a clock but keep separate endpoints") {
        Tween tween(2.f);
        tween.setFrom("x", 0.f);
        tween.setTo("x", 200.f);
        tween.setFrom("alpha", 1.f);
        tween.setDelta("alpha", -1.f);
        tween.start();
        tween.update(1.f);
        close(tween.get("x"), 100.f);
        close(tween.get("alpha"), .5f);
        CHECK_EQ(tween.getPropertyCount(), 2);
    };
}

TEST_CASE("animation.tween.easeAngleDelayAndRepeatContract") {
    SUB_CASE("out quad changes the midpoint without changing endpoints") {
        Tween tween(1.f);
        tween.setFrom("value", 0.f);
        tween.setTo("value", 1.f);
        tween.setEase("outQuad");
        close(tween.evaluate("value", .5f), .75f);
        close(tween.evaluate("value", 0.f), 0.f);
        close(tween.evaluate("value", 1.f), 1.f);
    };
    SUB_CASE("angle takes the shortest route across the wrap boundary") {
        Tween tween(1.f);
        tween.setFromAngle("heading", 2.8274334f);
        tween.setToAngle("heading", -2.8274334f);
        tween.start();
        tween.update(.5f);
        close(std::abs(tween.get("heading")), 3.14159265f, .05f);
    };
    SUB_CASE("delay excludes elapsed time and pause freezes it") {
        Tween tween(1.f);
        tween.setFrom("x", 0.f);
        tween.setTo("x", 10.f);
        tween.setDelay(.5f);
        tween.start();
        REQUIRE(tween.isDelayed());
        tween.update(.25f);
        close(tween.get("x"), 0.f);
        tween.pause();
        CHECK(tween.isPaused());
        tween.update(1.f);
        close(tween.get("x"), 0.f);
        tween.resume();
        tween.update(.25f);
        CHECK(tween.isRunning());
        tween.update(.5f);
        close(tween.get("x"), 5.f);
    };
    SUB_CASE("yoyo returns to the initial value on second cycle") {
        Tween tween(1.f);
        tween.setFrom("x", 0.f);
        tween.setTo("x", 100.f);
        tween.setRepeat(2);
        tween.setYoyo(true);
        tween.start();
        tween.update(1.f);
        CHECK(tween.isRunning());
        close(tween.get("x"), 100.f);
        tween.update(.5f);
        close(tween.get("x"), 50.f, .01f);
        tween.update(.5f);
        CHECK(tween.isFinished());
        close(tween.get("x"), 0.f, .01f);
    };
    SUB_CASE("unknown easing name reports an error") {
        Tween tween(1.f);
        bool  threw = false;
        try {
            tween.setEase("unknown");
        } catch (const eve::Exception&) {
            threw = true;
        }
        CHECK(threw);
    };
}

TEST_CASE("animation.module.tweenRegistryDropsFinishedEntries") {
    Animation* animation = Animation::create();
    animation->clearAll();
    Tween* shortTween = animation->newTween(.5f);
    Tween* longTween  = animation->newTween(2.f);
    for (Tween* tween : {shortTween, longTween}) {
        tween->setFrom("x", 0.f);
        tween->setTo("x", 1.f);
        tween->start();
    }
    REQUIRE_EQ(animation->getTweenCount(), 2);
    animation->update(.5f);
    CHECK(shortTween->isFinished());
    CHECK(longTween->isRunning());
    animation->clearFinished();
    CHECK_EQ(animation->getTweenCount(), 1);
    CHECK_EQ(animation->getActiveCount(), 1);
    delete shortTween;
    delete longTween;
}

std::unique_ptr<AnimSkeleton> twoBoneSkeleton() {
    auto      skeleton = std::make_unique<AnimSkeleton>();
    const int root     = skeleton->addBone("root", -1);
    const int hip      = skeleton->addBone("hip", root);
    skeleton->setBindPosition(hip, 0.f, 1.f, 0.f);
    return skeleton;
}

std::unique_ptr<AnimClip> locomotionClip(const char* name, float speed, float duration) {
    auto clip = std::make_unique<AnimClip>(name);
    clip->setLoop(true);
    clip->setSampleRate(20.f);
    clip->setDuration(duration);
    clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip->addPositionKey(0, duration, 0.f, 0.f, speed * duration);
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, duration, 0.f, 1.f, speed * duration);
    return clip;
}

TEST_CASE("animation.pose.skeletonBindWorldAndClipSamplingContract") {
    auto skeleton = twoBoneSkeleton();
    REQUIRE_EQ(skeleton->getBoneCount(), 2);
    CHECK_EQ(skeleton->findBone("hip"), 1);
    CHECK_EQ(skeleton->getParent(1), 0);
    CHECK_EQ(skeleton->getBoneName(0), "root");
    AnimPose pose;
    skeleton->applyBindPose(&pose);
    CHECK_EQ(pose.getBoneCount(), 2);
    close(pose.getLocalPositionY(1), 1.f);
    pose.computeWorld(skeleton.get());
    close(pose.getWorldPositionY(1), 1.f);

    AnimClip clip("wave");
    clip.addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip.addPositionKey(1, 1.f, 2.f, 1.f, 0.f);
    clip.setDuration(1.f);
    clip.sample(.5f, &pose, skeleton.get());
    close(pose.getLocalPositionX(1), 1.f);
    close(pose.getLocalPositionY(1), 1.f);
}

TEST_CASE("animation.pose.aimAndTwoBoneIkMoveTowardWorldTargets") {
    SUB_CASE("aim rotates the root forward vector toward the world target") {
        AnimSkeleton skeleton;
        skeleton.addBone("root", -1);
        AnimPose pose;
        skeleton.applyBindPose(&pose);
        REQUIRE(pose.aimBone(&skeleton, 0, 10.f, 0.f, 0.f, 1.f));
        CHECK(pose.getWorldMatrixElement(0, 8) > .999f);
        close(pose.getWorldMatrixElement(0, 10), 0.f, .001f);
    };
    SUB_CASE("two bone IK draws the tip toward the reachable target") {
        AnimSkeleton skeleton;
        const int    root = skeleton.addBone("root", -1);
        const int    knee = skeleton.addBone("knee", root);
        const int    tip  = skeleton.addBone("tip", knee);
        skeleton.setBindPosition(knee, 0.f, 1.f, 0.f);
        skeleton.setBindPosition(tip, 0.f, 1.f, 0.f);
        AnimPose pose;
        skeleton.applyBindPose(&pose);
        pose.computeWorld(&skeleton);
        const float before = std::hypot(pose.getWorldPositionX(tip) - 1.f, pose.getWorldPositionY(tip) - 1.f);
        REQUIRE(pose.solveTwoBoneIK(&skeleton, root, knee, tip, 1.f, 1.f, 0.f, 1.f));
        const float after = std::hypot(pose.getWorldPositionX(tip) - 1.f, pose.getWorldPositionY(tip) - 1.f);
        CHECK(after < before * .1f);
    };
}

TEST_CASE("animation.player.crossFadeRootMotionEventsAndRateLodContract") {
    auto skeleton = twoBoneSkeleton();
    auto walk     = locomotionClip("walk", 2.f, 1.f);
    auto run      = locomotionClip("run", 3.f, 1.f);
    walk->addEvent(.25f, "footstep.left");
    walk->addEvent(.75f, "footstep.right");
    AnimPlayer player(skeleton.get());
    player.play(walk.get());
    player.update(.3f);
    REQUIRE(player.isPlaying());
    close(player.getPose()->getLocalPositionZ(0), .6f, .05f);
    close(player.getRootMotionZ(), .6f, .01f);
    CHECK_EQ(player.consumeEvent(), "footstep.left");
    CHECK(player.consumeEvent().empty());
    player.setTime(.9f);
    player.update(.2f);
    close(player.getRootMotionZ(), .4f, .01f);
    player.update(.7f);
    CHECK_EQ(player.consumeEvent(), "footstep.left");
    CHECK_EQ(player.consumeEvent(), "footstep.right");
    player.crossFade(run.get(), .2f);
    player.update(.1f);
    CHECK(player.isPlaying());
    player.update(.2f);
    CHECK(player.getClip() == run.get());

    player.stop();
    player.play(walk.get());
    player.setUpdateRate(10.f);
    player.update(.04f);
    close(player.getTime(), 0.f);
    player.update(.06f);
    close(player.getTime(), .1f);
}

TEST_CASE("animation.graph.blendSpaceMaskedLayerAndAdditiveContract") {
    auto     skeleton = twoBoneSkeleton();
    auto     idle     = locomotionClip("idle", 0.f, 1.f);
    auto     run      = locomotionClip("run", 4.f, 1.f);
    AnimClip overlay("overlay");
    overlay.setDuration(1.f);
    overlay.addPositionKey(1, 0.f, 2.f, 1.f, 0.f);
    AnimGraph graph(skeleton.get());
    const int idleNode    = graph.addClip(idle.get());
    const int runNode     = graph.addClip(run.get());
    const int overlayNode = graph.addClip(&overlay);
    const int locomotion  = graph.addBlendSpace1D();
    graph.addBlendSpace1DPoint(locomotion, 0.f, idleNode);
    graph.addBlendSpace1DPoint(locomotion, 4.f, runNode);
    graph.setPosition1D(locomotion, 2.f);
    const int layer = graph.addLayer(locomotion, overlayNode, .5f);
    graph.setBoneMask(layer, 1, 1.f, true);
    graph.setRoot(layer);
    graph.update(.25f);
    close(graph.getPose()->getLocalPositionZ(0), .5f, .05f);
    close(graph.getPose()->getLocalPositionX(1), 1.f, .05f);
    const int additive = graph.addAdditive(locomotion, overlayNode, .25f);
    graph.setRoot(additive);
    graph.update(0.f);
    close(graph.getPose()->getLocalPositionX(1), .5f, .05f);
}

TEST_CASE("animation.graph.oneShotStartsOnTriggerAndCompletesAfterClip") {
    auto skeleton = twoBoneSkeleton();
    auto idle     = locomotionClip("idle", 0.f, 1.f);
    auto action   = locomotionClip("action", 2.f, .5f);
    action->setLoop(false);
    AnimGraph graph(skeleton.get());
    const int baseNode   = graph.addClip(idle.get());
    const int actionNode = graph.addClip(action.get());
    const int oneShot    = graph.addOneShot(baseNode, actionNode, .1f, .1f);
    graph.setRoot(oneShot);
    graph.trigger(oneShot);
    graph.update(.25f);
    REQUIRE(graph.isOneShotActive(oneShot));
    CHECK(graph.getPose()->getLocalPositionZ(0) > .1f);
    graph.update(.3f);
    CHECK(!graph.isOneShotActive(oneShot));
}

TEST_CASE("animation.layers.boneMaskOverrideAndAdditiveReferenceContract") {
    auto     skeleton = twoBoneSkeleton();
    auto     walk     = locomotionClip("walk", 1.f, 1.f);
    AnimClip upper("upper");
    upper.setDuration(1.f);
    upper.setLoop(true);
    upper.addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    upper.addPositionKey(1, 1.f, 2.f, 1.f, 0.f);
    AnimPlayer basePlayer(skeleton.get());
    AnimPlayer upperPlayer(skeleton.get());
    basePlayer.play(walk.get());
    upperPlayer.play(&upper);
    AnimBoneMask upperMask(skeleton.get());
    REQUIRE(upperMask.setBoneAndChildren("hip", 1.f));
    AnimLayerMixer mixer(skeleton.get());
    REQUIRE(mixer.setBasePlayer(&basePlayer));
    CHECK_EQ(mixer.addLayer("upper", &upperPlayer, &upperMask, "override"), 0);
    REQUIRE(mixer.setLayerWeight("upper", .5f));
    mixer.update(.5f);
    close(mixer.getPose()->getLocalPositionZ(0), .5f);
    close(mixer.getPose()->getLocalPositionX(1), .5f);
    REQUIRE(mixer.setLayerEnabled("upper", false));
    mixer.update(.25f);
    const float disabledTime = upperPlayer.getTime();
    CHECK(disabledTime > .5f);
    REQUIRE(mixer.setLayerEnabled("upper", true));
    mixer.update(.1f);
    CHECK(upperPlayer.getTime() > disabledTime);

    AnimClip absolute("absolute");
    absolute.setDuration(1.f);
    absolute.setLoop(true);
    absolute.addPositionKey(1, 0.f, 0.f, 3.f, 0.f);
    absolute.addPositionKey(1, 1.f, 0.f, 3.f, 0.f);
    AnimGraph graph(skeleton.get());
    const int baseNode     = graph.addClip(walk.get());
    const int absoluteNode = graph.addClip(&absolute);
    const int additiveNode = graph.addAdditive(baseNode, absoluteNode, 1.f);
    graph.setRoot(additiveNode);
    REQUIRE(graph.setAdditiveReferenceCompat(additiveNode, "bind"));
    CHECK_EQ(graph.getAdditiveReference(additiveNode), "bind");
    graph.update(0.f);
    close(graph.getPose()->getLocalPositionY(1), 3.f, .1f);
}

TEST_CASE("animation.stateMachine.floatAndTriggerTransitionsFollowExitRules") {
    SUB_CASE("float speed transitions between idle and walk") {
        auto             skeleton = twoBoneSkeleton();
        auto             idle     = locomotionClip("idle", 0.f, 1.f);
        auto             walk     = locomotionClip("walk", 1.f, 1.f);
        AnimStateMachine machine(skeleton.get());
        machine.addState("Idle", idle.get());
        machine.addState("Walk", walk.get());
        machine.setEntry("Idle");
        const int forward = machine.addTransition("Idle", "Walk", .1f);
        machine.addFloatCondition(forward, "speed", ">", .5f);
        const int backward = machine.addTransition("Walk", "Idle", .1f);
        machine.addFloatCondition(backward, "speed", "<", .1f);
        REQUIRE_EQ(machine.getCurrentState(), "Idle");
        machine.setFloat("speed", 1.f);
        for (int i = 0; i < 20; ++i) machine.update(.05f);
        CHECK_EQ(machine.getCurrentState(), "Walk");
        machine.setFloat("speed", 0.f);
        for (int i = 0; i < 20; ++i) machine.update(.05f);
        CHECK_EQ(machine.getCurrentState(), "Idle");
    };
    SUB_CASE("trigger starts action and exit time delays return") {
        auto skeleton = twoBoneSkeleton();
        auto idle     = locomotionClip("idle", 0.f, 1.f);
        auto jump     = locomotionClip("jump", 0.f, .5f);
        jump->setLoop(false);
        AnimStateMachine machine(skeleton.get());
        machine.addState("Idle", idle.get());
        machine.addState("Jump", jump.get());
        machine.setEntry("Idle");
        const int forward = machine.addTransition("Idle", "Jump", 0.f);
        machine.addTriggerCondition(forward, "jump");
        const int backward = machine.addTransition("Jump", "Idle", .05f);
        machine.setExitTime(backward, .9f);
        machine.update(.1f);
        CHECK_EQ(machine.getCurrentState(), "Idle");
        machine.setTrigger("jump");
        machine.update(.016f);
        CHECK_EQ(machine.getCurrentState(), "Jump");
        machine.update(.2f);
        CHECK_EQ(machine.getCurrentState(), "Jump");
        machine.update(.4f);
        for (int i = 0; i < 10; ++i) machine.update(.05f);
        CHECK_EQ(machine.getCurrentState(), "Idle");
    };
}

TEST_CASE("animation.stateMachine.snapshotRestoresParametersAndRejectsUnknownState") {
    auto             skeleton = twoBoneSkeleton();
    auto             idle     = locomotionClip("idle", 0.f, 1.f);
    auto             walk     = locomotionClip("walk", 1.f, 1.f);
    AnimStateMachine source(skeleton.get());
    source.addState("Idle", idle.get());
    source.addState("Walk", walk.get());
    source.setEntry("Idle");
    source.setFloat("speed", 2.5f);
    source.setBool("grounded", true);
    source.setTrigger("jump");
    source.update(.25f);
    eve::StateValue snapshot;
    REQUIRE(source.captureState(snapshot));
    source.setFloat("speed", 0.f);
    source.setBool("grounded", false);
    source.resetTrigger("jump");
    source.setEntry("Walk");
    std::string error;
    REQUIRE(source.restoreState(snapshot, &error));
    CHECK(error.empty());
    CHECK_EQ(source.getCurrentState(), "Idle");
    close(source.getFloat("speed"), 2.5f);
    CHECK(source.getBool("grounded"));
    CHECK(source.getTrigger("jump"));
    CHECK(source.getStateTime() > .1f);

    AnimStateMachine incompatible(skeleton.get());
    incompatible.addState("Walk", walk.get());
    incompatible.setEntry("Walk");
    CHECK(!incompatible.restoreState(snapshot, &error));
    CHECK(!error.empty());
    incompatible.resetToDefaults();
    CHECK_EQ(incompatible.getCurrentState(), "Walk");
    CHECK(!incompatible.isBlending());
}

TEST_CASE("animation.stateMachine.invalidDeclarationsRejectDuplicateAndMissingStates") {
    auto             skeleton = twoBoneSkeleton();
    auto             idle     = locomotionClip("idle", 0.f, 1.f);
    AnimStateMachine machine(skeleton.get());
    machine.addState("Idle", idle.get());
    bool duplicateRejected = false;
    try {
        machine.addState("Idle", idle.get());
    } catch (const eve::Exception&) {
        duplicateRejected = true;
    }
    REQUIRE(duplicateRejected);
    bool missingRejected = false;
    try {
        machine.addTransition("Idle", "Missing", .1f);
    } catch (const eve::Exception&) {
        missingRejected = true;
    }
    CHECK(missingRejected);
    const int valid            = machine.addTransition("Idle", "Idle", 0.f);
    bool      operatorRejected = false;
    try {
        machine.addFloatCondition(valid, "value", "??", 0.f);
    } catch (const eve::Exception&) {
        operatorRejected = true;
    }
    CHECK(operatorRejected);
}

TEST_CASE("animation.clip.timeWrappingAndPlanarRootMotionContract") {
    AnimClip clip("loop");
    clip.setDuration(1.f);
    clip.setLoop(true);
    close(clip.wrapTime(1.5f), .5f);
    close(clip.wrapTime(-.25f), .75f);
    clip.setLoop(false);
    close(clip.wrapTime(2.f), 1.f);
    close(clip.wrapTime(-1.f), 0.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 1.f, 0.f, 0.f, 0.f);
    clip.applyPlanarRootMotion(0, 0.f, 2.f);
    close(clip.getPositionKeyZ(0, 0), 0.f);
    close(clip.getPositionKeyZ(0, 1), 2.f);
}

TEST_CASE("animation.pose.blendLocalTransformsThenComputeWorldHierarchy") {
    auto     skeleton = twoBoneSkeleton();
    AnimPose left(2), right(2), midpoint;
    left.setLocalPosition(1, 0.f, 1.f, 0.f);
    right.setLocalPosition(1, 2.f, 1.f, 0.f);
    midpoint.blendFrom(&left, &right, .5f);
    close(midpoint.getLocalPositionX(1), 1.f);
    midpoint.setLocalPosition(0, 0.f, 0.f, 0.f);
    midpoint.setLocalPosition(1, 0.f, 2.f, 0.f);
    midpoint.computeWorld(skeleton.get());
    close(midpoint.getWorldPositionY(1), 2.f);
}

TEST_CASE("animation.player.pauseResumeAndNonloopStopContract") {
    auto skeleton = twoBoneSkeleton();
    auto clip     = locomotionClip("once", 1.f, 1.f);
    clip->setLoop(false);
    AnimPlayer player(skeleton.get());
    player.play(clip.get());
    player.setLoop(false);
    player.update(.25f);
    const float beforePause = player.getPose()->getLocalPositionZ(0);
    player.pause();
    REQUIRE(player.isPaused());
    player.update(.5f);
    close(player.getPose()->getLocalPositionZ(0), beforePause);
    player.resume();
    player.update(.25f);
    CHECK(player.getPose()->getLocalPositionZ(0) > beforePause + .1f);
    player.update(1.f);
    CHECK(!player.isPlaying());
    close(player.getTime(), 1.f, .001f);
    player.stop();
    CHECK(player.getClip() == nullptr);
}

TEST_CASE("animation.motionDatabase.desiredSpeedSelectsMatchingClip") {
    auto           skeleton = twoBoneSkeleton();
    auto           idle     = locomotionClip("idle", 0.f, 1.f);
    auto           walk     = locomotionClip("walk", 1.f, 1.f);
    auto           run      = locomotionClip("run", 3.f, 1.f);
    MotionDatabase database(skeleton.get());
    database.setRootBone(0);
    database.addFeatureBone(1);
    database.addClip(idle.get());
    database.addClip(walk.get());
    database.addClip(run.get());
    database.bake();
    CHECK(database.isBaked());
    REQUIRE_EQ(database.getClipCount(), 3);
    CHECK(database.getFrameCount() > 0);
    CHECK(database.getFeatureSize() >= 13);
    CHECK_EQ(database.getFeatureBoneCount(), 1);
    CHECK_EQ(database.getFeatureBone(0), 1);
    std::vector<float> feature(static_cast<std::size_t>(database.getFeatureSize()));
    database.getFeature(0, feature.data(), database.getFeatureSize());
    for (float value : feature) CHECK(std::isfinite(value));

    MotionMatcher matcher(skeleton.get(), &database);
    matcher.setIgnoreRadius(0);
    matcher.setVelocityWeight(3.f);
    matcher.setTrajectoryWeight(1.f);
    matcher.setPoseWeight(.25f);
    matcher.setDesiredVelocity(0.f, 0.f);
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 0);
    matcher.setDesiredVelocity(0.f, 1.f);
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 1);
    matcher.setDesiredVelocity(0.f, 3.f);
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 2);
    matcher.update(.1f);
    CHECK_EQ(matcher.getPose()->getBoneCount(), 2);
}

TEST_CASE("animation.importer.fixtureTextRoundTripsHierarchyClipAndMarkers") {
    auto skeleton = twoBoneSkeleton();
    skeleton->setBindRotation(1, 0.f, 0.f, 0.f, 1.f);
    auto clip = locomotionClip("walk", 1.5f, .8f);
    clip->setSampleRate(12.f);
    clip->addSyncMarker(.2f, "left_plant");
    clip->addSyncMarker(.6f, "right_plant");
    const std::string text = AnimImporter::exportAnimationFixtureText(skeleton.get(), clip.get());
    CHECK(text.find("EVA 1") != std::string::npos);
    AnimSkeleton* importedSkeleton = nullptr;
    AnimClip*     importedClip     = nullptr;
    AnimImporter::importAnimationFixtureText(text, &importedSkeleton, &importedClip);
    std::unique_ptr<AnimSkeleton> ownedSkeleton(importedSkeleton);
    std::unique_ptr<AnimClip>     ownedClip(importedClip);
    REQUIRE(ownedSkeleton != nullptr);
    REQUIRE(ownedClip != nullptr);
    CHECK_EQ(ownedSkeleton->getBoneCount(), 2);
    CHECK_EQ(std::string(ownedSkeleton->getBoneName(1)), "hip");
    close(ownedClip->getDuration(), .8f);
    close(ownedClip->getSampleRate(), 12.f);
    CHECK_EQ(ownedClip->getSyncMarkerCount(), 2);
    CHECK_EQ(std::string(ownedClip->getSyncMarkerName(1)), "right_plant");
    AnimPose pose;
    ownedClip->sample(.4f, &pose, ownedSkeleton.get());
    close(pose.getLocalPositionZ(0), .6f, .05f);
}

TEST_CASE("animation.importer.malformedFixtureLeavesOutputPointersNull") {
    AnimSkeleton* skeleton = nullptr;
    AnimClip*     clip     = nullptr;
    bool          rejected = false;
    try {
        AnimImporter::importAnimationFixtureText("EVA 99\n", &skeleton, &clip);
    } catch (const eve::Exception&) {
        rejected = true;
    }
    REQUIRE(rejected);
    CHECK(skeleton == nullptr);
    CHECK(clip == nullptr);
}

TEST_CASE("animation.controlAnim.integratorsConvergeAndRejectUnknownMode") {
    SUB_CASE("second order tracks a step target") {
        ControlAnim control(4.f, 1.f, 1.f);
        control.setIntegrator("secondOrder");
        control.set("x", 0.f);
        control.setTarget("x", 10.f);
        for (int i = 0; i < 120; ++i) control.update(1.f / 60.f);
        close(control.get("x"), 10.f, .15f);
        REQUIRE(std::abs(control.getVelocity("x")) < .5f);
        CHECK_EQ(control.getPropertyCount(), 1);
        CHECK_EQ(std::string(control.getPropertyName(0)), "x");
    };
    SUB_CASE("critical spring approaches without material overshoot") {
        ControlAnim control(5.f, 1.f, 0.f);
        control.setIntegrator("spring");
        control.set("y", 0.f);
        control.setTarget("y", 1.f);
        for (int i = 0; i < 180; ++i) {
            control.update(1.f / 60.f);
            CHECK(control.get("y") < 1.05f);
        }
        close(control.get("y"), 1.f, .05f);
    };
    SUB_CASE("PD underdamping overshoots before settling") {
        ControlAnim control(3.f, .35f, 1.f);
        control.setIntegrator("pd");
        control.set("z", 0.f);
        control.setTarget("z", 1.f);
        float maximum = 0.f;
        for (int i = 0; i < 240; ++i) {
            control.update(1.f / 60.f);
            maximum = std::max(maximum, control.get("z"));
        }
        CHECK(maximum > 1.02f);
        close(control.get("z"), 1.f, .1f);
    };
    SUB_CASE("impulse affects rest state and unknown integrator throws") {
        ControlAnim control(2.f, .5f, 0.f);
        control.set("p", 0.f);
        control.setTarget("p", 0.f);
        control.impulse("p", 5.f);
        control.update(1.f / 60.f);
        CHECK(control.get("p") > .01f);
        bool rejected = false;
        try {
            control.setIntegrator("unknown");
        } catch (const eve::Exception&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK_EQ(control.getIntegrator(), "secondOrder");
    };
}

TEST_CASE("animation.controlPose.targetAndZeroWeightFollowContract") {
    auto        skeleton = twoBoneSkeleton();
    ControlPose control(skeleton.get());
    control.setFrequency(5.f);
    control.setDamping(1.f);
    control.setResponse(1.f);
    control.setIntegrator("secondOrder");
    AnimPose target(2);
    skeleton->applyBindPose(&target);
    target.setLocalPosition(1, 0.f, 3.f, 0.f);
    control.setTargetPose(&target);
    for (int i = 0; i < 180; ++i) control.update(1.f / 60.f);
    close(control.getPose()->getLocalPositionY(1), 3.f, .2f);
    control.setIntegrator("spring");
    control.setBoneWeight(1, 0.f);
    target.setLocalPosition(1, 4.f, 2.f, -1.f);
    control.setTargetPose(&target);
    control.update(1.f / 60.f);
    close(control.getPose()->getLocalPositionX(1), 4.f);
    close(control.getPose()->getLocalPositionY(1), 2.f);
    close(control.getPose()->getLocalPositionZ(1), -1.f);
    float proportional = 0.f;
    float derivative   = 0.f;
    pdGainsFromOmegaZeta(10.f, 1.f, proportional, derivative);
    close(proportional, 100.f);
    close(derivative, 20.f);
}

TEST_CASE("animation.trail.distanceCapacityFadeAndBoneSamplingContract") {
    AnimTrail trail(4);
    trail.setDuration(0.f);
    trail.setMinDistance(5.f);
    trail.addPoint(0.f, 0.f);
    trail.addPoint(1.f, 0.f);
    REQUIRE_EQ(trail.getPointCount(), 1);
    for (float x : {6.f, 12.f, 18.f, 24.f}) trail.addPoint(x, 0.f);
    CHECK_EQ(trail.getPointCount(), 4);
    close(trail.getPointX(0), 6.f);
    close(trail.getPointX(3), 24.f);
    trail.clear();
    trail.setDuration(.2f);
    trail.setMinDistance(0.f);
    trail.setFade(true);
    trail.setStyle("line");
    trail.addPoint(0.f, 0.f);
    trail.addPoint(10.f, 0.f);
    trail.update(.1f);
    CHECK_EQ(trail.getPointCount(), 2);
    CHECK(trail.getPointAlpha(0) < .6f);
    trail.update(.15f);
    CHECK_EQ(trail.getPointCount(), 0);

    auto     skeleton = twoBoneSkeleton();
    AnimPose pose;
    skeleton->applyBindPose(&pose);
    pose.setLocalPosition(0, 3.f, 0.f, 7.f);
    pose.setLocalPosition(1, 0.f, 2.f, 0.f);
    pose.computeWorld(skeleton.get());
    trail.sampleBone(&pose, 1, "xy");
    close(trail.getPointX(0), 3.f);
    close(trail.getPointY(0), 2.f);
    trail.clear();
    trail.sampleBone(&pose, 1, "xz");
    close(trail.getPointY(0), 7.f);
    trail.clear();
    trail.sampleBoneOffset(&pose, 1, 1.f, 0.f, 0.f, "xy");
    close(trail.getPointX(0), 4.f);
}
}  // namespace test_animation_core
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_batch_lod)
#include "animation/AnimPose.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace test_animation_batch {
using namespace eve::animation;

TEST_CASE("animation.pose.blendInterpolatesPositionAndScaleComponentwise") {
    AnimPose first(1), second(1), output;
    first.setLocalPosition(0, 0.f, 2.f, 4.f);
    first.setLocalScale(0, 1.f, 2.f, 3.f);
    second.setLocalPosition(0, 10.f, 6.f, 0.f);
    second.setLocalScale(0, 3.f, 4.f, 5.f);
    output.blendFrom(&first, &second, .25f);
    REQUIRE(std::abs(output.getLocalPositionX(0) - 2.5f) < 1e-6f);
    CHECK(std::abs(output.getLocalPositionY(0) - 3.f) < 1e-6f);
    CHECK(std::abs(output.getLocalScaleZ(0) - 3.5f) < 1e-6f);
}
}  // namespace test_animation_batch
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_binary)
#include "animation/AnimClip.h"
#include "animation/AnimClipBinary.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/unittest.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace test_animation_binary {
using namespace eve::animation;
using Bytes = std::vector<std::byte>;

void appendInteger(Bytes& bytes, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        bytes.push_back(static_cast<std::byte>((value >> (byte * 8)) & 255));
    }
}
void appendFloat(Bytes& bytes, float value) { appendInteger(bytes, std::bit_cast<std::uint32_t>(value)); }
void appendString(Bytes& bytes, const std::string& value) {
    appendInteger(bytes, static_cast<std::uint32_t>(value.size()));
    for (char character : value) bytes.push_back(static_cast<std::byte>(character));
}

Bytes validClipBytes() {
    Bytes bytes;
    appendInteger(bytes, 0x43415645);
    appendInteger(bytes, 1);
    appendInteger(bytes, 1);
    appendFloat(bytes, 2.f);
    appendFloat(bytes, 30.f);
    appendInteger(bytes, 1);
    appendString(bytes, "test");
    appendString(bytes, "root");
    appendInteger(bytes, 2);
    for (float time : {0.f, 2.f}) {
        appendFloat(bytes, time);
        appendFloat(bytes, 0.f);
        appendFloat(bytes, 0.f);
        appendFloat(bytes, 3.f * time);
    }
    appendInteger(bytes, 1);
    for (float value : {0.f, 0.f, 0.f, 0.f, 1.f}) appendFloat(bytes, value);
    appendInteger(bytes, 1);
    for (float value : {0.f, 1.f, 1.f, 1.f}) appendFloat(bytes, value);
    return bytes;
}

TEST_CASE("animation.binary.validClipLoadsAndSamplesHierarchy") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("unanimated", 0);
    skeleton.setBindPosition(1, 0.f, 1.f, 0.f);
    AnimClip clip("old");
    REQUIRE(loadAnimationTracks(clip, validClipBytes(), skeleton).ok());
    CHECK_EQ(clip.getName(), "test");
    CHECK(clip.getLoop());
    CHECK_EQ(clip.getDuration(), 2.f);
    AnimPose pose;
    clip.sample(.5f, &pose, &skeleton);
    pose.computeWorld(&skeleton);
    CHECK(std::abs(pose.getWorldPositionZ(1) - 1.5f) < 1e-6f);
    CHECK_EQ(pose.getWorldPositionY(1), 1.f);
    clip.sample(2.5f, &pose, &skeleton);
    CHECK(std::abs(pose.getLocalPositionZ(0) - 1.5f) < 1e-6f);
}

TEST_CASE("animation.binary.truncatedAndMalformedBuffersLeaveClipUnchanged") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip clip("unchanged");
    clip.setDuration(4.f);
    const Bytes valid = validClipBytes();
    for (std::size_t length = 0; length < valid.size(); ++length) {
        REQUIRE(!loadAnimationTracks(clip, {valid.data(), length}, skeleton).ok());
        CHECK_EQ(clip.getName(), "unchanged");
        CHECK_EQ(clip.getDuration(), 4.f);
    }
    std::vector<Bytes> malformed;
    auto               version = valid;
    version[4]                 = std::byte{2};
    malformed.push_back(version);
    auto trailing = valid;
    trailing.push_back(std::byte{0});
    malformed.push_back(trailing);
    auto badBone = valid;
    badBone[36]  = std::byte{'X'};
    malformed.push_back(badBone);
    auto badFlag = valid;
    badFlag[20]  = std::byte{2};
    malformed.push_back(badFlag);
    auto       nonfiniteDuration = valid;
    const auto nan               = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    for (int byte = 0; byte < 4; ++byte) {
        nonfiniteDuration[12 + byte] = static_cast<std::byte>((nan >> (byte * 8)) & 255);
    }
    malformed.push_back(nonfiniteDuration);
    for (const auto& bytes : malformed) {
        CHECK(!loadAnimationTracks(clip, bytes, skeleton).ok());
        CHECK_EQ(clip.getName(), "unchanged");
        CHECK_EQ(clip.getDuration(), 4.f);
    }
}

TEST_CASE("animation.binary.adjacentFloatKeysPreserveStepBoundary") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip clip("step");
    clip.setLoop(false);
    const float before = std::nextafter(1.f, 0.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, before, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 1.f, 0.f, 0.f, 2.f);
    clip.addPositionKey(0, 2.f, 0.f, 0.f, 2.f);
    AnimPose pose;
    for (float time : {.3f, before}) {
        clip.sample(time, &pose, &skeleton);
        REQUIRE_EQ(pose.getLocalPositionZ(0), 0.f);
    }
    for (float time : {1.f, std::nextafter(1.f, 2.f), 1.8f}) {
        clip.sample(time, &pose, &skeleton);
        CHECK_EQ(pose.getLocalPositionZ(0), 2.f);
    }
}
}  // namespace test_animation_binary
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_clip_editing)
#include "animation/AnimClip.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>

namespace test_animation_clip_editing {
using namespace eve::animation;

TEST_CASE("animation.clip.keyAndEventEditsPreserveSortedTracks") {
    AnimClip clip("editable");
    clip.setDuration(2.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 1.f, 1.f, 2.f, 3.f);
    clip.addRotationKey(0, .5f, 0.f, 0.f, 0.f, 1.f);
    clip.addScaleKey(0, .5f, 1.f, 1.f, 1.f);
    clip.addEvent(.25f, "step", "left");
    REQUIRE(clip.setPositionKey(0, 1, .1f, 4.f, 5.f, 6.f));
    CHECK_EQ(clip.getPositionKeyTime(0, 0), 0.f);
    CHECK_EQ(clip.getPositionKeyTime(0, 1), .1f);
    CHECK_EQ(clip.getPositionKeyX(0, 1), 4.f);
    REQUIRE(clip.setRotationKey(0, 0, .4f, 0.f, 2.f, 0.f, 0.f));
    CHECK(std::abs(clip.getRotationKeyY(0, 0) - 1.f) < 1e-6f);
    REQUIRE(clip.setEvent(0, .75f, "land", "hard"));
    CHECK_EQ(std::string(clip.getEventName(0)), "land");
    CHECK_EQ(clip.getEventPayload(0), "hard");
    CHECK(!clip.removePositionKey(3, 0));
    REQUIRE(clip.removeScaleKey(0, 0));
    CHECK_EQ(clip.getScaleKeyCount(0), 0);
    REQUIRE(clip.clearTrack(0));
    CHECK_EQ(clip.getPositionKeyCount(0), 0);
    CHECK_EQ(clip.getRotationKeyCount(0), 0);
    REQUIRE(clip.removeEvent(0));
    CHECK_EQ(clip.getEventCount(), 0);
}

TEST_CASE("animation.clip.semanticNotifyContractReportsMissingEvents") {
    AnimClip clip("vault");
    clip.setDuration(1.f);
    clip.addEvent(.25f, "contact.left_hand");
    clip.addEvent(.75f, "land");
    CHECK(clip.hasEvent("contact.left_hand"));
    CHECK(!clip.hasEvent("contact.right_hand"));
    REQUIRE(clip.validateNotifyContract({"contact.left_hand", "land"}).ok());
    auto missing = clip.validateNotifyContract({"contact.left_hand", "contact.right_hand", "land"});
    CHECK(!missing.ok());
    CHECK(missing.code() == eve::StatusCode::Rejected);
}
}  // namespace test_animation_clip_editing
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_constraints_sync)
#include "animation/AnimClip.h"
#include "animation/AnimConstraintStack.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSyncGroup.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace test_animation_constraints_sync {
using namespace eve::animation;

TEST_CASE("animation.constraints.footIkPlantsFootAtRequestedHeight") {
    AnimSkeleton skeleton;
    const int    hip  = skeleton.addBone("hip");
    const int    knee = skeleton.addBone("knee", hip);
    const int    foot = skeleton.addBone("foot", knee);
    skeleton.setBindPosition(knee, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(foot, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    AnimConstraintStack constraints(&skeleton);
    constraints.addFootIK(hip, knee, foot, -1.5f, 0.f, 1.f, 0.f, .1f, 1.f);
    REQUIRE_EQ(constraints.getCount(), 1);
    constraints.apply(&pose);
    CHECK(std::abs(pose.getWorldPositionY(foot) + 1.4f) < .12f);
}

TEST_CASE("animation.syncGroup.normalizedAndMarkerPhaseAlignFollowers") {
    SUB_CASE("without common markers follower uses normalized phase") {
        AnimSkeleton skeleton;
        skeleton.addBone("root");
        AnimClip leaderClip("walk"), followerClip("run");
        leaderClip.setDuration(1.f);
        followerClip.setDuration(2.f);
        leaderClip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
        leaderClip.addPositionKey(0, 1.f, 1.f, 0.f, 0.f);
        followerClip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
        followerClip.addPositionKey(0, 2.f, 2.f, 0.f, 0.f);
        AnimPlayer leader(&skeleton), follower(&skeleton);
        leader.play(&leaderClip);
        follower.play(&followerClip);
        AnimSyncGroup group;
        group.addPlayer(&leader);
        group.addPlayer(&follower);
        group.update(.25f);
        REQUIRE(std::abs(group.getPhase() - .25f) < 1e-5f);
        CHECK(std::abs(follower.getTime() - .5f) < 1e-5f);
        CHECK(!group.getUsedMarkerSync());
    };
    SUB_CASE("common foot markers align interval phase including wrap") {
        AnimSkeleton skeleton;
        skeleton.addBone("root");
        AnimClip leaderClip("walk"), followerClip("run");
        leaderClip.setDuration(2.f);
        followerClip.setDuration(1.f);
        leaderClip.addSyncMarker(.2f, "left_plant");
        leaderClip.addSyncMarker(1.2f, "right_plant");
        followerClip.addSyncMarker(.3f, "left_plant");
        followerClip.addSyncMarker(.8f, "right_plant");
        AnimPlayer leader(&skeleton), follower(&skeleton);
        leader.play(&leaderClip);
        follower.play(&followerClip);
        AnimSyncGroup group;
        group.addPlayer(&leader);
        group.addPlayer(&follower);
        group.update(.7f);
        CHECK(group.getUsedMarkerSync());
        CHECK(std::abs(group.getPhase() - .35f) < 1e-5f);
        CHECK(std::abs(follower.getTime() - .55f) < 1e-5f);
        group.update(1.f);
        CHECK(std::abs(follower.getTime() - .05f) < 1e-5f);
    };
}
}  // namespace test_animation_constraints_sync
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_curves)
#include "animation/AnimCurveLibrary.h"
#include "zeroerr/unittest.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace test_animation_curves {
using Bytes = std::vector<std::byte>;

void appendInteger(Bytes& bytes, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        bytes.push_back(static_cast<std::byte>((value >> (byte * 8)) & 255));
    }
}
void appendFloat(Bytes& bytes, float value) { appendInteger(bytes, std::bit_cast<std::uint32_t>(value)); }
void appendString(Bytes& bytes, const std::string& value) {
    appendInteger(bytes, static_cast<std::uint32_t>(value.size()));
    for (char character : value) bytes.push_back(static_cast<std::byte>(character));
}

Bytes validCurves() {
    Bytes bytes;
    appendInteger(bytes, 0x43465645);
    appendInteger(bytes, 1);
    appendInteger(bytes, 2);
    appendString(bytes, "walk");
    appendFloat(bytes, 1.f);
    appendInteger(bytes, 1);
    appendString(bytes, "contact_l");
    appendInteger(bytes, 3);
    for (auto [time, value] : {std::pair{0.f, 0.f}, std::pair{.25f, 1.f}, std::pair{1.f, 0.f}}) {
        appendFloat(bytes, time);
        appendFloat(bytes, value);
    }
    appendString(bytes, "absent");
    appendFloat(bytes, 0.f);
    appendInteger(bytes, 0);
    return bytes;
}

TEST_CASE("animation.curves.loopClampMissingAndNonfiniteSampleContract") {
    eve::animation::AnimCurveLibrary library;
    REQUIRE(library.load(validCurves()).ok());
    CHECK_EQ(library.size(), 2);
    for (double time : {.125, 1.125, -.875, 1000000.125}) {
        auto sample = library.sample("walk", "contact_l", time, true);
        REQUIRE(sample.ok());
        REQUIRE(sample.value().has_value());
        CHECK(std::abs(*sample.value() - .5f) < 1e-6f);
    }
    for (double time : {-1., 2.}) {
        auto sample = library.sample("walk", "contact_l", time, false);
        REQUIRE(sample.ok());
        CHECK_EQ(*sample.value(), 0.f);
    }
    auto missingChannel = library.sample("walk", "contact_r", 0., false);
    REQUIRE(missingChannel.ok());
    CHECK(!missingChannel.value().has_value());
    auto emptySource = library.sample("absent", "contact_l", 0., true);
    REQUIRE(emptySource.ok());
    CHECK(!emptySource.value().has_value());
    CHECK(!library.sample("missing", "contact_l", 0., false).ok());
    CHECK(!library.sample("walk", "contact_l", std::numeric_limits<double>::infinity(), true).ok());
}

TEST_CASE("animation.curves.truncationAndMalformedRecordsPreserveLastGoodLibrary") {
    const Bytes                      valid = validCurves();
    eve::animation::AnimCurveLibrary library;
    REQUIRE(library.load(valid).ok());
    for (std::size_t length = 0; length < valid.size(); ++length) {
        CHECK(!library.load(std::span(valid).first(length)).ok());
        CHECK_EQ(library.size(), 2);
    }
    std::vector<Bytes> malformed;
    auto               version = valid;
    version[4]                 = std::byte{2};
    malformed.push_back(version);
    auto trailing = valid;
    trailing.push_back(std::byte{0});
    malformed.push_back(trailing);
    auto       nonfinite = valid;
    const auto nan       = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    for (int byte = 0; byte < 4; ++byte) {
        nonfinite[20 + byte] = static_cast<std::byte>((nan >> (byte * 8)) & 255);
    }
    malformed.push_back(nonfinite);
    auto tooManySources = valid;
    tooManySources[8]   = std::byte{0xA1};
    tooManySources[9]   = std::byte{0x86};
    tooManySources[10]  = std::byte{0x01};
    malformed.push_back(tooManySources);
    auto invalidName = valid;
    invalidName[16]  = std::byte{0xff};
    malformed.push_back(invalidName);
    for (const auto& bytes : malformed) {
        CHECK(!library.load(bytes).ok());
        auto sample = library.sample("walk", "contact_l", .25, false);
        REQUIRE(sample.ok());
        REQUIRE(sample.value().has_value());
        CHECK_EQ(*sample.value(), 1.f);
    }
}
}  // namespace test_animation_curves
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_motion_normalization)
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace test_animation_motion_schema {
using namespace eve::animation;

MotionFeatureLayout positionLayout() {
    MotionFeatureLayout layout;
    layout.sampleRate = 10;
    MotionFeatureChannel position;
    position.kind   = MotionFeatureKind::Position;
    position.source = MotionFeatureSource::Pose;
    position.query  = MotionFeatureQuery::Character;
    position.bone   = 1;
    position.origin = 0;
    position.axes   = 1;
    position.weight = 1.f;
    layout.channels.push_back(position);
    return layout;
}

struct PositionClips {
    AnimSkeleton skeleton;
    AnimClip     low{"low"};
    AnimClip     high{"high"};
    PositionClips() {
        skeleton.addBone("root");
        skeleton.addBone("sample", 0);
        low.setDuration(1.f);
        high.setDuration(1.f);
        low.setLoop(false);
        high.setLoop(false);
        low.addPositionKey(1, 0.f, 0.f, 0.f, 0.f);
        high.addPositionKey(1, 0.f, 10.f, 0.f, 0.f);
    }
    void addTo(MotionDatabase& database) {
        database.addClip(&low);
        database.addClip(&high);
    }
};

TEST_CASE("animation.motionSchema.normalizationRangesChangeFeatureScaleAndOwnInput") {
    PositionClips  clips;
    MotionDatabase normalized(&clips.skeleton);
    clips.addTo(normalized);
    REQUIRE(normalized.setFeatureLayout(positionLayout()).ok());
    std::vector<MotionNormalizationRange> ranges{{0, 0.f, 1.f}, {0, 0.f, 1.f}, {1, 0.f, 1.f}};
    auto                                  configured = normalized.setFeatureNormalizationRanges(ranges);
    REQUIRE(configured.ok());
    CHECK_EQ(configured.value(), 33);
    ranges.clear();
    normalized.bake();
    float sample = 0.f;
    normalized.getFeature(11, &sample, 1);
    CHECK(std::abs(sample - 1.5f) < 1e-5f);
    MotionDatabase ordinary(&clips.skeleton);
    clips.addTo(ordinary);
    REQUIRE(ordinary.setFeatureLayout(positionLayout()).ok());
    ordinary.bake();
    ordinary.getFeature(11, &sample, 1);
    CHECK(std::abs(sample - 1.f) < 1e-5f);
}

TEST_CASE("animation.motionSchema.invalidNormalizationRangePreservesLastConfiguration") {
    PositionClips  clips;
    MotionDatabase database(&clips.skeleton);
    clips.addTo(database);
    REQUIRE(database.setFeatureLayout(positionLayout()).ok());
    const std::array valid{MotionNormalizationRange{0, 0.f, 0.f}};
    REQUIRE(database.setFeatureNormalizationRanges(valid).ok());
    for (const auto& invalid :
         std::vector<MotionNormalizationRange>{{-1, 0.f, 1.f},
                                               {2, 0.f, 1.f},
                                               {0, -.1f, 1.f},
                                               {0, .5f, .4f},
                                               {0, 0.f, 2.f},
                                               {0, 0.f, std::numeric_limits<float>::quiet_NaN()}}) {
        CHECK(!database.setFeatureNormalizationRanges(std::span(&invalid, 1)).ok());
    }
    database.bake();
    float sample = 0.f;
    database.getFeature(11, &sample, 1);
    CHECK(std::abs(sample - 10.f) < 1e-5f);
}

TEST_CASE("animation.motionSchema.velocityQueryCopiesInputsAndRejectsPartialReplacement") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip forward("forward"), backward("backward");
    for (AnimClip* clip : {&forward, &backward}) {
        const float direction = clip == &forward ? 1.f : -1.f;
        clip->setDuration(1.f);
        clip->addPositionKey(1, 0.f, 0.f, 0.f, -.5f * direction);
        clip->addPositionKey(1, 1.f, 0.f, 0.f, .5f * direction);
    }
    MotionFeatureChannel velocity;
    velocity.kind  = MotionFeatureKind::Velocity;
    velocity.query = MotionFeatureQuery::Character;
    velocity.bone  = 1;
    velocity.axes  = 4;
    MotionFeatureLayout layout{20, {velocity}};
    MotionDatabase      database(&skeleton);
    REQUIRE(database.setFeatureLayout(layout).ok());
    layout.channels[0].axes = 7;
    CHECK_EQ(database.getFeatureSize(), 1);
    database.addClip(&forward);
    database.addClip(&backward);
    database.bake();
    CHECK_EQ(database.getFrameCount(), 42);
    CHECK(!database.setFeatureLayout(layout).ok());
    for (int direction : {-1, 1}) {
        MotionMatcher                          matcher(&skeleton, &database);
        const std::array<MotionSearchRange, 2> candidates{{{0, .5f, .5f}, {1, .5f, .5f}}};
        REQUIRE(matcher.setCandidateRanges(candidates).ok());
        AnimPose current(2), previous(2);
        previous.setLocalPosition(1, 0.f, 0.f, -direction / 60.f);
        REQUIRE(matcher.setFeatureQuery(current, previous, 1.f / 60.f, {}).ok());
        previous.setLocalPosition(1, 0.f, 0.f, 100.f);
        CHECK(!matcher.setFeatureQuery(current, previous, 0.f, {}).ok());
        CHECK(!matcher.setQueryPose(current).ok());
        const std::array<MotionTrajectorySample, 3> incomplete{};
        CHECK(!matcher.setTrajectory(incomplete).ok());
        matcher.search();
        CHECK_EQ(matcher.getMatchedClipIndex(), direction == 1 ? 0 : 1);
    }
}

TEST_CASE("animation.motionSchema.invalidLayoutPreservesPreviousFeatureSize") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    MotionDatabase       database(&skeleton);
    MotionFeatureChannel velocity;
    velocity.kind               = MotionFeatureKind::Velocity;
    velocity.normalizationGroup = "speed";
    MotionFeatureLayout layout{30, {velocity}};
    REQUIRE(database.setFeatureLayout(layout).ok());
    layout.channels.push_back(velocity);
    layout.channels.back().normalizeVelocity = true;
    layout.channels.back().axes              = 0;
    CHECK(!database.setFeatureLayout(layout).ok());
    CHECK_EQ(database.getFeatureSize(), 3);
    layout.channels.pop_back();
    layout.channels[0].weight = std::numeric_limits<float>::quiet_NaN();
    CHECK(!database.setFeatureLayout(layout).ok());
    CHECK_EQ(database.getFeatureSize(), 3);
    CHECK(database.hasFeatureLayout());
}

TEST_CASE("animation.motionSchema.playRateUsesUnnormalizedTrajectoryVelocity") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip clip("travel");
    clip.setDuration(2.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 2.f, 2.f, 0.f, 0.f);
    MotionFeatureChannel velocity;
    velocity.kind                   = MotionFeatureKind::Velocity;
    velocity.source                 = MotionFeatureSource::Trajectory;
    velocity.query                  = MotionFeatureQuery::Character;
    velocity.axes                   = 1;
    velocity.sampleTime             = 0.f;
    velocity.characterSpaceVelocity = false;
    velocity.normalizeVelocity      = false;
    MotionDatabase database(&skeleton);
    REQUIRE(database.setFeatureLayout({30, {velocity}, 100.f}).ok());
    database.addClip(&clip);
    database.bake();
    MotionMatcher                          matcher(&skeleton, &database);
    const std::array<MotionSearchRange, 1> candidates{{{0, 0.f, 0.f}}};
    REQUIRE(matcher.setCandidateRanges(candidates).ok());
    REQUIRE(matcher.setPlayRateRange(.85f, 1.15f).ok());
    CHECK(!matcher.setPlayRateRange(1.2f, .8f).ok());
    CHECK_EQ(matcher.getPlayRateMinimum(), .85f);
    CHECK_EQ(matcher.getPlayRateMaximum(), 1.15f);
    AnimPose                                     pose(1);
    std::array<MotionFeatureTrajectorySample, 1> samples{};
    samples[0].vx = 2.f;
    REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
    matcher.search();
    CHECK(std::abs(matcher.getPlayRate() - 1.15f) < 1e-4f);
    matcher.update(.5f);
    CHECK(std::abs(matcher.getMatchedTime() - .575f) < 1e-4f);
}

struct CandidateClips {
    AnimSkeleton   skeleton;
    AnimClip       first{"first"};
    AnimClip       second{"second"};
    MotionDatabase database{&skeleton};
    CandidateClips() {
        skeleton.addBone("root");
        for (AnimClip* clip : {&first, &second}) {
            clip->setDuration(1.f);
            clip->setLoop(false);
            clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
            database.addClip(clip);
        }
        database.bake();
    }
};

TEST_CASE("animation.motionMatcher.transitionBlocksPreventEntryButKeepContinuation") {
    CandidateClips clips;
    MotionMatcher  continuing(&clips.skeleton, &clips.database);
    continuing.setSearchInterval(10.f);
    const std::array seed{MotionSearchRange{0, 0.f, 0.f}};
    REQUIRE(continuing.setCandidateRanges(seed).ok());
    continuing.search();
    continuing.update(.3f);
    const float playhead = continuing.getMatchedTime();
    CHECK(playhead > .1f);
    std::vector<MotionSearchRange> candidates{{0, 0.f, 1.f, -100.f, false, {{.1f, .8f}}}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(continuing.setCandidateRanges(candidates).ok());
    continuing.search();
    CHECK_EQ(continuing.getMatchedClipIndex(), 0);
    CHECK(std::abs(continuing.getMatchedTime() - playhead) < 1e-6f);
    candidates[0].start = .2f;
    candidates[0].end   = .7f;
    MotionMatcher fresh(&clips.skeleton, &clips.database);
    REQUIRE(fresh.setCandidateRanges(candidates).ok());
    fresh.search();
    CHECK_EQ(fresh.getMatchedClipIndex(), 1);
    REQUIRE(continuing.setCandidateRanges(std::span(candidates).last(1)).ok());
    continuing.search();
    CHECK_EQ(continuing.getMatchedClipIndex(), 1);
}

TEST_CASE("animation.motionMatcher.invalidBlocksAreAtomicAndAllBlockedCanRecover") {
    CandidateClips   clips;
    MotionMatcher    matcher(&clips.skeleton, &clips.database);
    const std::array blocked{MotionSearchRange{0, 0.f, 1.f, 0.f, false, {{0.f, 2.f}}}};
    REQUIRE(matcher.setCandidateRanges(blocked).ok());
    matcher.search();
    CHECK_EQ(matcher.getMatchedFrame(), -1);
    for (const auto& invalid : {MotionTransitionBlock{-1.f, 1.f},
                                {1.f, 1.f},
                                {2.f, 1.f},
                                {0.f, std::numeric_limits<float>::infinity()},
                                {std::numeric_limits<float>::quiet_NaN(), 1.f}}) {
        const std::array attempted{MotionSearchRange{1, 0.f, 1.f, 0.f, false, {invalid}}};
        CHECK(!matcher.setCandidateRanges(attempted).ok());
        matcher.search();
        CHECK_EQ(matcher.getMatchedFrame(), -1);
    }
    const std::array recovered{MotionSearchRange{1, 0.f, 1.f}};
    REQUIRE(matcher.setCandidateRanges(recovered).ok());
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 1);
}

TEST_CASE("animation.motionMatcher.costBiasAndReplacementRangeSelectValidEntry") {
    CandidateClips clips;
    MotionMatcher  matcher(&clips.skeleton, &clips.database);
    matcher.setSearchInterval(10.f);
    const std::array seed{MotionSearchRange{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    CHECK_EQ(matcher.getMatchedClipIndex(), 0);
    std::vector<MotionSearchRange> candidates{{0, .8f, 1.f, 10.f}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(candidates).ok());
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 1);
    candidates = {{0, 0.f, 1.f, -2.f, false, {}, 10.f}, {0, 0.f, 1.f, -1.f, false, {}, -10.f}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(candidates).ok());
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 1);
}
}  // namespace test_animation_motion_schema
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_motion)
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
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace test_animation_motion {
using namespace eve::animation;

eve::SimulationStep stepAt(std::uint64_t tick, double seconds) {
    return eve::SimulationStep{eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("test dt")};
}

void close(float actual, float expected, float tolerance = .001f) { CHECK(std::abs(actual - expected) < tolerance); }

TEST_CASE("animation.motion.floatBindingAdvancesAtInjectedSimulationSteps") {
    Animation*       animation = Animation::create();
    float            output    = -1.f;
    FloatPointerSink sink(&output);
    auto             handle = animation->motion(0.f, 100.f, 1.f).ease("linear").bind(sink).expect("spawn");
    CHECK(animation->motions().isActive(handle));
    close(output, 0.f);
    REQUIRE(animation->advance(stepAt(1, .5)).ok());
    close(output, 50.f);
    REQUIRE(animation->advance(stepAt(2, .5)).ok());
    close(output, 100.f);
    CHECK(!animation->motions().isActive(handle));
    CHECK_EQ(animation->getMotionCount(), 0);
    auto stale = animation->motions().floatValue(handle);
    CHECK(!stale.ok());
    stale.ignore("completed handle must be stale");
}

TEST_CASE("animation.motion.completeAndCancelFireDistinctCallbacks") {
    Animation*       animation = Animation::create();
    float            output    = 0.f;
    FloatPointerSink sink(&output);
    int              completed = 0;
    int              cancelled = 0;
    auto             first =
        animation->motion(0.f, 10.f, 1.f).onComplete([&] { ++completed; }).bind(sink).expect("complete motion");
    REQUIRE(animation->advance(stepAt(1, .25)).ok());
    REQUIRE(animation->motions().complete(first).ok());
    close(output, 10.f);
    CHECK_EQ(completed, 1);
    CHECK(!animation->motions().isActive(first));
    auto second = animation->motion(0.f, 10.f, 1.f).onCancel([&] { ++cancelled; }).bind(sink).expect("cancel motion");
    REQUIRE(animation->advance(stepAt(2, .1)).ok());
    REQUIRE(animation->motions().cancel(second).ok());
    CHECK_EQ(cancelled, 1);
    CHECK_EQ(completed, 1);
    CHECK(!animation->motions().isActive(second));
}

TEST_CASE("animation.motion.vec2YoyoReturnsToBothStartingCoordinates") {
    Animation*      animation = Animation::create();
    float           x = 0.f, y = 0.f;
    Vec2PointerSink sink(&x, &y);
    auto            handle = animation->motionVec2({0.f, 0.f}, {10.f, 20.f}, 1.f)
                      .ease("linear")
                      .loops(2, MotionLoopMode::Yoyo)
                      .bind(sink)
                      .expect("vec2");
    REQUIRE(animation->advance(stepAt(1, 1.)).ok());
    close(x, 10.f);
    close(y, 20.f);
    CHECK(animation->motions().isActive(handle));
    REQUIRE(animation->advance(stepAt(2, 1.)).ok());
    close(x, 0.f);
    close(y, 0.f);
    CHECK(!animation->motions().isActive(handle));
}

TEST_CASE("animation.motion.easingAndOscillationBoundaryContract") {
    REQUIRE_EQ(evaluateMotionEase(0.f, "inBack"), 0.f);
    CHECK_EQ(evaluateMotionEase(1.f, "outBack"), 1.f);
    CHECK(evaluateMotionEase(.5f, "outBack") > 1.f);
    CHECK_EQ(evaluateMotionEase(0.f, "inElastic"), 0.f);
    CHECK_EQ(evaluateMotionEase(1.f, "outElastic"), 1.f);
    CHECK_EQ(evaluateMotionEase(0.f, "inBounce"), 0.f);
    CHECK_EQ(evaluateMotionEase(1.f, "outBounce"), 1.f);
    close(evaluateMotionOscillation(0.f, 10, 0.f), 0.f);
    close(evaluateMotionOscillation(1.f, 10, 0.f), 0.f);
    close(evaluateMotionOscillation(.25f, 2, 0.f), 1.f);
    const float first = evaluateMotionShakeSign(42u, 8, .3f, 0);
    close(first, evaluateMotionShakeSign(42u, 8, .3f, 0));
    CHECK(first >= -1.f);
    CHECK(first <= 1.f);
}

TEST_CASE("animation.motion.sequenceAppendJoinInsertAndCompletionContract") {
    Animation*       animation = Animation::create();
    float            first = -1.f, second = -1.f;
    FloatPointerSink firstSink(&first), secondSink(&second);
    auto             sequence = animation->sequence();
    REQUIRE(sequence.append(std::move(animation->motion(0.f, 10.f, 1.f).ease("linear").to(firstSink))).ok());
    REQUIRE(sequence.appendInterval(.5f).ok());
    REQUIRE(sequence.join(std::move(animation->motion(0.f, 20.f, 1.f).ease("linear").to(secondSink))).ok());
    CHECK_EQ(sequence.itemCount(), 2);
    close(sequence.duration(), 1.5f);
    auto playback = sequence.run().expect("run sequence");
    CHECK_EQ(playback.childCount(), 2);
    REQUIRE(animation->advance(stepAt(1, .5)).ok());
    close(first, 5.f);
    close(second, 10.f);
    REQUIRE(animation->advance(stepAt(2, .5)).ok());
    close(first, 10.f);
    close(second, 20.f);
    CHECK(!playback.isActive());

    auto invalid  = animation->sequence();
    auto infinite = invalid.append(std::move(animation->motion(0.f, 1.f, .2f).loops(-1, MotionLoopMode::Restart)));
    CHECK(!infinite.ok());
    infinite.ignore("infinite child cannot yield finite sequence duration");
}

TEST_CASE("animation.motion.sequenceInsertStartsAtOffsetAndCompleteFlushesChildren") {
    Animation*       animation = Animation::create();
    float            x = 0.f, y = 0.f;
    FloatPointerSink xSink(&x), ySink(&y);
    auto             sequence = animation->sequence();
    REQUIRE(sequence.append(std::move(animation->motion(0.f, 10.f, 1.f).ease("linear").to(xSink))).ok());
    REQUIRE(sequence.insert(.5f, std::move(animation->motion(0.f, 100.f, .5f).ease("linear").to(ySink))).ok());
    auto playback = sequence.run().expect("run inserted sequence");
    REQUIRE(animation->advance(stepAt(1, .5)).ok());
    close(x, 5.f);
    close(y, 0.f);
    REQUIRE(animation->advance(stepAt(2, .25)).ok());
    close(y, 50.f, .01f);
    REQUIRE(playback.complete().ok());
    close(x, 10.f);
    close(y, 100.f);
    CHECK(!playback.isActive());
}

TEST_CASE("animation.motion.punchAndShakeReturnToBaseAfterDuration") {
    Animation*       animation = Animation::create();
    float            output    = -1.f;
    FloatPointerSink sink(&output);
    auto             punch = animation->punch(0.f, 10.f, 1.f).frequency(2).dampingRatio(0.f).bind(sink).expect("punch");
    close(output, 0.f);
    REQUIRE(animation->advance(stepAt(1, .25)).ok());
    close(output, 10.f, .01f);
    REQUIRE(animation->advance(stepAt(2, .75)).ok());
    close(output, 0.f);
    CHECK(!animation->motions().isActive(punch));
    auto shake = animation->shake(0.f, 5.f, .5f).frequency(4).dampingRatio(0.f).seed(7).bind(sink).expect("shake");
    REQUIRE(animation->advance(stepAt(3, .1)).ok());
    CHECK(std::abs(output) > .001f);
    REQUIRE(animation->advance(stepAt(4, .4)).ok());
    close(output, 0.f);
    CHECK(!animation->motions().isActive(shake));
}

TEST_CASE("animation.motion.colorAndQuaternionBindingsInterpolateDistinctValueKinds") {
    Animation*       animation = Animation::create();
    float            red = 0.f, green = 0.f, blue = 0.f, alpha = 0.f;
    ColorPointerSink colorSink(&red, &green, &blue, &alpha);
    auto             color = animation->motionColor({0.f, 0.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}, 1.f)
                     .ease("linear")
                     .bind(colorSink)
                     .expect("color");
    REQUIRE(animation->advance(stepAt(1, .5)).ok());
    close(red, .5f);
    close(alpha, 1.f);
    REQUIRE(animation->advance(stepAt(2, .5)).ok());
    close(red, 1.f);
    CHECK(!animation->motions().isActive(color));
    float           qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
    QuatPointerSink quaternionSink(&qx, &qy, &qz, &qw);
    auto            quaternion = animation->motionQuat({0.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 0.f}, 1.f)
                          .ease("linear")
                          .bind(quaternionSink)
                          .expect("quaternion");
    REQUIRE(animation->advance(stepAt(3, .5)).ok());
    CHECK(std::abs(qy) > .1f);
    close(std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw), 1.f);
    REQUIRE(animation->advance(stepAt(4, .5)).ok());
    close(qy, 1.f);
    close(qw, 0.f);
    CHECK(!animation->motions().isActive(quaternion));
}

TEST_CASE("animation.motion.preallocatedFloatSlotsRecycleAfterCompletion") {
    Animation* animation = Animation::create();
    animation->ensureMotionCapacity(128);
    CHECK_EQ(animation->getMotionFloatCapacity(), 128);
    CHECK_EQ(animation->getMotionFloatFreeCount(), 128);
    std::vector<float>                            values(8, -1.f);
    std::vector<std::unique_ptr<FloatBufferSink>> sinks;
    for (std::size_t index = 0; index < values.size(); ++index) {
        sinks.push_back(std::make_unique<FloatBufferSink>(values.data(), index));
        animation->motion(0.f, static_cast<float>(index + 1), 1.f)
            .ease("linear")
            .bind(*sinks.back())
            .expect("buffer motion");
    }
    CHECK_EQ(animation->getMotionFloatFreeCount(), 120);
    REQUIRE(animation->advance(stepAt(1, .5)).ok());
    for (std::size_t index = 0; index < values.size(); ++index) {
        close(values[index], .5f * static_cast<float>(index + 1));
    }
    REQUIRE(animation->advance(stepAt(2, .5)).ok());
    CHECK_EQ(animation->getMotionFloatFreeCount(), 128);
    CHECK_EQ(animation->getMotionFloatCapacity(), 128);
}

void authorLocomotion(AnimClip& clip, float speed) {
    clip.setDuration(.8f);
    clip.setLoop(true);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, .8f, 0.f, 0.f, speed * .8f);
    clip.addPositionKey(1, 0.f, 0.f, .1f, -.2f);
    clip.addPositionKey(1, .4f, 0.f, .3f, .2f);
    clip.addPositionKey(1, .8f, 0.f, .1f, -.2f);
}

TEST_CASE("animation.motionMatcher.candidateRangesConstrainSelectionAtomically") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip idle("idle"), walk("walk");
    authorLocomotion(idle, 0.f);
    authorLocomotion(walk, 2.f);
    MotionDatabase database(&skeleton);
    database.addFeatureBone(1);
    database.addClip(&idle);
    database.addClip(&walk);
    database.bake();
    MotionMatcher    matcher(&skeleton, &database);
    const std::array first{MotionSearchRange{1, .2f, .5f, -.2f, true}};
    REQUIRE(matcher.setCandidateRanges(first).ok());
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 1);
    CHECK(matcher.getMatchedTime() >= .19999f);
    CHECK(matcher.getMatchedTime() <= .50001f);
    for (const auto& invalid : std::vector<MotionSearchRange>{
             {9, 0.f, .5f}, {0, .6f, .2f}, {0, 0.f, 9.f}, {0, 0.f, .1f, std::numeric_limits<float>::quiet_NaN()}}) {
        CHECK(!matcher.setCandidateRanges(std::span(&invalid, 1)).ok());
        matcher.search();
        CHECK_EQ(matcher.getMatchedClipIndex(), 1);
    }
    CHECK(!matcher.setCandidateRanges({}).ok());
    const std::array second{MotionSearchRange{0, 0.f, .1f}};
    REQUIRE(matcher.setCandidateRanges(second).ok());
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 0);
    CHECK(matcher.getMatchedTime() <= .10001f);
}

TEST_CASE("animation.motionMatcher.queryPoseIsOwnedAndInvalidTrajectoryIsRejected") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip left("left"), right("right");
    left.setDuration(1.f);
    right.setDuration(1.f);
    left.setLoop(true);
    right.setLoop(true);
    left.addPositionKey(1, 0.f, -1.f, 0.f, 0.f);
    right.addPositionKey(1, 0.f, 1.f, 0.f, 0.f);
    MotionDatabase database(&skeleton);
    database.addFeatureBone(1);
    database.addClip(&left);
    database.addClip(&right);
    database.bake();
    MotionMatcher matcher(&skeleton, &database);
    matcher.setPoseWeight(1.f);
    AnimPose input(2);
    input.setLocalPosition(1, 1.f, 0.f, 0.f);
    REQUIRE(matcher.setQueryPose(input).ok());
    AnimPose wrongSkeleton(1);
    CHECK(!matcher.setQueryPose(wrongSkeleton).ok());
    input.setLocalPosition(1, -1.f, 0.f, 0.f);
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 1);
    std::array<MotionTrajectorySample, 3> trajectory{};
    REQUIRE(matcher.setTrajectory(trajectory).ok());
    trajectory[1].yaw = std::numeric_limits<float>::infinity();
    CHECK(!matcher.setTrajectory(trajectory).ok());
    CHECK(!matcher.setTrajectory(std::span(trajectory).first(2)).ok());
}

TEST_CASE("animation.motionMatcher.loopDatabasePreservesTravelThroughWrap") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip walk("walk");
    authorLocomotion(walk, 1.7f);
    MotionDatabase database(&skeleton);
    database.addFeatureBone(1);
    database.addClip(&walk);
    database.bake();
    for (int frame = 0; frame < database.getFrameCount(); ++frame) {
        close(database.frameAt(frame).velZ, 1.7f);
        for (int feature = 2; feature < 8; ++feature) {
            close(database.frameAt(frame).feature[feature], 0.f, .01f);
        }
    }
}

TEST_CASE("animation.motionMatcher.poseInertiaStartsAtSourceAndConvergesToTarget") {
    detail::PoseInertia inertia;
    AnimPose            source(1), previous(1), target(1), previousTarget(1), output(1);
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.setLocalPosition(0, 1.97f, 0.f, 0.f);
    target.setLocalPosition(0, 7.f, 0.f, 0.f);
    previousTarget.setLocalPosition(0, 6.99f, 0.f, 0.f);
    inertia.remember(previous, .01f);
    inertia.begin(source, target, previousTarget, .3f);
    inertia.apply(target, 0.f, output);
    close(output.getLocalPositionX(0), 2.f, 1e-5f);
    target.setLocalPosition(0, 7.0001f, 0.f, 0.f);
    inertia.apply(target, .0001f, output);
    close((output.getLocalPositionX(0) - 2.f) / .0001f, 3.f, .02f);
    inertia.apply(target, .3f, output);
    close(output.getLocalPositionX(0), 7.0001f, 1e-5f);
}

TEST_CASE("animation.motionMatcher.oneShotExhaustionHandsOffToLoop") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("left", 0);
    skeleton.addBone("right", 0);
    skeleton.addBone("pelvis", 0);
    AnimClip action("action"), loop("loop");
    action.setDuration(.2f);
    loop.setDuration(.4f);
    loop.setLoop(true);
    MotionDatabase database(&skeleton);
    REQUIRE(database.setLocomotionFeatures(1, 2, 3).ok());
    database.addClip(&action);
    database.addClip(&loop);
    database.bake();
    MotionMatcher matcher(&skeleton, &database);
    matcher.setSearchInterval(10.f);
    const std::array candidates{MotionSearchRange{0, 0.f, .2f, -1.f, true}, MotionSearchRange{1, 0.f, .4f, 0.f, false}};
    REQUIRE(matcher.setCandidateRanges(candidates).ok());
    AnimPose                              pose(4);
    std::array<MotionLocomotionSample, 5> locomotion{};
    REQUIRE(matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, locomotion).ok());
    matcher.search();
    CHECK_EQ(matcher.getMatchedClipIndex(), 0);
    for (int frame = 0; frame < 15; ++frame) matcher.update(1.f / 60.f);
    CHECK_EQ(matcher.getMatchedClipIndex(), 1);
    CHECK(matcher.getMatchedTime() > 0.f);
}
}  // namespace test_animation_motion
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_retarget_compression)
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>

namespace test_animation_retarget {
using namespace eve::animation;

TEST_CASE("animation.clip.compressionRemovesRedundantKeysWithinTolerance") {
    AnimClip clip("linear");
    clip.setDuration(1.f);
    for (int index = 0; index <= 10; ++index) {
        const float time = index / 10.f;
        clip.addPositionKey(0, time, 2.f * time, -time, .5f * time);
        clip.addRotationKey(0, time, 0.f, 0.f, 0.f, 1.f);
        clip.addScaleKey(0, time, 1.f + time, 1.f, 1.f);
    }
    CHECK_EQ(clip.compress(1e-5f, .01f, 1e-5f), 27);
    REQUIRE_EQ(clip.getPositionKeyCount(0), 2);
    CHECK_EQ(clip.getRotationKeyCount(0), 2);
    CHECK_EQ(clip.getScaleKeyCount(0), 2);
    AnimPose sampled;
    clip.sample(.35f, &sampled);
    CHECK(std::abs(sampled.getLocalPositionX(0) - .7f) < 1e-5f);
    CHECK(std::abs(sampled.getLocalPositionY(0) + .35f) < 1e-5f);
}

TEST_CASE("animation.retarget.bindRelativeTranslationRotationAndEventsSurvive") {
    AnimSkeleton source, target;
    const int    sourceRoot = source.addBone("root");
    const int    sourceHand = source.addBone("hand", sourceRoot);
    source.setBindPosition(sourceHand, 0.f, 1.f, 0.f);
    const int targetRoot = target.addBone("root");
    const int targetHand = target.addBone("hand", targetRoot);
    target.setBindPosition(targetHand, 0.f, 2.f, 0.f);
    AnimClip clip("wave");
    clip.setDuration(1.f);
    clip.setLoop(false);
    clip.addPositionKey(sourceHand, 0.f, 0.f, 1.f, 0.f);
    clip.addPositionKey(sourceHand, 1.f, .5f, 1.f, 0.f);
    clip.addRotationKey(sourceHand, 0.f, 0.f, 0.f, 0.f, 1.f);
    clip.addRotationKey(sourceHand, 1.f, 0.f, 0.f, .70710678f, .70710678f);
    clip.addEvent(.5f, "contact");
    std::unique_ptr<AnimClip> converted(clip.retarget(&source, &target));
    REQUIRE(converted != nullptr);
    CHECK_EQ(converted->getName(), "wave_retargeted");
    CHECK_EQ(converted->getEventCount(), 1);
    AnimPose pose;
    converted->sample(1.f, &pose, &target);
    CHECK(std::abs(pose.getLocalPositionX(targetHand) - 1.f) < 1e-5f);
    CHECK(std::abs(pose.getLocalPositionY(targetHand) - 2.f) < 1e-5f);
    CHECK(std::abs(pose.getLocalRotationZ(targetHand) - .70710678f) < 1e-5f);
}

TEST_CASE("animation.retarget.profileMapsDifferentNamesAndReportsUnmatchedTargets") {
    AnimSkeleton source, target;
    const int    sourceRoot = source.addBone("mixamorig:Root");
    const int    sourceHips = source.addBone("mixamorig:Hips", sourceRoot);
    source.setBindPosition(sourceHips, 0.f, 1.f, 0.f);
    const int targetRoot   = target.addBone("root");
    const int targetPelvis = target.addBone("pelvis", targetRoot);
    target.addBone("weapon_socket", targetPelvis);
    target.setBindPosition(targetPelvis, 0.f, 2.f, 0.f);
    AnimClip clip("walk");
    clip.setDuration(1.f);
    clip.setLoop(false);
    clip.addPositionKey(sourceHips, 0.f, 0.f, 1.f, 0.f);
    clip.addPositionKey(sourceHips, 1.f, 1.f, 1.f, 0.f);
    AnimRetargetProfile profile;
    profile.addBoneMapping("mixamorig:Hips", "pelvis");
    profile.setRootBones("mixamorig:Hips", "pelvis");
    std::unique_ptr<AnimClip> converted(clip.retargetWithProfile(&source, &target, &profile));
    REQUIRE(converted != nullptr);
    CHECK_EQ(profile.getMatchedBoneCount(), 2);
    CHECK_EQ(profile.getUnmatchedBoneCount(), 1);
    CHECK_EQ(profile.getUnmatchedTargetBone(0), "weapon_socket");
    AnimPose pose;
    converted->sample(1.f, &pose, &target);
    CHECK(std::abs(pose.getLocalPositionX(targetPelvis) - 2.f) < 1e-5f);
    CHECK(std::abs(pose.getLocalPositionY(targetPelvis) - 2.f) < 1e-5f);
}
}  // namespace test_animation_retarget
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_root_motion_policy)
#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/RootMotionPolicy.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <memory>

namespace test_animation_root_policy {
using namespace eve::animation;

std::unique_ptr<AnimSkeleton> skeleton() {
    auto      value = std::make_unique<AnimSkeleton>();
    const int root  = value->addBone("root", -1);
    const int hip   = value->addBone("hip", root);
    value->setBindPosition(hip, 0.f, 1.f, 0.f);
    return value;
}

std::unique_ptr<AnimClip> walk(float speed, float duration) {
    auto clip = std::make_unique<AnimClip>("walk");
    clip->setLoop(true);
    clip->setDuration(duration);
    clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip->addPositionKey(0, duration, 0.f, 0.f, speed * duration);
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, duration, 0.f, 1.f, speed * duration);
    return clip;
}

void close(float actual, float expected, float tolerance = 1e-4f) { CHECK(std::abs(actual - expected) < tolerance); }

TEST_CASE("animation.rootMotion.verticalLockAndFacingRotationApplyInOrder") {
    TransformTRS movement = TransformTRS::identity();
    movement.pz           = 1.f;
    RootMotionPolicy policy;
    policy.applySpace   = RootMotionApplySpace::CharacterFacing;
    policy.characterYaw = 1.57079632679f;
    auto turned         = applyRootMotionPolicy(movement, policy);
    close(turned.px, 1.f);
    close(turned.pz, 0.f);
    policy.lockAxes = RootMotionLockAxes::X;
    auto locked     = applyRootMotionPolicy(movement, policy);
    close(locked.px, 0.f);
    close(locked.pz, 0.f);
    policy.lockAxes = RootMotionLockAxes::VerticalY;
    auto     source = skeleton();
    AnimClip diagonal("diagonal");
    diagonal.setDuration(1.f);
    diagonal.setLoop(false);
    diagonal.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    diagonal.addPositionKey(0, 1.f, 1.f, .5f, 2.f);
    policy.applySpace   = RootMotionApplySpace::BoneLocal;
    policy.characterYaw = 0.f;
    AnimPlayer player(source.get());
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(&diagonal);
    player.update(.5f);
    close(player.getRootMotionX(), .5f);
    close(player.getRootMotionY(), 0.f);
    close(player.getRootMotionZ(), 1.f);
}

TEST_CASE("animation.rootMotion.translationBakeKeepsBindRootAcrossLoop") {
    auto             source = skeleton();
    auto             clip   = walk(2.f, 1.f);
    AnimPlayer       player(source.get());
    RootMotionPolicy policy;
    policy.lockAxes                = RootMotionLockAxes::VerticalY;
    policy.bakeTranslationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(.9f);
    close(player.getPose()->getLocalPositionZ(0), 0.f);
    player.update(.2f);
    close(player.getPose()->getLocalPositionZ(0), 0.f);
    CHECK(player.getRootMotionZ() > .3f);
}

TEST_CASE("animation.rootMotion.rotationBakeRestoresBindRotation") {
    auto     source = skeleton();
    AnimClip spin("spin");
    spin.setLoop(true);
    spin.setDuration(1.f);
    spin.addRotationKey(0, 0.f, 0.f, 0.f, 0.f, 1.f);
    spin.addRotationKey(0, 1.f, 0.f, 1.f, 0.f, 0.f);
    AnimPlayer       player(source.get());
    RootMotionPolicy policy;
    policy.bakeRotationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(&spin);
    player.update(.5f);
    close(player.getPose()->getLocalRotationY(0), 0.f);
    close(player.getPose()->getLocalRotationW(0), 1.f);
    player.update(.5f);
    close(player.getPose()->getLocalRotationY(0), 0.f);
    close(player.getPose()->getLocalRotationW(0), 1.f);
}

TEST_CASE("animation.rootMotion.crossFadePublishesBlendedDeltaWithoutRootOffset") {
    auto     source  = skeleton();
    auto     walking = walk(2.f, 1.f);
    AnimClip idle("offset-idle");
    idle.setLoop(true);
    idle.setDuration(1.f);
    idle.addPositionKey(0, 0.f, 0.f, 0.f, 5.f);
    idle.addPositionKey(0, 1.f, 0.f, 0.f, 5.f);
    AnimPlayer       player(source.get());
    RootMotionPolicy policy;
    policy.bakeTranslationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(walking.get());
    player.update(.5f);
    close(player.getRootMotionZ(), 1.f, .02f);
    player.crossFade(&idle, .2f);
    player.update(.1f);
    CHECK(player.getRootMotionZ() > .05f);
    CHECK(player.getRootMotionZ() < .2f);
    close(player.getPose()->getLocalPositionZ(0), 0.f);
}

TEST_CASE("animation.rootMotion.parseNamesAndRejectNonfiniteYaw") {
    CHECK(!parseRootMotionLockAxes("sideways").ok());
    CHECK(!parseRootMotionApplySpace("world").ok());
    auto axes = parseRootMotionLockAxes("horizontal");
    REQUIRE(axes.ok());
    CHECK(axes.value() == RootMotionLockAxes::HorizontalXZ);
    auto space = parseRootMotionApplySpace("facing");
    REQUIRE(space.ok());
    CHECK(space.value() == RootMotionApplySpace::CharacterFacing);
    auto             source = skeleton();
    AnimPlayer       player(source.get());
    RootMotionPolicy policy;
    policy.characterYaw = std::numeric_limits<float>::quiet_NaN();
    CHECK(!player.setRootMotionPolicy(policy).ok());
    CHECK(!player.setRootMotionCharacterYaw(std::numeric_limits<float>::infinity()).ok());
}
}  // namespace test_animation_root_policy
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_skinned)
#include "animation/AnimMath.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "graphics/Mesh.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>

namespace test_animation_skinned {
using namespace eve::animation;

TEST_CASE("animation.skin.identityTrsMatrixPreservesPointCoordinates") {
    const TransformTRS identity = TransformTRS::identity();
    const Mat4         matrix   = Mat4::fromTRS(identity);
    for (int index : {0, 5, 10, 15}) REQUIRE(std::abs(matrix.m[index] - 1.f) < 1e-5f);
    float x = 0.f, y = 0.f, z = 0.f;
    matrix.transformPoint(1.f, 2.f, 3.f, x, y, z);
    CHECK(std::abs(x - 1.f) < 1e-5f);
    CHECK(std::abs(y - 2.f) < 1e-5f);
    CHECK(std::abs(z - 3.f) < 1e-5f);
}

TEST_CASE("animation.skin.meshPaletteOwnsMatrixCopyAndRejectsBadCounts") {
    eve::graphics::Mesh   mesh;
    std::array<float, 32> matrices{};
    matrices[0] = matrices[5] = matrices[10] = matrices[15] = 1.f;
    matrices[16] = matrices[21] = matrices[26] = matrices[31] = 1.f;
    matrices[28]                                              = 3.5f;
    REQUIRE(mesh.setSkinPalette(matrices.data(), 2).ok());
    CHECK_EQ(mesh.getSkinPaletteCount(), 2);
    CHECK(std::abs(mesh.skinPalette()[28] - 3.5f) < 1e-6f);
    matrices[28] = 9.f;
    CHECK(std::abs(mesh.skinPalette()[28] - 3.5f) < 1e-6f);
    CHECK(!mesh.setSkinPalette(nullptr, 1).ok());
    CHECK(!mesh.setSkinPalette(matrices.data(), -1).ok());
}

TEST_CASE("animation.skin.childWorldMatrixIncludesParentTranslation") {
    AnimSkeleton skeleton;
    const int    root  = skeleton.addBone("root", -1);
    const int    child = skeleton.addBone("child", root);
    skeleton.setBindPosition(child, 0.f, 1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    pose.setLocalPosition(root, 10.f, 0.f, 0.f);
    pose.computeWorld(&skeleton);
    REQUIRE(std::abs(pose.getWorldPositionX(child) - 10.f) < 1e-4f);
    CHECK(std::abs(pose.getWorldPositionY(child) - 1.f) < 1e-4f);
    float matrix[16];
    pose.getWorldMatrix(child, matrix);
    CHECK(std::abs(matrix[12] - 10.f) < 1e-4f);
    CHECK(std::abs(matrix[13] - 1.f) < 1e-4f);
    CHECK(std::abs(pose.getWorldMatrixElement(child, 12) - 10.f) < 1e-4f);
}
}  // namespace test_animation_skinned
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_smr_retarget)
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSmr.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace test_animation_smr {
using namespace eve::animation;

void armature(AnimSkeleton& skeleton, float torso, float upper, float lower) {
    const int hips     = skeleton.addBone("Hips");
    const int chest    = skeleton.addBone("Spine", hips);
    const int shoulder = skeleton.addBone("LeftShoulder", chest);
    const int elbow    = skeleton.addBone("LeftElbow", shoulder);
    const int hand     = skeleton.addBone("LeftHand", elbow);
    skeleton.setBindPosition(chest, 0.f, torso, 0.f);
    skeleton.setBindPosition(shoulder, .15f, 0.f, 0.f);
    skeleton.setBindPosition(elbow, upper, 0.f, 0.f);
    skeleton.setBindPosition(hand, lower, 0.f, 0.f);
}

float handChestGap(const AnimSkeleton& skeleton, const AnimClip& clip) {
    AnimPose pose;
    clip.sample(0.f, &pose, &skeleton);
    pose.computeWorld(&skeleton);
    const int hand  = skeleton.findBone("LeftHand");
    const int chest = skeleton.findBone("Spine");
    return std::hypot(pose.getWorldPositionX(hand) - pose.getWorldPositionX(chest),
                      pose.getWorldPositionY(hand) - pose.getWorldPositionY(chest),
                      pose.getWorldPositionZ(hand) - pose.getWorldPositionZ(chest));
}

TEST_CASE("animation.smr.sensorCloudTagsTorsoAndArmBones") {
    AnimSkeleton skeleton;
    armature(skeleton, 1.f, .3f, .3f);
    const auto cloud = AnimSmrSensorCloud::fromSkeleton(&skeleton);
    REQUIRE(cloud.getSensorCount() > 0);
    bool torso = false, arm = false;
    for (int index = 0; index < cloud.getSensorCount(); ++index) {
        torso |= cloud.getSensorPart(index) == AnimSmrBodyPart::Torso;
        arm |= cloud.getSensorPart(index) == AnimSmrBodyPart::Arm;
    }
    CHECK(torso);
    CHECK(arm);
}

TEST_CASE("animation.smr.interactionCorrectionImprovesScaledHandTorsoProximity") {
    AnimSkeleton source, target;
    armature(source, 1.f, .28f, .28f);
    armature(target, 1.8f, .7f, .7f);
    AnimClip reach("reach");
    reach.setDuration(0.f);
    reach.setSampleRate(30.f);
    reach.addRotationKey(source.findBone("LeftShoulder"), 0.f, 0.f, 0.f, .70710678f, .70710678f);
    reach.addRotationKey(source.findBone("LeftElbow"), 0.f, 0.f, 0.f, .70710678f, .70710678f);
    const float         sourceGap = handChestGap(source, reach);
    AnimRetargetProfile plain;
    plain.setSkinnedInteractionPreserve(false);
    std::unique_ptr<AnimClip> uncorrected(reach.retargetWithProfile(&source, &target, &plain));
    REQUIRE(uncorrected != nullptr);
    const float         uncorrectedGap = handChestGap(target, *uncorrected);
    AnimRetargetProfile corrected;
    corrected.setSkinnedInteractionPreserve(true);
    corrected.setInteractionContactThreshold(std::max(sourceGap * 1.25f, .35f));
    corrected.setInteractionCorrectionWeight(1.f);
    corrected.addInteractionIkChain("LeftShoulder", "LeftElbow", "LeftHand");
    std::unique_ptr<AnimClip> refined(reach.retargetWithProfile(&source, &target, &corrected));
    REQUIRE(refined != nullptr);
    CHECK(corrected.getInteractionCorrectionCount() >= 1);
    CHECK(uncorrectedGap > sourceGap);
    const bool improved        = handChestGap(target, *refined) + 1e-4f < uncorrectedGap;
    const int  elbow           = target.findBone("LeftElbow");
    bool       changedRotation = refined->getRotationKeyCount(elbow) != uncorrected->getRotationKeyCount(elbow);
    if (!changedRotation && refined->getRotationKeyCount(elbow) > 0) {
        const float x   = refined->getRotationKeyX(elbow, 0) - uncorrected->getRotationKeyX(elbow, 0);
        const float y   = refined->getRotationKeyY(elbow, 0) - uncorrected->getRotationKeyY(elbow, 0);
        const float z   = refined->getRotationKeyZ(elbow, 0) - uncorrected->getRotationKeyZ(elbow, 0);
        const float w   = refined->getRotationKeyW(elbow, 0) - uncorrected->getRotationKeyW(elbow, 0);
        changedRotation = std::abs(x) + std::abs(y) + std::abs(z) + std::abs(w) > 1e-4f;
    }
    const bool correctedOutputDiffers = improved || changedRotation;
    CHECK(correctedOutputDiffers);
}
}  // namespace test_animation_smr
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_two_bone_precision)
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>

namespace test_animation_ik_precision {
using namespace eve::animation;

TEST_CASE("animation.ik.rotatedScaledParentStillReachesWorldTarget") {
    AnimSkeleton skeleton;
    skeleton.addBone("parent");
    skeleton.addBone("hip", 0);
    skeleton.addBone("knee", 1);
    skeleton.addBone("ankle", 2);
    skeleton.setBindPosition(0, .5f, .25f, -.3f);
    skeleton.setBindRotation(0, 0.f, .70710678f, 0.f, .70710678f);
    skeleton.setBindScale(0, 1.5f, 1.5f, 1.5f);
    skeleton.setBindPosition(1, 0.f, .1f, 0.f);
    skeleton.setBindPosition(2, 0.f, 1.f, 0.f);
    skeleton.setBindPosition(3, 0.f, 1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    REQUIRE(pose.solveTwoBoneIK(&skeleton, 1, 2, 3, .8f, 2.1f, .1f, 1.f));
    CHECK(std::abs(pose.world(3).px - .8f) < .0003f);
    CHECK(std::abs(pose.world(3).py - 2.1f) < .0003f);
    CHECK(std::abs(pose.world(3).pz - .1f) < .0003f);
}

TEST_CASE("animation.ik.reachableTargetsAreStableAndUnreachableClamp") {
    AnimSkeleton skeleton;
    skeleton.addBone("hip");
    skeleton.addBone("knee", 0);
    skeleton.addBone("ankle", 1);
    skeleton.setBindPosition(1, 0.f, 1.f, 0.f);
    skeleton.setBindPosition(2, 0.f, 1.f, 0.f);
    AnimPose pose;
    for (const auto& target : {std::array{.2f, 1.4f, .1f}, std::array{0.f, 1.99f, 0.f}, std::array{0.f, .01f, 0.f},
                               std::array{-.3f, -1.5f, .2f}}) {
        skeleton.applyBindPose(&pose);
        REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, target[0], target[1], target[2], 1.f));
        const auto  tip   = pose.world(2);
        const float error = std::hypot(tip.px - target[0], tip.py - target[1], tip.pz - target[2]);
        CHECK(error < .0003f);
        const auto knee = pose.world(1);
        REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, target[0], target[1], target[2], 1.f));
        CHECK(std::abs(pose.world(1).px - knee.px) < .0003f);
    }
    skeleton.applyBindPose(&pose);
    REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, 0.f, 3.f, 0.f, 1.f));
    CHECK(std::abs(pose.world(2).py - 2.f) < 1e-6f);
    skeleton.setBindPosition(1, 0.f, 0.f, 0.f);
    skeleton.applyBindPose(&pose);
    CHECK(!pose.solveTwoBoneIK(&skeleton, 0, 1, 2, 1.f, 0.f, 1.f, 1.f));
    CHECK(std::abs(pose.world(2).py - 1.f) < 1e-6f);
}
}  // namespace test_animation_ik_precision
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hair_dynamic_bones)
#include "animation/AnimSkeleton.h"
#include "animation/DynamicBoneSolver.h"
#include "animation/HairDynamicBones.h"
#include "zeroerr/unittest.h"

#include <array>
#include <vector>

namespace test_animation_hair {
using namespace eve::animation;

TEST_CASE("animation.hair.chainAndHeadColliderSetupValidatesBoneNames") {
    AnimSkeleton skeleton;
    const int    head   = skeleton.addBone("head");
    const int    root   = skeleton.addBone("hair_root", head);
    const int    middle = skeleton.addBone("hair_middle", root);
    skeleton.addBone("hair_tip", middle);
    skeleton.setBindPosition(middle, 0.f, -.2f, 0.f);
    skeleton.setBindPosition(3, 0.f, -.2f, 0.f);
    DynamicBoneSolver solver(&skeleton);
    hair::ChainDesc   chain;
    chain.rootBone      = "hair_root";
    chain.tipBone       = "hair_tip";
    chain.stiffness     = .12f;
    chain.damping       = .18f;
    chain.inertia       = .8f;
    chain.endLength     = .05f;
    chain.selfCollision = true;
    auto added          = hair::addChain(solver, chain);
    REQUIRE(added.ok());
    CHECK_EQ(added.value(), 0);
    CHECK_EQ(solver.getChainCount(), 1);
    auto collider = hair::addHeadCollider(solver, "head", .12f);
    REQUIRE(collider.ok());
    CHECK(collider.value() >= 1);
    chain.rootBone = "missing";
    const std::vector<hair::ChainDesc> batch{chain};
    CHECK(!hair::setupChains(solver, batch, false).ok());
}
}  // namespace test_animation_hair
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_sprite_spine)
#include "animation/Animation.h"
#include "animation/SpineAnim.h"
#include "animation/SpineAtlas.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Quad.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace test_animation_sprite_spine {
using namespace eve::animation;

TEST_CASE("animation.sprite.sheetGridNamedFramesAndQuadCoordinates") {
    SpriteSheet sheet;
    CHECK_EQ(sheet.setGrid(4, 2, 32, 48, 0, 0, 0, 0), 8);
    REQUIRE_EQ(sheet.getFrameCount(), 8);
    CHECK_EQ(sheet.getFrameX(5), 32);
    CHECK_EQ(sheet.getFrameY(5), 48);
    CHECK_EQ(sheet.getFrameWidth(0), 32);
    CHECK_EQ(sheet.getFrameHeight(0), 48);
    sheet.clear();
    CHECK_EQ(sheet.addFrame("idle0", 10, 20, 16, 16), 0);
    CHECK_EQ(sheet.addFrame("idle1", 26, 20, 16, 16), 1);
    CHECK_EQ(sheet.findFrame("idle1"), 1);
    CHECK_EQ(sheet.findFrame("missing"), -1);
    eve::graphics::Quad quad;
    sheet.applyToQuad(&quad, 1);
    CHECK_EQ(quad.getX(), 26);
    CHECK_EQ(quad.getY(), 20);
    CHECK_EQ(quad.getWidth(), 16);
    CHECK_EQ(quad.getHeight(), 16);
}

TEST_CASE("animation.sprite.clipFramesLoopAndNonloopPlayerContract") {
    Animation*                   animation = Animation::create();
    std::unique_ptr<SpriteSheet> sheet(animation->newSpriteSheet());
    sheet->setGrid(4, 1, 16, 16);
    std::unique_ptr<SpriteClip> clip(animation->newSpriteClip("walk"));
    clip->setLoop(true);
    for (int frame = 0; frame < 4; ++frame) clip->addFrame(frame, .1f);
    REQUIRE(std::abs(clip->getDuration() - .4f) < 1e-5f);
    CHECK_EQ(clip->frameAtTime(.05f), 0);
    CHECK_EQ(clip->frameAtTime(.15f), 1);
    CHECK_EQ(clip->frameAtTime(.45f), 0);
    std::unique_ptr<SpriteAnim> player(animation->newSpriteAnim());
    player->setSheet(sheet.get());
    eve::graphics::Quad quad;
    player->bindQuad(&quad);
    player->play(clip.get());
    CHECK(player->isPlaying());
    CHECK_EQ(player->getSheetFrame(), 0);
    animation->update(.15f);
    CHECK_EQ(player->getClipFrame(), 1);
    CHECK_EQ(quad.getX(), 16);
    player->play(clip.get());
    player->setLoop(false);
    for (int frame = 0; frame < 10; ++frame) player->update(.1f);
    CHECK(player->isFinished());
    CHECK(!player->isPlaying());
    CHECK_EQ(player->getSheetFrame(), 3);
}

TEST_CASE("animation.sprite.reverseRangeSpeedCurveAndEventsContract") {
    Animation*                  animation = Animation::create();
    std::unique_ptr<SpriteClip> clip(animation->newSpriteClip("effect"));
    clip->addRange(2, 5, 20.f);
    REQUIRE_EQ(clip->getFrameCount(), 4);
    CHECK_EQ(clip->getFPS(), 20.f);
    clip->setFPS(10.f);
    CHECK(std::abs(clip->getDuration() - .4f) < 1e-5f);
    std::unique_ptr<SpriteAnim> player(animation->newSpriteAnim());
    player->playReverse(clip.get());
    CHECK(player->getSpeed() < 0.f);
    CHECK_EQ(player->getSheetFrame(), 5);
    player->update(.11f);
    CHECK_EQ(player->getSheetFrame(), 4);
    player->update(.35f);
    CHECK(player->consumeLooped() >= 1);
    player->setLoop(false);
    player->playReverse(clip.get());
    player->setLoop(false);
    player->update(1.f);
    CHECK(player->isFinished());
    CHECK(player->consumeCompleted());
    CHECK(!player->consumeCompleted());
    CHECK_EQ(player->getSheetFrame(), 2);
    player->setSpeed(1.f);
    player->clearSpeedCurve();
    player->addSpeedCurveKey(0.f, .5f);
    player->addSpeedCurveKey(1.f, 2.f);
    player->setSpeedCurveLoop(false);
    player->play(clip.get());
    CHECK(std::abs(player->getSpeedCurveValue() - .5f) < 1e-5f);
    player->update(.5f);
    CHECK(player->getSpeedCurveValue() > 1.2f);
    clip->addEvent(2, "impact");
    player->setFrame(2);
    CHECK_EQ(player->consumeEvent(), "impact");
    player->step(1);
    CHECK_EQ(player->getClipFrame(), 3);
    SpriteSheet trimmed;
    const int   frame = trimmed.addFrameTrimmed("trim", 2, 3, 10, 12, 32, 40, 7, 9);
    CHECK_EQ(trimmed.getFrameSourceWidth(frame), 32);
    CHECK_EQ(trimmed.getFrameOffsetX(frame), 7);
}

TEST_CASE("animation.spine.atlasParserExposesRegionsAndUvCoordinates") {
    const char* text =
        "hero.png\nsize: 128,64\nbody\n  xy: 2, 2\n  size: 40, 50\n"
        "head\n  xy: 50, 4\n  size: 24, 24\n";
    SpineAtlas  atlas;
    std::string error;
    REQUIRE(atlas.loadFromText(text, &error));
    CHECK_EQ(atlas.getPageCount(), 1);
    CHECK_EQ(std::string(atlas.getPageName(0)), "hero.png");
    CHECK_EQ(atlas.getPageWidth(0), 128);
    CHECK_EQ(atlas.getRegionCount(), 2);
    CHECK_EQ(atlas.findRegion("body"), 0);
    CHECK_EQ(atlas.getRegionWidth(0), 40);
    float u0 = 0.f, v0 = 0.f, u1 = 0.f, v1 = 0.f;
    atlas.getRegionUV(0, 128, 64, u0, v0, u1, v1);
    CHECK(std::abs(u0 - 2.f / 128.f) < 1e-5f);
    CHECK(std::abs(v0 - 2.f / 64.f) < 1e-5f);
}

TEST_CASE("animation.spine.skeletonAnimationAndDrawItemsFollowJsonData") {
    const char* json = R"JSON({
      "bones":[{"name":"root"},{"name":"body","parent":"root","y":40},
               {"name":"head","parent":"body","y":30}],
      "slots":[{"name":"body","bone":"body","attachment":"body"},
               {"name":"head","bone":"head","attachment":"head"}],
      "skins":[{"name":"default","attachments":{
        "body":{"body":{"width":40,"height":50}},
        "head":{"head":{"width":24,"height":24,"y":5}}}}],
      "animations":{"idle":{"bones":{"body":{"rotate":[
        {"time":0,"value":0},{"time":0.5,"value":20},{"time":1,"value":0}]}}},
        "nod":{"bones":{"head":{"translate":[
        {"time":0,"x":0,"y":0},{"time":0.25,"x":0,"y":-8}]}}}}
    })JSON";
    const char* atlasText =
        "hero.png\nsize: 128,64\nbody\n  xy: 2, 2\n  size: 40, 50\n"
        "head\n  xy: 50, 4\n  size: 24, 24\n";
    Animation*                         animation = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animation->newSpineSkeletonDataFromJson(json));
    REQUIRE(data != nullptr);
    CHECK_EQ(data->getBoneCount(), 3);
    CHECK_EQ(data->getAnimationCount(), 2);
    std::unique_ptr<SpineAtlas> atlas(animation->newSpineAtlasFromText(atlasText));
    REQUIRE(atlas != nullptr);
    std::unique_ptr<SpineSkeleton> skeleton(animation->newSpineSkeleton(data.get()));
    skeleton->updateWorldTransform();
    CHECK(std::abs(skeleton->getBoneWorldY(2) - 70.f) < 1e-3f);
    std::unique_ptr<SpineAnim> player(animation->newSpineAnim(skeleton.get()));
    player->setAtlas(atlas.get());
    player->setFlipY(false);
    REQUIRE(player->play("idle"));
    player->setTime(.5f);
    player->apply();
    CHECK(std::abs(skeleton->getBoneLocalRotation(1) - 20.f) < 1e-3f);
    REQUIRE(player->play("nod"));
    player->setLoop(false);
    player->setTime(.25f);
    player->apply();
    CHECK(std::abs(skeleton->getBoneLocalY(2) - 22.f) < 1e-3f);
    std::vector<eve::graphics::DrawItem2D> items;
    player->setPosition(100.f, 200.f);
    player->collectDrawItems(items);
    CHECK_EQ(items.size(), std::size_t{2});
}
}  // namespace test_animation_sprite_spine
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_control_rig)
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace test_animation_control_rig {
using namespace eve::animation;

RigDefinition fkDefinition() {
    RigDefinition definition;
    RigControl    control;
    control.id                   = "rootControl";
    control.initial.px           = 2.f;
    control.keys                 = {{0., control.initial}, {2., control.initial}};
    control.keys[1].transform.px = 4.f;
    definition.controls.push_back(control);
    RigNode node;
    node.id      = "rootFK";
    node.control = control.id;
    node.bone    = "root";
    definition.nodes.push_back(node);
    return definition;
}

TEST_CASE("animation.rig.fkTimelineBakeAndCodecOwnSkeletonDescription") {
    std::unique_ptr<ControlRig> rig;
    {
        AnimSkeleton skeleton;
        skeleton.addBone("root");
        auto compiled = ControlRig::compile(fkDefinition(), skeleton);
        REQUIRE(compiled.ok());
        rig = std::move(compiled).takeValue();
    }
    AnimPose input(1), output;
    REQUIRE(rig->evaluate(1., input, output).ok());
    CHECK(std::abs(output.local(0).px - 3.f) < 1e-5f);
    auto baked = rig->bake(30., input);
    REQUIRE(baked.ok());
    AnimPose sampled;
    baked.value()->sample(1.f, &sampled);
    CHECK(std::abs(sampled.local(0).px - output.local(0).px) < 1e-5f);
    auto matched = rig->matchFK(output);
    REQUIRE(matched.ok());
    CHECK(std::abs(matched.value()[0].px - 3.f) < 1e-5f);
    auto encoded = encodeControlRig(fkDefinition());
    REQUIRE(encoded.ok());
    auto decoded = decodeControlRig(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().controls[0].keys.size(), std::size_t{2});
    encoded.value().set("unknown", true);
    CHECK(!decodeControlRig(encoded.value()).ok());
}

TEST_CASE("animation.rig.invalidGraphAndInputDoNotMutateOutput") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto definition               = fkDefinition();
    definition.controls[0].parent = "rootControl";
    CHECK(!ControlRig::compile(definition, skeleton).ok());
    definition                = fkDefinition();
    definition.nodes[0].after = {"rootFK"};
    CHECK(!ControlRig::compile(definition, skeleton).ok());
    definition               = fkDefinition();
    definition.nodes[0].bone = "missing";
    CHECK(!ControlRig::compile(definition, skeleton).ok());
    definition                          = fkDefinition();
    definition.controls[0].keys[1].time = 0.;
    CHECK(!validateRigDefinition(definition).ok());
    auto compiled = ControlRig::compile(fkDefinition(), skeleton);
    REQUIRE(compiled.ok());
    AnimPose input(1), output(1);
    output.local(0).px = 99.f;
    CHECK(!compiled.value()->evaluate(std::numeric_limits<double>::quiet_NaN(), input, output).ok());
    CHECK_EQ(output.local(0).px, 99.f);
    input.local(0).sx = 0.f;
    CHECK(!compiled.value()->evaluate(0., input, output).ok());
    CHECK_EQ(output.local(0).px, 99.f);
}

TEST_CASE("animation.rig.twoBoneIkReparentPreservesWorldTarget") {
    AnimSkeleton skeleton;
    const int    root   = skeleton.addBone("root");
    const int    middle = skeleton.addBone("mid", root);
    const int    tip    = skeleton.addBone("tip", middle);
    skeleton.setBindPosition(middle, 0.f, 1.f, 0.f);
    skeleton.setBindPosition(tip, 0.f, 1.f, 0.f);
    RigDefinition definition;
    RigControl    hand;
    hand.id         = "hand";
    hand.initial.px = 1.f;
    hand.initial.py = 1.f;
    definition.controls.push_back(hand);
    RigNode ik;
    ik.id        = "ik";
    ik.control   = "hand";
    ik.bone      = "root";
    ik.middle    = "mid";
    ik.tip       = "tip";
    ik.operation = RigOperation::TwoBoneIK;
    definition.nodes.push_back(ik);
    auto compiled = ControlRig::compile(definition, skeleton);
    REQUIRE(compiled.ok());
    AnimPose input, output;
    skeleton.applyBindPose(&input);
    REQUIRE(compiled.value()->evaluate(0., input, output).ok());
    CHECK(std::abs(output.world(tip).px - 1.f) < 1e-4f);
    CHECK(std::abs(output.world(tip).py - 1.f) < 1e-4f);
    CHECK(!compiled.value()->matchFK(output).ok());
    RigControl space;
    space.id         = "space";
    space.initial.px = 5.f;
    definition.controls.push_back(space);
    REQUIRE(reparentRigControl(definition, "hand", "space").ok());
    CHECK(std::abs(definition.controls[0].initial.px + 4.f) < 1e-5f);
    CHECK(!reparentRigControl(definition, "space", "hand").ok());
    auto moved = ControlRig::compile(definition, skeleton);
    REQUIRE(moved.ok());
    REQUIRE(moved.value()->evaluate(0., input, output).ok());
    CHECK(std::abs(output.world(tip).px - 1.f) < 1e-4f);
}

TEST_CASE("animation.rig.aimComposesWithInputAndRejectsBadOverrideAtomically") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("unaffected");
    auto definition = fkDefinition();
    definition.controls[0].keys.clear();
    definition.controls[0].initial.px = 1.f;
    definition.nodes[0].operation     = RigOperation::Aim;
    auto compiled                     = ControlRig::compile(definition, skeleton);
    REQUIRE(compiled.ok());
    AnimPose input(2), output;
    input.local(1).py = 7.f;
    REQUIRE(compiled.value()->evaluate(0., input, output).ok());
    CHECK(std::abs(output.local(0).qy - std::sqrt(.5f)) < 1e-4f);
    CHECK_EQ(output.local(1).py, 7.f);
    std::vector<TransformTRS> invalidOverride(1);
    CHECK(!compiled.value()->evaluate(0., input, output, invalidOverride).ok());
    CHECK_EQ(output.local(1).py, 7.f);
}
}  // namespace test_animation_control_rig
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_control_rig_limits)
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "common/Value.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <string>

namespace test_animation_rig_limits {
using namespace eve::animation;

TEST_CASE("animation.rigLimits.lockClampExtensionAndInvalidConfigContract") {
    RigControlLimits limits;
    limits.reference.py        = 2.f;
    limits.translation[0].mode = RigAxisMode::Locked;
    limits.translation[1]      = {RigAxisMode::Limited, -.5f, .5f};
    limits.rotation[0].mode    = RigAxisMode::Locked;
    limits.rotation[1].mode    = RigAxisMode::Locked;
    limits.rotation[2]         = {RigAxisMode::Limited, -30.f, 45.f};
    limits.scale               = {RigAxisMode::Limited, .8f, 1.2f};
    TransformTRS desired;
    desired.px = 9.f;
    desired.py = 7.f;
    desired.qz = .70710678f;
    desired.qw = .70710678f;
    desired.sx = desired.sy = desired.sz = 3.f;
    auto projected                       = constrainRigControl(limits, desired);
    REQUIRE(projected.ok());
    CHECK_EQ(projected.value().px, 0.f);
    CHECK_EQ(projected.value().py, 2.5f);
    CHECK(std::abs(projected.value().sx - 1.2f) < 1e-5f);
    limits.extension = true;
    CHECK(!constrainRigControl(limits, desired).ok());
    limits.translation   = {};
    limits.minimumLength = .5f;
    limits.maximumLength = 1.5f;
    projected            = constrainRigControl(limits, desired);
    REQUIRE(projected.ok());
    CHECK_EQ(projected.value().py, 3.f);
    desired.py = -8.f;
    projected  = constrainRigControl(limits, desired);
    REQUIRE(projected.ok());
    CHECK_EQ(projected.value().py, 1.f);
    limits.reference.py = 0.f;
    CHECK(!constrainRigControl(limits, desired).ok());
    limits.extension   = false;
    limits.rotation[1] = {RigAxisMode::Limited, -100.f, 100.f};
    CHECK(!constrainRigControl(limits, desired).ok());
    limits.rotation[1] = {};
    desired.px         = std::numeric_limits<float>::quiet_NaN();
    CHECK(!constrainRigControl(limits, desired).ok());
}

TEST_CASE("animation.rigLimits.referenceRotationProjectionIsIdempotent") {
    RigControlLimits limits;
    limits.reference.qz = limits.reference.qw = std::sqrt(.5f);
    limits.rotation[0]                        = {RigAxisMode::Limited, -30.f, 30.f};
    limits.rotation[1].mode                   = RigAxisMode::Locked;
    limits.rotation[2].mode                   = RigAxisMode::Locked;
    TransformTRS desired;
    desired.qx = desired.qy = desired.qz = desired.qw = .5f;
    auto projected                                    = constrainRigControl(limits, desired);
    REQUIRE(projected.ok());
    CHECK(std::abs(projected.value().qx - std::sin(3.14159265f / 12.f) * std::sqrt(.5f)) < 1e-5f);
    auto again = constrainRigControl(limits, projected.value());
    REQUIRE(again.ok());
    CHECK_EQ(again.value().qx, projected.value().qx);
}

TEST_CASE("animation.rigLimits.codecMigrationAndBakeApplyClampedKeys") {
    RigDefinition definition;
    RigControl    control;
    control.id                    = "root";
    control.limits.translation[0] = {RigAxisMode::Limited, -1.f, 1.f};
    control.keys                  = {{0., {}}, {2., {}}};
    control.keys[1].transform.px  = 9.f;
    definition.controls.push_back(control);
    RigNode node;
    node.id      = "fk";
    node.control = "root";
    node.bone    = "root";
    definition.nodes.push_back(node);
    auto encoded = encodeControlRig(definition);
    REQUIRE(encoded.ok());
    auto decoded = decodeControlRig(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().controls[0].keys[1].transform.px, 1.f);
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto rig = ControlRig::compile(decoded.value(), skeleton);
    REQUIRE(rig.ok());
    AnimPose input(1), output;
    REQUIRE(rig.value()->evaluate(1., input, output).ok());
    CHECK(std::abs(output.local(0).px - .5f) < 1e-5f);
    auto clip = rig.value()->bake(30., input);
    REQUIRE(clip.ok());
    AnimPose baked;
    clip.value()->sample(1.f, &baked);
    CHECK(std::abs(baked.local(0).px - output.local(0).px) < 1e-5f);
    auto legacy = eve::Value::fromJson(
        R"({"schema":"eve.control-rig","version":1,"duration":2,"controls":[{"id":"old","parent":"","initial":[0,2,0,0,0,0,1,1,1,1],"shapeSize":0.1,"keys":[]}],"nodes":[]})");
    REQUIRE(legacy.ok());
    auto migrated = decodeControlRig(legacy.value());
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value().controls[0].limits.reference.py, 2.f);
    CHECK(migrated.value().controls[0].limits.translation[0].mode == RigAxisMode::Free);
    encoded.value().find("controls")->at(0).find("limits")->set("unknown", true);
    CHECK(!decodeControlRig(encoded.value()).ok());
}
}  // namespace test_animation_rig_limits
#endif
