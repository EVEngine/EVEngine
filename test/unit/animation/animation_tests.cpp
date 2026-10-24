#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <memory>
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
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation.cpp
namespace test_animation {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::animation;

TEST_CASE("animation.tween.lerpAbsolute") {
    auto                  *anim = Animation::create();
    std::unique_ptr<Tween> tw(anim->newTween(1.f));
    tw->setFrom("x", 0.f);
    tw->setTo("x", 100.f);
    tw->setEase("linear");
    tw->start();

    CHECK(tw->isRunning());
    CHECK(std::fabs(tw->get("x")) < 1e-5f);

    tw->update(0.5f);
    CHECK(std::fabs(tw->get("x") - 50.f) < 1e-4f);
    CHECK(std::fabs(tw->getProgress() - 0.5f) < 1e-5f);

    tw->update(0.5f);
    CHECK(std::fabs(tw->get("x") - 100.f) < 1e-4f);
    CHECK(tw->isFinished());
    CHECK(!tw->isActive());
}

TEST_CASE("animation.tween.setDeltaRelative") {
    auto                  *anim = Animation::create();
    std::unique_ptr<Tween> tw(anim->newTween(1.f));
    tw->setFrom("y", 10.f);
    tw->setDelta("y", 40.f);  // → 50
    tw->start();

    CHECK(std::fabs(tw->getTo("y") - 50.f) < 1e-5f);
    CHECK(std::fabs(tw->getDelta("y") - 40.f) < 1e-5f);

    tw->update(0.25f);
    CHECK(std::fabs(tw->get("y") - 20.f) < 1e-4f);

    tw->update(0.75f);
    CHECK(std::fabs(tw->get("y") - 50.f) < 1e-4f);
    CHECK(tw->isFinished());
}

TEST_CASE("animation.tween.multiProperty") {
    auto                  *anim = Animation::create();
    std::unique_ptr<Tween> tw(anim->newTween(2.f));
    tw->setFrom("x", 0.f);
    tw->setTo("x", 200.f);
    tw->setFrom("alpha", 1.f);
    tw->setDelta("alpha", -1.f);  // → 0
    tw->start();

    anim->update(1.f);
    CHECK(std::fabs(tw->get("x") - 100.f) < 1e-3f);
    CHECK(std::fabs(tw->get("alpha") - 0.5f) < 1e-4f);
    CHECK_EQ(tw->getPropertyCount(), 2);
    CHECK_EQ(anim->getActiveCount(), 1);
}

TEST_CASE("animation.tween.easeOutQuad") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    tw->setFrom("v", 0.f);
    tw->setTo("v", 1.f);
    tw->setEase("outQuad");
    // evaluate at t=0.5: outQuad(0.5) = 1 - 0.5^2 = 0.75
    CHECK(std::fabs(tw->evaluate("v", 0.5f) - 0.75f) < 1e-5f);
}

TEST_CASE("animation.tween.angleShortestPath") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    tw->setFromAngle("rot", float(M_PI * 0.9));  // ~162°
    tw->setToAngle("rot", float(-M_PI * 0.9));   // ~-162°, short path crosses ±π
    tw->start();
    tw->update(0.5f);
    // Midpoint of shortest arc should be near ±π
    float mid = tw->get("rot");
    CHECK(std::fabs(std::fabs(mid) - float(M_PI)) < 0.05f);
}

TEST_CASE("animation.tween.delayPauseResume") {
    auto                  *anim = Animation::create();
    std::unique_ptr<Tween> tw(anim->newTween(1.f));
    tw->setFrom("x", 0.f);
    tw->setTo("x", 10.f);
    tw->setDelay(0.5f);
    tw->start();

    CHECK(tw->isDelayed());
    tw->update(0.25f);
    CHECK(tw->isDelayed());
    CHECK(std::fabs(tw->get("x")) < 1e-5f);

    tw->pause();
    CHECK(tw->isPaused());
    tw->update(1.f);  // paused — no progress
    CHECK(tw->isPaused());

    tw->resume();
    tw->update(0.25f);  // finish delay
    CHECK(tw->isRunning());
    tw->update(0.5f);
    CHECK(std::fabs(tw->get("x") - 5.f) < 1e-3f);
}

TEST_CASE("animation.tween.yoyoRepeat") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    tw->setFrom("x", 0.f);
    tw->setTo("x", 100.f);
    tw->setRepeat(2);
    tw->setYoyo(true);
    tw->start();

    tw->update(1.f);  // end of first cycle → at 100, reverse starts
    CHECK(tw->isRunning());
    CHECK(std::fabs(tw->get("x") - 100.f) < 1e-3f);

    tw->update(0.5f);  // halfway back
    CHECK(std::fabs(tw->get("x") - 50.f) < 1e-2f);

    tw->update(0.5f);  // finished second (yoyo back) cycle
    CHECK(tw->isFinished());
    CHECK(std::fabs(tw->get("x")) < 1e-2f);
}

TEST_CASE("animation.module.clearFinished") {
    auto *anim = Animation::create();
    anim->clearAll();
    // Keep raw pointers: clearFinished only drops registry entries.
    Tween *a = anim->newTween(0.5f);
    Tween *b = anim->newTween(2.f);
    a->setFrom("x", 0.f);
    a->setTo("x", 1.f);
    a->start();
    b->setFrom("x", 0.f);
    b->setTo("x", 1.f);
    b->start();

    CHECK_EQ(anim->getTweenCount(), 2);
    anim->update(0.5f);
    CHECK(a->isFinished());
    CHECK(b->isRunning());
    anim->clearFinished();
    CHECK_EQ(anim->getTweenCount(), 1);
    CHECK_EQ(anim->getActiveCount(), 1);

    delete a;
    delete b;
}

TEST_CASE("animation.tween.unknownEaseThrows") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    bool                   threw = false;
    try {
        tw->setEase("notARealEase");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("animation.pose.aimBoneTracksWorldTarget") {
    auto sk = std::make_unique<AnimSkeleton>();
    sk->addBone("root", -1);
    auto pose = std::make_unique<AnimPose>();
    sk->applyBindPose(pose.get());

    REQUIRE(pose->aimBone(sk.get(), 0, 10.f, 0.f, 0.f, 1.f));
    const float forwardX = pose->getWorldMatrixElement(0, 8);
    const float forwardZ = pose->getWorldMatrixElement(0, 10);
    CHECK(forwardX > 0.999f);
    CHECK(std::fabs(forwardZ) < 1e-3f);
}

TEST_CASE("animation.pose.twoBoneIkMovesTipTowardTarget") {
    auto sk   = std::make_unique<AnimSkeleton>();
    int  root = sk->addBone("root", -1);
    int  mid  = sk->addBone("mid", root);
    int  tip  = sk->addBone("tip", mid);
    sk->setBindPosition(mid, 0.f, 1.f, 0.f);
    sk->setBindPosition(tip, 0.f, 1.f, 0.f);
    auto pose = std::make_unique<AnimPose>();
    sk->applyBindPose(pose.get());
    pose->computeWorld(sk.get());
    const float before = std::hypot(pose->getWorldPositionX(tip) - 1.f, pose->getWorldPositionY(tip) - 1.f);

    REQUIRE(pose->solveTwoBoneIK(sk.get(), root, mid, tip, 1.f, 1.f, 0.f, 1.f));
    const float after = std::hypot(pose->getWorldPositionX(tip) - 1.f, pose->getWorldPositionY(tip) - 1.f);
    CHECK(after < before * 0.1f);
}

TEST_CASE("animation.tween.deltaWithoutFromUsesCurrent") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    tw->setDelta("x", 10.f);  // from defaults to current (0)
    tw->start();
    CHECK(std::fabs(tw->getFrom("x")) < 1e-5f);
    CHECK(std::fabs(tw->getTo("x") - 10.f) < 1e-5f);
    tw->update(1.f);
    CHECK(std::fabs(tw->get("x") - 10.f) < 1e-4f);
}

// ---- helpers for 3D skeletal tests ----

static std::unique_ptr<AnimSkeleton> makeTwoBoneSkeleton() {
    auto sk   = std::make_unique<AnimSkeleton>();
    int  root = sk->addBone("root", -1);
    sk->setBindPosition(root, 0.f, 0.f, 0.f);
    int hip = sk->addBone("hip", root);
    sk->setBindPosition(hip, 0.f, 1.f, 0.f);
    return sk;
}

/** Clip that moves root along +Z at roughly `speed` units/sec over `dur`. */
static std::unique_ptr<AnimClip> makeLocomotionClip(const char *name, float speed, float dur) {
    auto clip = std::make_unique<AnimClip>(name);
    clip->setLoop(true);
    clip->setSampleRate(20.f);
    clip->setDuration(dur);
    // Root translation keys; hip stays near bind with a mild bob.
    clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip->addPositionKey(0, dur, 0.f, 0.f, speed * dur);
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, dur * 0.5f, 0.f, 1.05f, speed * dur * 0.5f);
    clip->addPositionKey(1, dur, 0.f, 1.f, speed * dur);
    return clip;
}

TEST_CASE("animation.skeleton.hierarchyAndBind") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    CHECK_EQ(sk->getBoneCount(), 2);
    CHECK_EQ(sk->findBone("hip"), 1);
    CHECK_EQ(sk->getParent(1), 0);
    CHECK(sk->getBoneName(0) == "root");

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    CHECK_EQ(pose->getBoneCount(), 2);
    CHECK(std::fabs(pose->getLocalPositionY(1) - 1.f) < 1e-5f);

    pose->computeWorld(sk.get());
    CHECK(std::fabs(pose->getWorldPositionY(1) - 1.f) < 1e-4f);
}

TEST_CASE("animation.clip.sampleLerp") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     clip(new AnimClip("wave"));
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, 1.f, 2.f, 1.f, 0.f);
    clip->setDuration(1.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    clip->sample(0.5f, pose.get(), sk.get());
    CHECK(std::fabs(pose->getLocalPositionX(1) - 1.f) < 1e-4f);
    CHECK(std::fabs(pose->getLocalPositionY(1) - 1.f) < 1e-4f);
}

TEST_CASE("animation.player.playAndCrossFade") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     a(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip>     b(makeLocomotionClip("run", 3.f, 1.f));

    std::unique_ptr<AnimPlayer> player(new AnimPlayer(sk.get()));
    player->play(a.get());
    player->update(0.5f);
    CHECK(player->isPlaying());
    CHECK(std::fabs(player->getPose()->getLocalPositionZ(0) - 0.5f) < 0.05f);

    player->crossFade(b.get(), 0.2f);
    player->update(0.1f);
    CHECK(player->isPlaying());
    // Mid-fade: between walk(~0.6) and run start(0)
    float z = player->getPose()->getLocalPositionZ(0);
    CHECK(z > -0.01f);
    CHECK(z < 0.7f);

    player->update(0.2f);
    CHECK(player->getClip() == b.get());
}

TEST_CASE("animation.player.rootMotionAndNotifyAcrossLoop") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 2.f, 1.f));
    walk->addEvent(0.25f, "footstep.left");
    walk->addEvent(0.75f, "footstep.right");
    AnimPlayer player(sk.get());
    player.play(walk.get());
    player.update(0.3f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.6f) < 0.01f);
    CHECK(player.consumeEvent() == "footstep.left");
    CHECK(player.consumeEvent().empty());

    player.setTime(0.9f);
    player.update(0.2f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.4f) < 0.01f);
    player.update(0.7f);
    CHECK(player.consumeEvent() == "footstep.left");
    CHECK(player.consumeEvent() == "footstep.right");
}

TEST_CASE("animation.player.updateRateLodAccumulatesTime") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));
    AnimPlayer                    player(sk.get());
    player.play(walk.get());
    player.setUpdateRate(10.f);
    player.update(0.04f);
    CHECK(std::fabs(player.getTime()) < 1e-5f);
    player.update(0.06f);
    CHECK(std::fabs(player.getTime() - 0.1f) < 1e-5f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.1f) < 1e-5f);
}

TEST_CASE("animation.graph.blendSpaceLayerAndAdditive") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     run(makeLocomotionClip("run", 4.f, 1.f));
    std::unique_ptr<AnimClip>     overlay(new AnimClip("overlay"));
    overlay->setDuration(1.f);
    overlay->addPositionKey(1, 0.f, 2.f, 1.f, 0.f);

    AnimGraph graph(sk.get());
    const int idleNode    = graph.addClip(idle.get());
    const int runNode     = graph.addClip(run.get());
    const int overlayNode = graph.addClip(overlay.get());
    const int locomotion  = graph.addBlendSpace1D();
    graph.addBlendSpace1DPoint(locomotion, 0.f, idleNode);
    graph.addBlendSpace1DPoint(locomotion, 4.f, runNode);
    graph.setPosition1D(locomotion, 2.f);
    const int layer = graph.addLayer(locomotion, overlayNode, 0.5f);
    graph.setBoneMask(layer, 1, 1.f, true);
    graph.setRoot(layer);
    graph.update(0.25f);

    CHECK(std::fabs(graph.getPose()->getLocalPositionZ(0) - 0.5f) < 0.05f);
    CHECK(std::fabs(graph.getPose()->getLocalPositionX(1) - 1.f) < 0.05f);

    const int additive = graph.addAdditive(locomotion, overlayNode, 0.25f);
    graph.setRoot(additive);
    graph.update(0.f);
    CHECK(std::fabs(graph.getPose()->getLocalPositionX(1) - 0.5f) < 0.05f);
}

TEST_CASE("animation.graph.oneShotFadesAndCompletes") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     shot(makeLocomotionClip("shot", 2.f, 0.5f));
    shot->setLoop(false);
    AnimGraph graph(sk.get());
    const int base    = graph.addClip(idle.get());
    const int action  = graph.addClip(shot.get());
    const int oneShot = graph.addOneShot(base, action, 0.1f, 0.1f);
    graph.setRoot(oneShot);
    graph.trigger(oneShot);
    graph.update(0.25f);
    CHECK(graph.isOneShotActive(oneShot));
    CHECK(graph.getPose()->getLocalPositionZ(0) > 0.1f);
    graph.update(0.3f);
    CHECK(!graph.isOneShotActive(oneShot));
}

TEST_CASE("animation.layers.overrideMaskAndAdditive") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     base(makeLocomotionClip("walk", 1.f, 1.f));

    std::unique_ptr<AnimClip> upper(new AnimClip("upper"));
    upper->setDuration(1.f);
    upper->setLoop(true);
    upper->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    upper->addPositionKey(1, 1.f, 2.f, 1.f, 0.f);

    AnimPlayer basePlayer(sk.get());
    AnimPlayer upperPlayer(sk.get());
    basePlayer.play(base.get());
    upperPlayer.play(upper.get());
    AnimBoneMask upperMask(sk.get());
    CHECK(upperMask.setBoneAndChildren("hip", 1.f));
    AnimLayerMixer mixer(sk.get());
    CHECK(mixer.setBasePlayer(&basePlayer));
    CHECK_EQ(mixer.addLayer("upper", &upperPlayer, &upperMask, "override"), 0);
    CHECK(mixer.setLayerWeight("upper", 0.5f));
    mixer.update(0.5f);
    CHECK(std::fabs(mixer.getPose()->getLocalPositionZ(0) - 0.5f) < 1e-5f);
    CHECK(std::fabs(mixer.getPose()->getLocalPositionX(1) - 0.5f) < 1e-5f);

    std::unique_ptr<AnimClip> recoil(new AnimClip("recoil"));
    recoil->setDuration(1.f);
    recoil->setLoop(true);
    recoil->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    recoil->addPositionKey(1, 1.f, 0.f, 3.f, 0.f);
    AnimPlayer recoilPlayer(sk.get());
    recoilPlayer.play(recoil.get());
    CHECK_EQ(mixer.addLayer("recoil", &recoilPlayer, &upperMask, "additive"), 1);
    CHECK(mixer.setLayerWeight("recoil", 0.5f));
    mixer.update(0.5f);
    CHECK(std::fabs(mixer.getPose()->getLocalPositionY(1) - 1.5f) < 1e-5f);
}


TEST_CASE("animation.layers.poseSourcesAndAdditiveReference") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));

    // Absolute upper pose used with bind-pose additive reference.
    std::unique_ptr<AnimClip> upperAbs(new AnimClip("upperAbs"));
    upperAbs->setDuration(1.f);
    upperAbs->setLoop(true);
    upperAbs->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    upperAbs->addPositionKey(1, 1.f, 0.f, 3.f, 0.f);

    // Identity-delta clip: translation is already the additive offset.
    std::unique_ptr<AnimClip> upperDelta(new AnimClip("upperDelta"));
    upperDelta->setDuration(1.f);
    upperDelta->setLoop(true);
    upperDelta->addPositionKey(1, 0.f, 0.f, 0.f, 0.f);
    upperDelta->addPositionKey(1, 1.f, 0.f, 2.f, 0.f);

    AnimPlayer basePlayer(sk.get());
    basePlayer.play(walk.get());

    AnimGraph upperGraph(sk.get());
    const int upperClip = upperGraph.addClip(upperAbs.get());
    upperGraph.setRoot(upperClip);

    AnimBoneMask upperMask(sk.get());
    CHECK(upperMask.setBoneAndChildren("hip", 1.f));

    AnimLayerMixer mixer(sk.get());
    CHECK(mixer.setBasePlayer(&basePlayer));
    CHECK_EQ(mixer.addGraphLayer("upper", &upperGraph, &upperMask, "additive"), 0);
    CHECK(mixer.setLayerWeight("upper", 0.5f));
    CHECK_EQ(mixer.getLayerMode("upper"), std::string("additive"));
    CHECK_EQ(mixer.getLayerAdditiveReference("upper"), std::string("bind"));
    mixer.update(0.5f);
    // bind-local hip Y is 1; absolute sample Y at t=0.5 is 2; delta=(2-1); weight 0.5 -> +0.5 on base.
    // Base locomotion at t=0.5 keeps hip near bind Y (~1.05 from bob keys) — tolerate range.
    CHECK(mixer.getPose()->getLocalPositionY(1) > 1.2f);

    // Switch the same absolute clip to identity reference and expect a larger lift
    // because the absolute Y is treated as a raw delta.
    CHECK(mixer.setLayerAdditiveReferenceCompat("upper", "identity"));
    CHECK_EQ(mixer.getLayerAdditiveReference("upper"), std::string("identity"));

    AnimPlayer deltaPlayer(sk.get());
    deltaPlayer.play(upperDelta.get());
    CHECK(mixer.removeLayer("upper"));
    CHECK_EQ(mixer.addLayer("upperDelta", &deltaPlayer, &upperMask, "additive"), 0);
    CHECK(mixer.setLayerAdditiveReferenceCompat("upperDelta", "identity"));
    CHECK(mixer.setLayerWeight("upperDelta", 0.5f));
    mixer.update(0.5f);
    // Identity delta Y at t=0.5 is 1; weight 0.5 adds ~0.5 on top of locomotion hip (~1).
    CHECK(mixer.getPose()->getLocalPositionY(1) > 1.4f);
}

TEST_CASE("animation.layers.disabledLayersStillAdvance") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip>     upper(new AnimClip("upper"));
    upper->setDuration(1.f);
    upper->setLoop(true);
    upper->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    upper->addPositionKey(1, 1.f, 2.f, 1.f, 0.f);

    AnimPlayer basePlayer(sk.get());
    AnimPlayer upperPlayer(sk.get());
    basePlayer.play(walk.get());
    upperPlayer.play(upper.get());
    AnimBoneMask upperMask(sk.get());
    CHECK(upperMask.setBoneAndChildren("hip", 1.f));
    AnimLayerMixer mixer(sk.get());
    CHECK(mixer.setBasePlayer(&basePlayer));
    CHECK_EQ(mixer.addLayer("upper", &upperPlayer, &upperMask, "override"), 0);
    CHECK(mixer.setLayerEnabled("upper", false));
    mixer.update(0.5f);
    const float disabledTime = upperPlayer.getTime();
    CHECK(disabledTime > 0.4f);
    // Disabled layers still advance, so re-enabling resumes from mid-clip rather than t=0.
    CHECK(mixer.setLayerEnabled("upper", true));
    CHECK(upperPlayer.getTime() >= disabledTime);
    mixer.update(0.25f);
    CHECK(upperPlayer.getTime() > disabledTime);
}

TEST_CASE("animation.graph.additiveReferenceBindMatchesMixer") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     base(makeLocomotionClip("walk", 0.f, 1.f));
    std::unique_ptr<AnimClip>     abs(new AnimClip("abs"));
    abs->setDuration(1.f);
    abs->setLoop(true);
    abs->addPositionKey(1, 0.f, 0.f, 3.f, 0.f);
    abs->addPositionKey(1, 1.f, 0.f, 3.f, 0.f);

    AnimGraph graph(sk.get());
    const int baseNode = graph.addClip(base.get());
    const int absNode  = graph.addClip(abs.get());
    const int addNode  = graph.addAdditive(baseNode, absNode, 1.f);
    graph.setRoot(addNode);
    CHECK(graph.setAdditiveReferenceCompat(addNode, "bind"));
    CHECK_EQ(graph.getAdditiveReference(addNode), std::string("bind"));
    graph.update(0.f);
    // Absolute Y=3 against bind Y=1 => +2 on base bind (~1) => ~3.
    CHECK(std::fabs(graph.getPose()->getLocalPositionY(1) - 3.f) < 0.1f);
}

TEST_CASE("animation.player.eventsSurviveLoopBoundaries") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     clip(makeLocomotionClip("events", 0.f, 1.f));
    clip->addEvent(0.25f, "footstep", "left");
    clip->addEvent(0.75f, "footstep", "right");
    AnimPlayer player(sk.get());
    player.play(clip.get());

    player.update(0.3f);
    CHECK_EQ(player.getEventCount(), 1);
    CHECK_EQ(player.getEventName(0), std::string("footstep"));
    CHECK_EQ(player.getEventPayload(0), std::string("left"));
    player.update(0.5f);
    CHECK_EQ(player.getEventCount(), 1);
    CHECK_EQ(player.getEventPayload(0), std::string("right"));
    player.update(1.5f);
    CHECK_EQ(player.getEventCount(), 3);
}

TEST_CASE("animation.stateMachine.floatTransition") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));

    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(sk.get()));
    sm->addState("Idle", idle.get());
    sm->addState("Walk", walk.get());
    sm->setEntry("Idle");

    int t = sm->addTransition("Idle", "Walk", 0.1f);
    sm->addFloatCondition(t, "speed", ">", 0.5f);
    int t2 = sm->addTransition("Walk", "Idle", 0.1f);
    sm->addFloatCondition(t2, "speed", "<", 0.1f);

    CHECK(sm->getCurrentState() == "Idle");
    sm->setFloat("speed", 1.f);
    sm->update(0.016f);
    // Logical state switches immediately; pose may still be blending.
    CHECK((sm->getCurrentState() == "Walk" || sm->isBlending()));
    for (int i = 0; i < 20; ++i) sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Walk");

    sm->setFloat("speed", 0.f);
    for (int i = 0; i < 20; ++i) sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Idle");
}

TEST_CASE("animation.stateMachine.triggerAndExitTime") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     jump(makeLocomotionClip("jump", 0.f, 0.5f));
    jump->setLoop(false);

    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(sk.get()));
    sm->addState("Idle", idle.get());
    sm->addState("Jump", jump.get());
    sm->setEntry("Idle");

    int t = sm->addTransition("Idle", "Jump", 0.f);
    sm->addTriggerCondition(t, "jump");
    int tBack = sm->addTransition("Jump", "Idle", 0.05f);
    sm->setExitTime(tBack, 0.9f);

    sm->update(0.1f);
    CHECK(sm->getCurrentState() == "Idle");
    sm->setTrigger("jump");
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "Jump");

    // Before exit time, stay in Jump
    sm->update(0.2f);
    CHECK(sm->getCurrentState() == "Jump");
    // Past 0.9 * 0.5s duration
    sm->update(0.4f);
    for (int i = 0; i < 10; ++i) sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Idle");
}

TEST_CASE("animation.stateMachine.captureRestoreRoundtrip") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));

    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(sk.get()));
    sm->addState("Idle", idle.get());
    sm->addState("Walk", walk.get());
    sm->setEntry("Idle");
    sm->setFloat("speed", 2.5f);
    sm->setBool("grounded", true);
    sm->setTrigger("jump");
    sm->update(0.25f);  // advance stateTime a bit

    eve::StateValue captured;
    REQUIRE(sm->captureState(captured));

    // Mutate everything, then restore.
    sm->setFloat("speed", 0.f);
    sm->setBool("grounded", false);
    sm->resetTrigger("jump");
    sm->setEntry("Walk");

    std::string err;
    CHECK(sm->restoreState(captured, &err));
    CHECK(err.empty());
    CHECK(sm->getCurrentState() == "Idle");
    CHECK(sm->getFloat("speed") == 2.5f);
    CHECK(sm->getBool("grounded"));
    CHECK(sm->getTrigger("jump"));
    CHECK(sm->getStateTime() > 0.1f);
}

TEST_CASE("animation.stateMachine.restoreUnknownStateFallsBackToEntry") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));

    std::unique_ptr<AnimStateMachine> src(new AnimStateMachine(sk.get()));
    src->addState("Idle", idle.get());
    src->setEntry("Idle");

    std::unique_ptr<AnimStateMachine> dst(new AnimStateMachine(sk.get()));
    dst->addState("Walk", walk.get());
    dst->setEntry("Walk");

    eve::StateValue captured;
    REQUIRE(src->captureState(captured));
    std::string err;
    CHECK(!dst->restoreState(captured, &err));  // "Idle" is not defined in dst
    CHECK(!err.empty());

    dst->resetToDefaults();
    CHECK(dst->getCurrentState() == "Walk");
    CHECK(!dst->isBlending());
}

TEST_CASE("animation.motionMatching.selectsFasterClip") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip>     run(makeLocomotionClip("run", 3.f, 1.f));

    std::unique_ptr<MotionDatabase> db(new MotionDatabase(sk.get()));
    db->addFeatureBone(1);
    db->addClip(walk.get());
    db->addClip(run.get());
    db->bake();
    CHECK(db->isBaked());
    CHECK(db->getFrameCount() > 0);

    std::unique_ptr<MotionMatcher> mm(new MotionMatcher(sk.get(), db.get()));
    mm->setSearchInterval(0.05f);
    mm->setBlendTime(0.05f);
    mm->setIgnoreRadius(0);
    mm->setDesiredVelocity(0.f, 3.f);  // want run speed along +Z
    mm->setDesiredYaw(0.f);
    mm->search();

    CHECK(mm->getMatchedFrame() >= 0);
    // Prefer the run clip for high desired speed.
    CHECK_EQ(mm->getMatchedClipIndex(), 1);

    mm->update(0.1f);
    AnimPose *pose = mm->getPose();
    CHECK(pose->getBoneCount() == 2);
}

TEST_CASE("animation.module.factories") {
    auto                         *anim = Animation::create();
    std::unique_ptr<AnimSkeleton> sk(anim->newSkeleton());
    sk->addBone("root", -1);
    std::unique_ptr<AnimClip> clip(anim->newClip("c"));
    clip->setDuration(0.5f);
    std::unique_ptr<AnimPlayer> player(anim->newPlayer(sk.get()));
    player->play(clip.get());
    player->update(0.1f);
    CHECK(player->isPlaying());

    std::unique_ptr<AnimStateMachine> sm(anim->newStateMachine(sk.get()));
    sm->addState("A", clip.get());
    sm->setEntry("A");
    CHECK(sm->hasState("A"));

    std::unique_ptr<MotionDatabase> db(anim->newMotionDatabase(sk.get()));
    db->addClip(clip.get());
    db->bake();
    std::unique_ptr<MotionMatcher> mm(anim->newMotionMatcher(sk.get(), db.get()));
    mm->search();
    CHECK(mm->getMatchedFrame() >= 0);
}

TEST_CASE("animation.clip.wrapTimeLoopAndClamp") {
    std::unique_ptr<AnimClip> clip(new AnimClip("loop"));
    clip->setDuration(1.f);
    clip->setLoop(true);
    CHECK(std::fabs(clip->wrapTime(1.5f) - 0.5f) < 1e-4f);
    CHECK(std::fabs(clip->wrapTime(-0.25f) - 0.75f) < 1e-4f);

    clip->setLoop(false);
    CHECK(std::fabs(clip->wrapTime(2.f) - 1.f) < 1e-5f);
    CHECK(std::fabs(clip->wrapTime(-1.f)) < 1e-5f);
}

TEST_CASE("animation.clip.applyPlanarRootMotion") {
    std::unique_ptr<AnimClip> clip(new AnimClip("inplace"));
    clip->setDuration(1.f);
    clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip->addPositionKey(0, 1.f, 0.f, 0.f, 0.f);
    clip->applyPlanarRootMotion(0, 0.f, 2.f);
    CHECK(std::fabs(clip->getPositionKeyZ(0, 0)) < 1e-5f);
    CHECK(std::fabs(clip->getPositionKeyZ(0, 1) - 2.f) < 1e-4f);
}

TEST_CASE("animation.pose.blendAndWorldFk") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimPose>     a(new AnimPose(2));
    std::unique_ptr<AnimPose>     b(new AnimPose(2));
    a->setLocalPosition(1, 0.f, 1.f, 0.f);
    b->setLocalPosition(1, 2.f, 1.f, 0.f);
    std::unique_ptr<AnimPose> mid(new AnimPose());
    mid->blendFrom(a.get(), b.get(), 0.5f);
    CHECK(std::fabs(mid->getLocalPositionX(1) - 1.f) < 1e-4f);

    mid->setLocalPosition(0, 0.f, 0.f, 0.f);
    mid->setLocalPosition(1, 0.f, 2.f, 0.f);
    mid->computeWorld(sk.get());
    CHECK(std::fabs(mid->getWorldPositionY(1) - 2.f) < 1e-4f);
}

TEST_CASE("animation.player.pauseResumeStopAndNonLoop") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     clip(makeLocomotionClip("once", 1.f, 1.f));
    clip->setLoop(false);

    std::unique_ptr<AnimPlayer> player(new AnimPlayer(sk.get()));
    player->play(clip.get());
    player->setLoop(false);
    player->update(0.25f);
    float z0 = player->getPose()->getLocalPositionZ(0);
    player->pause();
    CHECK(player->isPaused());
    player->update(0.5f);
    CHECK(std::fabs(player->getPose()->getLocalPositionZ(0) - z0) < 1e-4f);

    player->resume();
    player->update(0.25f);
    CHECK(player->getPose()->getLocalPositionZ(0) > z0 + 0.1f);

    player->update(1.f);
    CHECK(!player->isPlaying());
    CHECK(std::fabs(player->getTime() - 1.f) < 1e-3f);

    player->stop();
    CHECK(player->getClip() == nullptr);
}

TEST_CASE("animation.stateMachine.boolAndCompoundConditions") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     crouch(makeLocomotionClip("crouch", 0.f, 1.f));
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));

    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(sk.get()));
    sm->addState("Idle", idle.get());
    sm->addState("Crouch", crouch.get());
    sm->addState("Walk", walk.get());
    sm->setEntry("Idle");

    int toCrouch = sm->addTransition("Idle", "Crouch", 0.f);
    sm->addBoolCondition(toCrouch, "crouching", true);

    int toWalk = sm->addTransition("Idle", "Walk", 0.f);
    sm->addFloatCondition(toWalk, "speed", ">=", 0.5f);
    sm->addBoolCondition(toWalk, "crouching", false);

    int crouchUp = sm->addTransition("Crouch", "Idle", 0.f);
    sm->addBoolCondition(crouchUp, "crouching", false);

    sm->setBool("crouching", true);
    sm->setFloat("speed", 1.f);  // would walk, but crouch bool wins first matching transition
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "Crouch");

    sm->setBool("crouching", false);
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "Idle");

    sm->setFloat("speed", 1.f);
    sm->setBool("crouching", false);
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "Walk");
}

TEST_CASE("animation.stateMachine.floatOpsAndTriggerConsume") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     a(makeLocomotionClip("a", 0.f, 1.f));
    std::unique_ptr<AnimClip>     b(makeLocomotionClip("b", 0.f, 1.f));

    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(sk.get()));
    sm->addState("A", a.get());
    sm->addState("B", b.get());
    sm->setEntry("A");

    int tEq = sm->addTransition("A", "B", 0.f);
    sm->addFloatCondition(tEq, "mode", "==", 2.f);
    sm->setFloat("mode", 1.f);
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "A");
    sm->setFloat("mode", 2.f);
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "B");

    // Rebuild for trigger consume: A --trigger--> B --auto back with bool
    std::unique_ptr<AnimStateMachine> sm2(new AnimStateMachine(sk.get()));
    sm2->addState("A", a.get());
    sm2->addState("B", b.get());
    sm2->setEntry("A");
    int tTrig = sm2->addTransition("A", "B", 0.f);
    sm2->addTriggerCondition(tTrig, "go");
    int tBack = sm2->addTransition("B", "A", 0.f);
    sm2->addBoolCondition(tBack, "reset", true);

    sm2->setTrigger("go");
    sm2->update(0.016f);
    CHECK(sm2->getCurrentState() == "B");
    // Trigger consumed: without re-set, returning to A then update should stay A
    sm2->setBool("reset", true);
    sm2->update(0.016f);
    CHECK(sm2->getCurrentState() == "A");
    sm2->setBool("reset", false);
    sm2->update(0.016f);
    CHECK(sm2->getCurrentState() == "A");
}

TEST_CASE("animation.stateMachine.blendKeepsLogicalState") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));

    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(sk.get()));
    sm->addState("Idle", idle.get());
    sm->addState("Walk", walk.get());
    sm->setEntry("Idle");
    int t = sm->addTransition("Idle", "Walk", 0.2f);
    sm->addFloatCondition(t, "speed", ">", 0.1f);

    sm->setFloat("speed", 1.f);
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "Walk");
    CHECK(sm->isBlending());
    sm->update(0.25f);
    CHECK(!sm->isBlending());
    CHECK(sm->getCurrentState() == "Walk");
}

TEST_CASE("animation.stateMachine.invalidApiThrows") {
    std::unique_ptr<AnimSkeleton>     sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>         clip(makeLocomotionClip("c", 0.f, 1.f));
    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(sk.get()));
    sm->addState("A", clip.get());

    bool threw = false;
    try {
        sm->addState("A", clip.get());
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        sm->addTransition("A", "Missing", 0.1f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    int t = sm->addTransition("A", "A", 0.f);
    threw = false;
    try {
        sm->addFloatCondition(t, "x", "??", 0.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("animation.motionMatching.threeSpeedsAndWeights") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip>     run(makeLocomotionClip("run", 3.f, 1.f));

    std::unique_ptr<MotionDatabase> db(new MotionDatabase(sk.get()));
    db->setRootBone(0);
    db->addFeatureBone(1);
    db->addClip(idle.get());
    db->addClip(walk.get());
    db->addClip(run.get());
    db->bake();
    CHECK_EQ(db->getClipCount(), 3);
    CHECK(db->getFeatureSize() >= 13);

    std::unique_ptr<MotionMatcher> mm(new MotionMatcher(sk.get(), db.get()));
    mm->setIgnoreRadius(0);
    mm->setVelocityWeight(3.f);
    mm->setTrajectoryWeight(1.f);
    mm->setPoseWeight(0.25f);

    mm->setDesiredVelocity(0.f, 0.f);
    mm->search();
    CHECK_EQ(mm->getMatchedClipIndex(), 0);

    mm->setDesiredVelocity(0.f, 1.f);
    mm->search();
    CHECK_EQ(mm->getMatchedClipIndex(), 1);

    mm->setDesiredVelocity(0.f, 3.f);
    mm->search();
    CHECK_EQ(mm->getMatchedClipIndex(), 2);
}

TEST_CASE("animation.motionMatching.searchIntervalAndIgnoreRadius") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip>     run(makeLocomotionClip("run", 3.f, 1.f));

    std::unique_ptr<MotionDatabase> db(new MotionDatabase(sk.get()));
    db->addClip(walk.get());
    db->addClip(run.get());
    db->bake();

    std::unique_ptr<MotionMatcher> mm(new MotionMatcher(sk.get(), db.get()));
    mm->setSearchInterval(1.f);  // long interval
    mm->setIgnoreRadius(0);
    mm->setDesiredVelocity(0.f, 3.f);
    mm->search();
    const int frame0 = mm->getMatchedFrame();
    CHECK(frame0 >= 0);
    CHECK_EQ(mm->getMatchedClipIndex(), 1);

    // Small updates should advance matched time without forcing a new search.
    mm->update(0.05f);
    CHECK(mm->getMatchedTime() > db->getFrameTime(frame0));
    CHECK_EQ(mm->getMatchedClipIndex(), 1);

    // Ignore radius: with large radius around current frame, still finds a match.
    mm->setIgnoreRadius(1000);
    mm->setDesiredVelocity(0.f, 1.f);
    mm->search();
    CHECK(mm->getMatchedFrame() >= 0);
}

TEST_CASE("animation.motionMatching.databaseMetadata") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip>     walk(makeLocomotionClip("walk", 1.f, 0.5f));
    walk->setSampleRate(10.f);

    std::unique_ptr<MotionDatabase> db(new MotionDatabase(sk.get()));
    db->addFeatureBone(1);
    db->addClip(walk.get());
    db->bake();

    CHECK(db->isBaked());
    CHECK(db->getFrameCount() >= 5);
    CHECK_EQ(db->getFrameClipIndex(0), 0);
    CHECK(db->getFrameTime(0) >= 0.f);
    CHECK_EQ(db->getFeatureBoneCount(), 1);
    CHECK_EQ(db->getFeatureBone(0), 1);

    std::vector<float> feat(static_cast<size_t>(db->getFeatureSize()), 0.f);
    db->getFeature(0, feat.data(), db->getFeatureSize());
    // velocity/trajectory features should be finite
    for (float v : feat) CHECK(std::isfinite(v));
}

TEST_CASE("animation.anim.txt.roundTrip") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    sk->setBindRotation(1, 0.f, 0.f, 0.f, 1.f);
    std::unique_ptr<AnimClip> clip(makeLocomotionClip("walk", 1.5f, 0.8f));
    clip->setSampleRate(12.f);
    clip->addSyncMarker(0.2f, "left_plant");
    clip->addSyncMarker(0.6f, "right_plant");

    const std::string text = AnimImporter::exportAnimationFixtureText(sk.get(), clip.get());
    CHECK(text.find("EVA 1") != std::string::npos);

    AnimSkeleton *sk2   = nullptr;
    AnimClip     *clip2 = nullptr;
    AnimImporter::importAnimationFixtureText(text, &sk2, &clip2);
    std::unique_ptr<AnimSkeleton> skOwned(sk2);
    std::unique_ptr<AnimClip>     clipOwned(clip2);

    CHECK_EQ(skOwned->getBoneCount(), 2);
    CHECK(skOwned->getBoneName(1) == "hip");
    CHECK(std::fabs(clipOwned->getDuration() - 0.8f) < 1e-4f);
    CHECK(std::fabs(clipOwned->getSampleRate() - 12.f) < 1e-4f);
    CHECK(clipOwned->getPositionKeyCount(0) >= 2);
    CHECK_EQ(clipOwned->getSyncMarkerCount(), 2);
    CHECK(clipOwned->getSyncMarkerName(1) == "right_plant");

    std::unique_ptr<AnimPose> pose(new AnimPose());
    clipOwned->sample(0.4f, pose.get(), skOwned.get());
    CHECK(std::fabs(pose->getLocalPositionZ(0) - 1.5f * 0.4f) < 0.05f);
}

TEST_CASE("animation.importer.invalidThrows") {
    bool threw = false;
    try {
        AnimImporter::importAnimationFixtureText("not an eva file\n", nullptr, nullptr);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    threw              = false;
    AnimSkeleton *sk   = nullptr;
    AnimClip     *clip = nullptr;
    try {
        AnimImporter::importAnimationFixtureText("EVA 99\n", &sk, &clip);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    CHECK(sk == nullptr);
    CHECK(clip == nullptr);
}

TEST_CASE("animation.controlAnim.secondOrderTracksTarget") {
    std::unique_ptr<ControlAnim> ca(new ControlAnim(4.f, 1.f, 1.f));
    ca->setIntegrator("secondOrder");
    ca->set("x", 0.f);
    ca->setTarget("x", 10.f);

    for (int i = 0; i < 120; ++i) ca->update(1.f / 60.f);
    CHECK(std::fabs(ca->get("x") - 10.f) < 0.15f);
    CHECK(std::fabs(ca->getVelocity("x")) < 0.5f);
    CHECK_EQ(ca->getPropertyCount(), 1);
    CHECK(ca->getPropertyName(0) == "x");
}

TEST_CASE("animation.controlAnim.springCriticallyDamped") {
    std::unique_ptr<ControlAnim> ca(new ControlAnim(5.f, 1.f, 0.f));
    ca->setIntegrator("spring");
    ca->set("y", 0.f);
    ca->setTarget("y", 1.f);

    float prev  = 0.f;
    bool  moved = false;
    for (int i = 0; i < 180; ++i) {
        ca->update(1.f / 60.f);
        float v = ca->get("y");
        if (v > prev + 1e-6f) moved = true;
        // Critically damped: should not overshoot the target significantly.
        CHECK(v < 1.05f);
        prev = v;
    }
    CHECK(moved);
    CHECK(std::fabs(ca->get("y") - 1.f) < 0.05f);
}

TEST_CASE("animation.controlAnim.pdAndUnderdamped") {
    std::unique_ptr<ControlAnim> ca(new ControlAnim(3.f, 0.35f, 1.f));
    ca->setIntegrator("pd");
    ca->set("z", 0.f);
    ca->setTarget("z", 1.f);

    float maxV = 0.f;
    for (int i = 0; i < 240; ++i) {
        ca->update(1.f / 60.f);
        maxV = std::max(maxV, ca->get("z"));
    }
    // Underdamped PD should overshoot then settle near target.
    CHECK(maxV > 1.02f);
    CHECK(std::fabs(ca->get("z") - 1.f) < 0.1f);
}

TEST_CASE("animation.controlAnim.impulseAndIntegratorSwitch") {
    std::unique_ptr<ControlAnim> ca(new ControlAnim(2.f, 0.5f, 0.f));
    ca->set("p", 0.f);
    ca->setTarget("p", 0.f);
    ca->impulse("p", 5.f);
    ca->update(1.f / 60.f);
    CHECK(ca->get("p") > 0.01f);

    bool threw = false;
    try {
        ca->setIntegrator("nope");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    CHECK(ca->getIntegrator() == "secondOrder");
}

TEST_CASE("animation.controlPose.tracksTargetPosition") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<ControlPose>  cp(new ControlPose(sk.get()));
    cp->setFrequency(5.f);
    cp->setDamping(1.f);
    cp->setResponse(1.f);
    cp->setIntegrator("secondOrder");

    std::unique_ptr<AnimPose> target(new AnimPose(2));
    sk->applyBindPose(target.get());
    target->setLocalPosition(1, 0.f, 3.f, 0.f);
    cp->setTargetPose(target.get());

    // Start near bind (y=1), track toward y=3.
    for (int i = 0; i < 180; ++i) cp->update(1.f / 60.f);
    AnimPose *pose = cp->getPose();
    CHECK(std::fabs(pose->getLocalPositionY(1) - 3.f) < 0.2f);
}

TEST_CASE("animation.controlPose.boneWeightHardFollow") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<ControlPose>  cp(new ControlPose(sk.get()));
    cp->setIntegrator("spring");
    cp->setBoneWeight(1, 0.f);

    std::unique_ptr<AnimPose> target(new AnimPose(2));
    sk->applyBindPose(target.get());
    target->setLocalPosition(1, 4.f, 2.f, -1.f);
    cp->setTargetPose(target.get());
    cp->update(1.f / 60.f);

    AnimPose *pose = cp->getPose();
    CHECK(std::fabs(pose->getLocalPositionX(1) - 4.f) < 1e-4f);
    CHECK(std::fabs(pose->getLocalPositionY(1) - 2.f) < 1e-4f);
    CHECK(std::fabs(pose->getLocalPositionZ(1) - -1.f) < 1e-4f);
}

TEST_CASE("animation.controlMath.pdGainsCritical") {
    float kp = 0.f, kd = 0.f;
    pdGainsFromOmegaZeta(10.f, 1.f, kp, kd);
    CHECK(std::fabs(kp - 100.f) < 1e-4f);
    CHECK(std::fabs(kd - 20.f) < 1e-4f);

    SecondOrderCoeffs c = makeSecondOrderCoeffs(1.f, 1.f, 1.f);
    CHECK(c.k1 > 0.f);
    CHECK(c.k2 > 0.f);
    CHECK(c.k3 > 0.f);
}

TEST_CASE("animation.trail.recordsAndExpires") {
    auto                      *anim = Animation::create();
    std::unique_ptr<AnimTrail> trail(anim->newTrail(8));
    REQUIRE(trail.get() != nullptr);
    CHECK_EQ(trail->getCapacity(), 8);
    trail->setDuration(0.2f);
    trail->setMinDistance(0.f);
    trail->setFade(true);
    trail->setStyle("line");
    trail->setColor(1.f, 0.5f, 0.2f, 1.f);

    trail->addPoint(0.f, 0.f);
    trail->addPoint(10.f, 0.f);
    trail->addPoint(20.f, 5.f);
    CHECK_EQ(trail->getPointCount(), 3);
    CHECK(std::fabs(trail->getPointX(0) - 0.f) < 1e-5f);
    CHECK(std::fabs(trail->getPointX(2) - 20.f) < 1e-5f);
    CHECK(std::fabs(trail->getPointAlpha(2) - 1.f) < 1e-4f);

    trail->update(0.1f);
    CHECK_EQ(trail->getPointCount(), 3);
    CHECK(trail->getPointAge(0) > 0.09f);
    CHECK(trail->getPointAlpha(0) < 0.6f);

    trail->update(0.15f);
    CHECK_EQ(trail->getPointCount(), 0);

    trail->draw(nullptr);  // no-op without Graphics
}

TEST_CASE("animation.trail.capacityAndMinDistance") {
    std::unique_ptr<AnimTrail> trail(new AnimTrail(4));
    trail->setDuration(0.f);  // capacity-only eviction
    trail->setMinDistance(5.f);
    trail->addPoint(0.f, 0.f);
    trail->addPoint(1.f, 0.f);  // skipped (< 5)
    CHECK_EQ(trail->getPointCount(), 1);
    trail->addPoint(6.f, 0.f);
    trail->addPoint(12.f, 0.f);
    trail->addPoint(18.f, 0.f);
    trail->addPoint(24.f, 0.f);
    CHECK_EQ(trail->getPointCount(), 4);
    CHECK(std::fabs(trail->getPointX(0) - 6.f) < 1e-5f);
    CHECK(std::fabs(trail->getPointX(3) - 24.f) < 1e-5f);
}

TEST_CASE("animation.trail.sampleBonePlanes") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(0, 3.f, 0.f, 7.f);
    pose->setLocalPosition(1, 0.f, 2.f, 0.f);
    pose->computeWorld(sk.get());

    std::unique_ptr<AnimTrail> trail(new AnimTrail(16));
    trail->sampleBone(pose.get(), 1, "xy");
    CHECK_EQ(trail->getPointCount(), 1);
    CHECK(std::fabs(trail->getPointX(0) - 3.f) < 1e-4f);
    CHECK(std::fabs(trail->getPointY(0) - 2.f) < 1e-4f);

    trail->clear();
    trail->sampleBone(pose.get(), 1, "xz");
    CHECK(std::fabs(trail->getPointX(0) - 3.f) < 1e-4f);
    CHECK(std::fabs(trail->getPointY(0) - 7.f) < 1e-4f);

    trail->clear();
    trail->sampleBoneOffset(pose.get(), 1, 1.f, 0.f, 0.f, "xy");
    CHECK(std::fabs(trail->getPointX(0) - 4.f) < 1e-4f);

    bool threw = false;
    try {
        trail->setStyle("ribbon");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        trail->sampleBone(pose.get(), 1, "uvw");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}
}  // namespace test_animation
#undef M_PI
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_batch_lod)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include <vector>
#include "animation/AnimBatch.h"
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_batch_lod.cpp
namespace test_animation_batch_lod_unit {
using namespace eve::animation;


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
}  // namespace test_animation_batch_lod_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_binary)
#include "PrivateTestScope.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimClipBinary.h"
#include "animation/AnimParallelInternal.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_binary.cpp
namespace test_animation_binary_unit {
using namespace eve::animation;
namespace {
using Bytes = std::vector<std::byte>;
[[maybe_unused]] void integer(Bytes &b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::byte>((v >> (8 * i)) & 255));
}
[[maybe_unused]] void number(Bytes &b, float v) { integer(b, std::bit_cast<std::uint32_t>(v)); }
[[maybe_unused]] void string(Bytes &b, const std::string &v) {
    integer(b, static_cast<std::uint32_t>(v.size()));
    for (auto c : v) b.push_back(static_cast<std::byte>(c));
}
[[maybe_unused]] Bytes fixture() {
    Bytes b;
    integer(b, 0x43415645);
    integer(b, 1);
    integer(b, 1);
    number(b, 2.f);
    number(b, 30.f);
    integer(b, 1);
    string(b, "test");
    string(b, "root");
    integer(b, 2);
    for (float t : {0.f, 2.f}) {
        number(b, t);
        number(b, 0.f);
        number(b, 0.f);
        number(b, 3.f * t);
    }
    integer(b, 1);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 1.f);
    integer(b, 1);
    number(b, 0.f);
    number(b, 1.f);
    number(b, 1.f);
    number(b, 1.f);
    return b;
}
}  // namespace


TEST_CASE("animation.binary.loadsTracksAndPreservesSampling") {
    AnimSkeleton sk;
    sk.addBone("root");
    sk.addBone("unanimated", 0);
    sk.setBindPosition(1, 0.f, 1.f, 0.f);
    AnimClip clip("old");
    auto     loaded = loadAnimationTracks(clip, fixture(), sk);
    REQUIRE(loaded.ok());
    CHECK(clip.getName() == "test");
    CHECK(clip.getLoop());
    CHECK(clip.getDuration() == 2.f);
    AnimPose p;
    clip.sample(0.5f, &p, &sk);
    p.computeWorld(&sk);
    CHECK(std::abs(p.getWorldPositionZ(1) - 1.5f) < 1e-6f);
    CHECK(p.getWorldPositionY(1) == 1.f);
    clip.sample(2.5f, &p, &sk);
    CHECK(std::abs(p.getLocalPositionZ(0) - 1.5f) < 1e-6f);
}
TEST_CASE("animation.binary.rejectsEveryTruncationAtomically") {
    AnimSkeleton sk;
    sk.addBone("root");
    AnimClip clip("unchanged");
    clip.setDuration(4.f);
    const auto bytes = fixture();
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        auto r = loadAnimationTracks(clip, {bytes.data(), size}, sk);
        CHECK(!r.ok());
        CHECK(clip.getName() == "unchanged");
        CHECK(clip.getDuration() == 4.f);
    }
}
TEST_CASE("animation.binary.rejectsVersionTrailingNamesFlagsAndNonfinite") {
    AnimSkeleton sk;
    sk.addBone("root");
    AnimClip clip("old");
    for (int bad : {0, 1, 2, 3, 4, 5, 6, 7}) {
        auto bytes = fixture();
        if (bad == 0) bytes[4] = std::byte{2};
        if (bad == 1) bytes.push_back(std::byte{0});
        if (bad == 2) bytes[36] = std::byte{'X'};
        if (bad == 3) bytes[20] = std::byte{2};
        if (bad == 4) {
            Bytes nan;
            number(nan, std::numeric_limits<float>::quiet_NaN());
            for (int i = 0; i < 4; ++i) bytes[12 + i] = nan[i];
        }
        if (bad == 5) bytes[36] = std::byte{0xc0};
        if (bad == 6)
            for (int i = 0; i < 4; ++i) bytes[60 + i] = std::byte{0};
        if (bad == 7)
            for (int i = 0; i < 4; ++i) bytes[96 + i] = std::byte{0};
        auto r = loadAnimationTracks(clip, bytes, sk);
        CHECK(!r.ok());
        CHECK(clip.getName() == "old");
    }
}
TEST_CASE("animation.binary.stepBoundaryAtAdjacentFloatTimes") {
    AnimClip clip("step");
    clip.setLoop(false);
    AnimSkeleton sk;
    sk.addBone("root");
    const float before = std::nextafter(1.f, 0.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, before, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 1.f, 0.f, 0.f, 2.f);
    clip.addPositionKey(0, 2.f, 0.f, 0.f, 2.f);
    AnimPose p;
    for (float t : {0.3f, before}) {
        clip.sample(t, &p, &sk);
        CHECK(p.getLocalPositionZ(0) == 0.f);
    }
    for (float t : {1.f, std::nextafter(1.f, 2.f), 1.8f}) {
        clip.sample(t, &p, &sk);
        CHECK(p.getLocalPositionZ(0) == 2.f);
    }
}
}  // namespace test_animation_binary_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_clip_editing)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "animation/AnimClip.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_clip_editing.cpp
namespace test_animation_clip_editing_unit {
using namespace eve::animation;

TEST_CASE("animation.clip.editableTracksStaySorted") {
    AnimClip clip("editable");
    clip.setDuration(2.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 1.f, 1.f, 2.f, 3.f);
    clip.addRotationKey(0, 0.5f, 0.f, 0.f, 0.f, 1.f);
    clip.addScaleKey(0, 0.5f, 1.f, 1.f, 1.f);
    clip.addEvent(0.25f, "step", "left");

    REQUIRE(clip.setPositionKey(0, 1, 0.1f, 4.f, 5.f, 6.f));
    CHECK_EQ(clip.getPositionKeyTime(0, 0), 0.f);
    CHECK_EQ(clip.getPositionKeyTime(0, 1), 0.1f);
    CHECK_EQ(clip.getPositionKeyX(0, 1), 4.f);
    REQUIRE(clip.setRotationKey(0, 0, 0.4f, 0.f, 2.f, 0.f, 0.f));
    CHECK(std::fabs(clip.getRotationKeyY(0, 0) - 1.f) < 1e-6f);
    REQUIRE(clip.setEvent(0, 0.75f, "land", "hard"));
    CHECK_EQ(clip.getEventName(0), std::string("land"));
    CHECK_EQ(clip.getEventPayload(0), std::string("hard"));
    CHECK(!clip.removePositionKey(3, 0));
    REQUIRE(clip.removeScaleKey(0, 0));
    CHECK_EQ(clip.getScaleKeyCount(0), 0);
    REQUIRE(clip.clearTrack(0));
    CHECK_EQ(clip.getPositionKeyCount(0), 0);
    CHECK_EQ(clip.getRotationKeyCount(0), 0);
    REQUIRE(clip.removeEvent(0));
    CHECK_EQ(clip.getEventCount(), 0);
}

TEST_CASE("animation.clip.notifyContractRejectsMissingSemanticEvents") {
    AnimClip clip("vault-low");
    clip.setDuration(1.f);
    clip.addEvent(0.25f, "contact.left_hand");
    clip.addEvent(0.75f, "land");

    CHECK(clip.hasEvent("contact.left_hand"));
    CHECK(!clip.hasEvent("contact.right_hand"));
    auto complete = clip.validateNotifyContract({"contact.left_hand", "land"});
    REQUIRE(complete.ok());
    auto missing = clip.validateNotifyContract({"contact.left_hand", "contact.right_hand", "land"});
    CHECK(!missing.ok());
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(eve::StatusCode::Rejected));
}
}  // namespace test_animation_clip_editing_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_constraints_sync)
#include "PrivateTestScope.h"
#include <cmath>
#include "animation/AnimClip.h"
#include "animation/AnimConstraintStack.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSyncGroup.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_constraints_sync.cpp
namespace test_animation_constraints_sync {
using namespace eve::animation;

TEST_CASE("animation.constraints.footIkPlantsAndOrders") {
    AnimSkeleton skeleton;
    const int    hip  = skeleton.addBone("hip");
    const int    knee = skeleton.addBone("knee", hip);
    const int    foot = skeleton.addBone("foot", knee);
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
}  // namespace test_animation_constraints_sync
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_curves)
#include "PrivateTestScope.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "animation/AnimCurveLibrary.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_curves.cpp
namespace test_animation_curves_unit {
namespace {
using Bytes = std::vector<std::byte>;
void integer(Bytes &out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(std::byte((value >> (8 * i)) & 255));
}
void number(Bytes &out, float value) { integer(out, std::bit_cast<std::uint32_t>(value)); }
void string(Bytes &out, const std::string &value) {
    integer(out, static_cast<std::uint32_t>(value.size()));
    for (char c : value) out.push_back(std::byte(c));
}
Bytes fixture() {
    Bytes out;
    integer(out, 0x43465645);
    integer(out, 1);
    integer(out, 2);
    string(out, "walk");
    number(out, 1);
    integer(out, 1);
    string(out, "contact_l");
    integer(out, 3);
    for (auto key : {std::pair{0.f, 0.f}, std::pair{.25f, 1.f}, std::pair{1.f, 0.f}}) {
        number(out, key.first);
        number(out, key.second);
    }
    string(out, "absent");
    number(out, 0);
    integer(out, 0);
    return out;
}
}  // namespace
TEST_CASE("animation.curves.samplingWrapsClampsAndDistinguishesAbsence") {
    eve::animation::AnimCurveLibrary curves;
    auto                             loaded = curves.load(fixture());
    REQUIRE(loaded.ok());
    CHECK(curves.size() == 2);
    for (double t : {.125, 1.125, -.875, 1000000.125}) {
        auto sample = curves.sample("walk", "contact_l", t, true);
        REQUIRE(sample.ok());
        REQUIRE(sample.value().has_value());
        CHECK(std::abs(*sample.value() - .5f) < 1e-6f);
    }
    for (double t : {-1., 2.}) {
        auto sample = curves.sample("walk", "contact_l", t, false);
        REQUIRE(sample.ok());
        CHECK(*sample.value() == 0);
    }
    auto absent = curves.sample("walk", "contact_r", 0, false);
    REQUIRE(absent.ok());
    CHECK(!absent.value());
    auto empty = curves.sample("absent", "contact_l", 0, true);
    REQUIRE(empty.ok());
    CHECK(!empty.value());
    auto missing = curves.sample("missing", "contact_l", 0, false);
    CHECK(!missing.ok());
    auto invalid = curves.sample("walk", "contact_l", std::numeric_limits<double>::infinity(), true);
    CHECK(!invalid.ok());
}
TEST_CASE("animation.curves.rejectsMalformedInputAtomically") {
    auto                             bytes = fixture();
    eve::animation::AnimCurveLibrary curves;
    auto                             loaded = curves.load(bytes);
    REQUIRE(loaded.ok());
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        auto rejected = curves.load(std::span(bytes).first(size));
        CHECK(!rejected.ok());
        CHECK(curves.size() == 2);
    }
    std::vector<Bytes> cases;
    auto               version = bytes;
    version[4]                 = std::byte(2);
    cases.push_back(version);
    auto trailing = bytes;
    trailing.push_back(std::byte(0));
    cases.push_back(trailing);
    auto nan = bytes;
    // Header(12), source length/name(8), duration at offset 20.
    for (int i = 0; i < 4; ++i) nan[20 + i] = std::byte((0x7fc00000u >> (i * 8)) & 255);
    cases.push_back(nan);
    auto duplicate = bytes;
    // Duplicate a complete source record by appending the first record instead of the absent record.
    duplicate.resize(bytes.size() - 18);  // absent: length/name(10), duration/count(8).
    duplicate.insert(duplicate.end(), bytes.begin() + 12, bytes.end() - 18);
    cases.push_back(duplicate);
    auto changedInteger = [&](std::size_t offset, std::uint32_t value) {
        auto changed = bytes;
        for (int i = 0; i < 4; ++i) changed[offset + i] = std::byte((value >> (i * 8)) & 255);
        cases.push_back(changed);
    };
    changedInteger(8, 100001);                              // source limit
    changedInteger(24, 65);                                 // channel limit
    changedInteger(41, 1000001);                            // key limit
    changedInteger(53, 0);                                  // duplicate key time
    changedInteger(61, std::bit_cast<std::uint32_t>(.9f));  // missing duration endpoint
    auto invalidName = bytes;
    invalidName[16]  = std::byte(0xff);
    cases.push_back(invalidName);
    auto duplicateChannel = bytes;
    duplicateChannel[24]  = std::byte(2);
    duplicateChannel.insert(duplicateChannel.begin() + 69, bytes.begin() + 28, bytes.begin() + 69);
    cases.push_back(duplicateChannel);
    for (const auto &invalid : cases) {
        auto rejected = curves.load(invalid);
        CHECK(!rejected.ok());
        auto sample = curves.sample("walk", "contact_l", .25, false);
        REQUIRE(sample.ok());
        CHECK(*sample.value() == 1);
    }
}
}  // namespace test_animation_curves_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_motion_normalization)
#include "PrivateTestScope.h"
#include <array>
#include <cmath>
#include <limits>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_motion_normalization.cpp
namespace test_animation_motion_normalization {
// animation_motion_normalization.cpp
using namespace eve::animation;

namespace {
MotionFeatureLayout positionLayout() {
    MotionFeatureLayout layout;
    layout.sampleRate = 10;
    MotionFeatureChannel channel;
    channel.kind   = MotionFeatureKind::Position;
    channel.source = MotionFeatureSource::Pose;
    channel.query  = MotionFeatureQuery::Character;
    channel.bone   = 1;
    channel.origin = 0;
    channel.axes   = 1;
    channel.weight = 1.f;
    layout.channels.push_back(channel);
    return layout;
}

struct NormalizationFixture {
    AnimSkeleton skeleton;
    AnimClip     low{"low"}, high{"high"};
    NormalizationFixture() {
        skeleton.addBone("root");
        skeleton.addBone("sample", 0);
        for (auto *clip : {&low, &high}) {
            clip->setDuration(1.f);
            clip->setLoop(false);
        }
        low.addPositionKey(1, 0.f, 0.f, 0.f, 0.f);
        high.addPositionKey(1, 0.f, 10.f, 0.f, 0.f);
    }
    void add(MotionDatabase &database) {
        database.addClip(&low);
        database.addClip(&high);
    }
};
}  // namespace

TEST_CASE("animation.motionSchema.normalizationRangesPreserveMultiplicityAndSamplingIntervals") {
    NormalizationFixture fixture;
    MotionDatabase       database(&fixture.skeleton);
    fixture.add(database);
    REQUIRE(database.setFeatureLayout(positionLayout()).ok());
    std::vector<MotionNormalizationRange> ranges{{0, 0.f, 1.f}, {0, 0.f, 1.f}, {1, 0.f, 1.f}};
    auto                                  configured = database.setFeatureNormalizationRanges(ranges);
    REQUIRE(configured.ok());
    CHECK(configured.value() == 33);
    ranges.clear();
    database.bake();
    float high = 0.f;
    database.getFeature(11, &high, 1);
    CHECK(std::abs(high - 1.5f) < 1e-5f);

    MotionDatabase ordinary(&fixture.skeleton);
    fixture.add(ordinary);
    REQUIRE(ordinary.setFeatureLayout(positionLayout()).ok());
    ordinary.bake();
    ordinary.getFeature(11, &high, 1);
    CHECK(std::abs(high - 1.f) < 1e-5f);
}

TEST_CASE("animation.motionSchema.normalizationRangeValidationIsAtomic") {
    NormalizationFixture fixture;
    MotionDatabase       database(&fixture.skeleton);
    fixture.add(database);
    REQUIRE(database.setFeatureLayout(positionLayout()).ok());
    std::vector<MotionNormalizationRange> valid{{0, 0.f, 0.f}};
    REQUIRE(database.setFeatureNormalizationRanges(valid).ok());
    for (const auto &invalid :
         std::vector<MotionNormalizationRange>{{-1, 0.f, 1.f},
                                               {2, 0.f, 1.f},
                                               {0, -.1f, 1.f},
                                               {0, .5f, .4f},
                                               {0, 0.f, 2.f},
                                               {0, 0.f, std::numeric_limits<float>::quiet_NaN()}}) {
        REQUIRE(!database.setFeatureNormalizationRanges(std::span(&invalid, 1)).ok());
    }
    database.bake();
    float high = 0.f;
    database.getFeature(11, &high, 1);
    // The retained one-sample low range has zero deviation and therefore unit scale.
    CHECK(std::abs(high - 10.f) < 1e-5f);
}

// animation_motion_schema.cpp
using namespace eve::animation;

TEST_CASE("animation.motionSchema.velocitySelectionCopiesQueryAndRejectsInvalidReplacement") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip forward("forward"), backward("backward");
    for (auto *clip : {&forward, &backward}) {
        const float direction = clip == &forward ? 1.f : -1.f;
        clip->setDuration(1.f);
        clip->addPositionKey(1, 0.f, 0.f, 0.f, -.5f * direction);
        clip->addPositionKey(1, 1.f, 0.f, 0.f, .5f * direction);
    }
    MotionFeatureChannel channel;
    channel.kind  = MotionFeatureKind::Velocity;
    channel.query = MotionFeatureQuery::Character;
    channel.bone  = 1;
    channel.axes  = 4;
    MotionFeatureLayout layout{20, {channel}};
    MotionDatabase      db(&skeleton);
    REQUIRE(db.setFeatureLayout(layout).ok());
    layout.channels[0].axes = 7;
    CHECK(db.getFeatureSize() == 1);
    db.addClip(&forward);
    db.addClip(&backward);
    db.bake();
    CHECK(db.getFrameCount() == 42);
    CHECK(!db.setFeatureLayout(layout).ok());
    for (int direction : {-1, 1}) {
        MotionMatcher                          matcher(&skeleton, &db);
        const std::array<MotionSearchRange, 2> ranges{{{0, .5f, .5f}, {1, .5f, .5f}}};
        REQUIRE(matcher.setCandidateRanges(ranges).ok());
        AnimPose current(2), previous(2);
        previous.setLocalPosition(1, 0.f, 0.f, -direction / 60.f);
        REQUIRE(matcher.setFeatureQuery(current, previous, 1.f / 60.f, {}).ok());
        previous.setLocalPosition(1, 0.f, 0.f, 100.f);
        CHECK(!matcher.setFeatureQuery(current, previous, 0.f, {}).ok());
        CHECK(!matcher.setQueryPose(current).ok());
        const std::array<MotionTrajectorySample, 3> basic{};
        CHECK(!matcher.setTrajectory(basic).ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
    }
}

TEST_CASE("animation.motionSchema.layoutFailurePreservesConfiguration") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    MotionDatabase       db(&skeleton);
    MotionFeatureChannel channel;
    channel.kind               = MotionFeatureKind::Velocity;
    channel.normalizationGroup = "speed";
    MotionFeatureLayout layout{30, {channel}};
    REQUIRE(db.setFeatureLayout(layout).ok());
    layout.channels.push_back(channel);
    layout.channels.back().normalizeVelocity = true;
    layout.channels.back().axes              = 0;
    CHECK(!db.setFeatureLayout(layout).ok());
    CHECK(db.getFeatureSize() == 3);
    layout.channels.pop_back();
    layout.channels[0].weight = std::numeric_limits<float>::quiet_NaN();
    CHECK(!db.setFeatureLayout(layout).ok());
    CHECK(db.getFeatureSize() == 3);
    CHECK(db.hasFeatureLayout());
}

TEST_CASE("animation.motionSchema.mixedVelocityPoolPreservesAuthoredLengthScale") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip clip("travel");
    clip.setDuration(1.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 1.f, 2.f, 0.f, 0.f);
    MotionFeatureChannel raw;
    raw.kind                     = MotionFeatureKind::Velocity;
    raw.axes                     = 1;
    raw.characterSpaceVelocity   = false;
    raw.normalizationGroup       = "speed";
    raw.query                    = MotionFeatureQuery::Character;
    auto normalized              = raw;
    normalized.normalizeVelocity = true;
    MotionDatabase db(&skeleton);
    REQUIRE(db.setFeatureLayout({30, {raw, normalized}, 100.f}).ok());
    db.addClip(&clip);
    db.bake();
    const auto &feature = db.frameAt(15).feature;
    CHECK(std::abs(feature[0] - 1.f) < .0001f);
    CHECK(std::abs(feature[1] + 1.f) < .0001f);
    MotionMatcher matcher(&skeleton, &db);
    AnimPose      current(1), previous(1);
    current.setLocalPosition(0, 1.f, 0.f, 0.f);
    previous.setLocalPosition(0, 1.f - 2.f / 60.f, 0.f, 0.f);
    REQUIRE(matcher.setFeatureQuery(current, previous, 1.f / 60.f, {}).ok());
    matcher.search();
    CHECK(matcher.getLastSearchCost() < .000001f);
}

TEST_CASE("animation.motionSchema.playRateMatchesUnnormalizedTrajectorySpeedRatio") {
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
    MotionDatabase db(&skeleton);
    REQUIRE(db.setFeatureLayout({30, {velocity}, 100.f}).ok());
    db.addClip(&clip);
    db.bake();
    MotionMatcher                          matcher(&skeleton, &db);
    const std::array<MotionSearchRange, 1> ranges{{MotionSearchRange{0, 0.f, 0.f}}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    REQUIRE(matcher.setPlayRateRange(.85f, 1.15f).ok());
    const float oldMin = matcher.getPlayRateMinimum(), oldMax = matcher.getPlayRateMaximum();
    CHECK(!matcher.setPlayRateRange(1.2f, .8f).ok());
    CHECK(matcher.getPlayRateMinimum() == oldMin);
    CHECK(matcher.getPlayRateMaximum() == oldMax);
    AnimPose                                     pose(1);
    std::array<MotionFeatureTrajectorySample, 1> samples{};
    samples[0].vx = 2.f;
    REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
    matcher.search();
    CHECK(std::abs(matcher.getPlayRate() - 1.15f) < .0001f);
    matcher.update(.5f);
    CHECK(std::abs(matcher.getMatchedTime() - .575f) < .0001f);
}

TEST_CASE("animation.motionSchema.arbitraryTrajectoryTimesSelectTravelAcrossClipBoundaries") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    for (bool loop : {false, true}) {
        AnimClip forward("forward"), backward("backward");
        for (auto *clip : {&forward, &backward}) {
            clip->setDuration(1.f);
            clip->setLoop(loop);
            clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
            clip->addPositionKey(0, 1.f, clip == &forward ? 2.f : -2.f, 0.f, 0.f);
        }
        MotionFeatureChannel past;
        past.source                 = MotionFeatureSource::Trajectory;
        past.query                  = MotionFeatureQuery::Character;
        past.axes                   = 1;
        past.sampleTime             = -.2f;
        MotionFeatureChannel future = past;
        future.sampleTime           = 1.35f;
        MotionDatabase db(&skeleton);
        REQUIRE(db.setFeatureLayout({20, {past, future}}).ok());
        db.addClip(&forward);
        db.addClip(&backward);
        db.bake();
        CHECK(db.getFrameCount() == (loop ? 40 : 42));
        for (int direction : {-1, 1}) {
            MotionMatcher                          matcher(&skeleton, &db);
            const std::array<MotionSearchRange, 2> ranges{{{0, 0.f, 0.f}, {1, 0.f, 0.f}}};
            REQUIRE(matcher.setCandidateRanges(ranges).ok());
            AnimPose                                     pose(1);
            std::array<MotionFeatureTrajectorySample, 2> samples{};
            samples[0].seconds = -.2f;
            samples[0].x       = -.4f * direction;
            samples[1].seconds = 1.35f;
            samples[1].x       = 2.7f * direction;
            REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
            CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, std::span(samples).first(1)).ok());
            samples[1].seconds = samples[0].seconds;
            CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
            matcher.search();
            CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
        }
    }
}

// animation_motion_transition_blocks.cpp
using namespace eve::animation;

namespace {
struct BlockFixture {
    AnimSkeleton   skeleton;
    AnimClip       first{"first"}, second{"second"};
    MotionDatabase database{&skeleton};
    BlockFixture() {
        skeleton.addBone("root");
        for (auto *clip : {&first, &second}) {
            clip->setDuration(1.f);
            clip->setLoop(false);
            clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
            database.addClip(clip);
        }
        database.bake();
    }
};
}  // namespace

TEST_CASE("animation.motionMatching.transitionBlocksPreventEntryButPreserveContinuation") {
    BlockFixture  fixture;
    MotionMatcher continuing(&fixture.skeleton, &fixture.database);
    continuing.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(continuing.setCandidateRanges(seed).ok());
    continuing.search();
    continuing.update(.3f);
    const float time = continuing.getMatchedTime();
    REQUIRE(time > .1f);
    std::vector<MotionSearchRange> ranges{{0, 0.f, 1.f, -100.f, false, {{.1f, .8f}}}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(continuing.setCandidateRanges(ranges).ok());
    continuing.search();
    CHECK(continuing.getMatchedClipIndex() == 0);
    CHECK(std::abs(continuing.getMatchedTime() - time) < 1e-6f);

    // A fresh search would strongly prefer clip 0's bias, but its selected
    // middle section is protected. This does not remove it from continuation.
    ranges[0].start = .2f;
    ranges[0].end   = .7f;
    MotionMatcher fresh(&fixture.skeleton, &fixture.database);
    REQUIRE(fresh.setCandidateRanges(ranges).ok());
    fresh.search();
    CHECK(fresh.getMatchedClipIndex() == 1);
    REQUIRE(continuing.setCandidateRanges(ranges).ok());
    continuing.search();
    CHECK(continuing.getMatchedClipIndex() == 0);

    // Ordinary membership still applies even to a protected ongoing pose.
    REQUIRE(continuing.setCandidateRanges(std::span(ranges).last(1)).ok());
    continuing.search();
    CHECK(continuing.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.transitionBlockBoundariesAreCopiedAndUnioned") {
    BlockFixture fixture;
    const float  start = fixture.database.getFrameTime(3);
    const float  end   = fixture.database.getFrameTime(4);
    REQUIRE(end > start);
    std::vector<MotionSearchRange> ranges{
        {0, start, start, -100.f, false, {{start, end}}}, {0, start, start, -200.f}, {1, 0.f, 0.f}};
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    ranges[0].transitionBlocks.clear();
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);

    // Exact end is eligible; exact start was blocked, including an overlapping
    // duplicate candidate range with a better score and no block of its own.
    ranges = {{0, end, end, -100.f, false, {{start, end}}}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 0);
    CHECK(matcher.getMatchedTime() == end);
}

TEST_CASE("animation.motionMatching.transitionBlockFailureIsAtomicAndAllBlockedCanRecover") {
    BlockFixture                   fixture;
    MotionMatcher                  matcher(&fixture.skeleton, &fixture.database);
    std::vector<MotionSearchRange> blocked{{0, 0.f, 1.f, 0.f, false, {{0.f, 2.f}}}};
    REQUIRE(matcher.setCandidateRanges(blocked).ok());
    matcher.search();
    CHECK(matcher.getMatchedFrame() == -1);
    for (const auto &invalid : {MotionTransitionBlock{-1.f, 1.f},
                                {1.f, 1.f},
                                {2.f, 1.f},
                                {0.f, std::numeric_limits<float>::infinity()},
                                {std::numeric_limits<float>::quiet_NaN(), 1.f}}) {
        std::vector<MotionSearchRange> attempted{{1, 0.f, 1.f, 0.f, false, {invalid}}};
        REQUIRE(!matcher.setCandidateRanges(attempted).ok());
        matcher.search();
        CHECK(matcher.getMatchedFrame() == -1);
    }
    std::vector<MotionSearchRange> recovered{{1, 0.f, 1.f}};
    REQUIRE(matcher.setCandidateRanges(recovered).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.poseReselectHistoryExpiresAndKeepsContinuationEligible") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    REQUIRE(matcher.setPoseReselectHistory(.3f).ok());
    REQUIRE(!matcher.setPoseReselectHistory(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(std::abs(matcher.getPoseReselectHistory() - .3f) < 1e-6f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.update(.1f);

    std::vector<MotionSearchRange> ranges{{0, .1f, .1f, -100.f, false, {}, 200.f}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);

    matcher.update(.31f);
    ranges[1].continuingCostBias = 200.f;
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 0);
}

TEST_CASE("animation.motionMatching.anyStrictCostImprovementCanSwitch") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    std::vector<MotionSearchRange> ranges{{0, 0.f, 1.f, 0.f, false, {}, 1.f}, {1, 0.f, 1.f, .95f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.replacementRangesInvalidateDistantSameClipContinuation") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    REQUIRE(matcher.getMatchedClipIndex() == 0);
    REQUIRE(matcher.getMatchedTime() < .5f);

    // The same asset is still present, but only at a distant phase. It cannot
    // masquerade as continuation of the current out-of-range playhead.
    std::vector<MotionSearchRange> ranges{{0, .8f, 1.f, 10.f}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.authoredCandidateCostOverrideUsesLastActiveWindow") {
    BlockFixture                   fixture;
    MotionMatcher                  matcher(&fixture.skeleton, &fixture.database);
    const float                    time = fixture.database.getFrameTime(3);
    std::vector<MotionSearchRange> ranges{
        {0, time, time, -10.f, false, {}, 0.f, {{0.f, 1.f, -2.f}, {time, time + .01f, 4.f}}}, {1, time, time, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);

    // The override end is exclusive, so its base -10 cost wins exactly there.
    const float end         = fixture.database.getFrameTime(4);
    ranges[0].start         = end;
    ranges[0].end           = end;
    ranges[0].costOverrides = {{0.f, end, 4.f}};
    ranges[1].start         = end;
    ranges[1].end           = end;
    MotionMatcher boundary(&fixture.skeleton, &fixture.database);
    REQUIRE(boundary.setCandidateRanges(ranges).ok());
    boundary.search();
    CHECK(boundary.getMatchedClipIndex() == 0);
}

TEST_CASE("animation.motionMatching.continuingCostBiasAndOverrideControlRetention") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    REQUIRE(matcher.getMatchedClipIndex() == 0);

    std::vector<MotionSearchRange> ranges{{0, 0.f, 1.f, 0.f, false, {}, -5.f}, {1, 0.f, 1.f, -1.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 0);

    // The source notify replaces the database continuing bias at this pose.
    ranges[0].continuingCostOverrides = {{.2f, .8f, 5.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.duplicateRangeRetainsCostPairFromWinningCandidate") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    std::vector<MotionSearchRange> ranges{
        {0, 0.f, 1.f, -2.f, false, {}, 10.f}, {0, 0.f, 1.f, -1.f, false, {}, -10.f}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}
}  // namespace test_animation_motion_normalization
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_motion)
#include "PrivateTestScope.h"
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
namespace test_animation_motion_unit {
// animation_motion.cpp
using namespace eve::animation;

namespace {

[[maybe_unused]] eve::SimulationStep stepAt(std::uint64_t tick, double seconds) {
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


// animation_motion_continuity.cpp
using namespace eve::animation;

namespace {
[[maybe_unused]] void locomotion(AnimClip &clip, float speed) {
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
[[maybe_unused]] std::vector<std::byte> curveBytes() {
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
[[maybe_unused]] void bones(AnimSkeleton &skeleton) {
    skeleton.addBone("root");
    skeleton.addBone("left", 0);
    skeleton.addBone("right", 0);
    skeleton.addBone("pelvis", 0);
}
[[maybe_unused]] void swing(AnimClip &clip, float direction) {
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
}  // namespace test_animation_motion_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_retarget_compression)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_retarget_compression.cpp
namespace test_animation_retarget_compression {
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
    const int    sourceRoot = source.addBone("root");
    const int    sourceHand = source.addBone("hand", sourceRoot);
    source.setBindPosition(sourceHand, 0.f, 1.f, 0.f);

    AnimSkeleton target;
    const int    targetRoot = target.addBone("root");
    const int    targetHand = target.addBone("hand", targetRoot);
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
    const int    sourceRoot = source.addBone("mixamorig:Root");
    const int    sourceHips = source.addBone("mixamorig:Hips", sourceRoot);
    source.setBindPosition(sourceHips, 0.f, 1.f, 0.f);

    AnimSkeleton target;
    const int    targetRoot      = target.addBone("root");
    const int    targetPelvis    = target.addBone("pelvis", targetRoot);
    const int    targetAccessory = target.addBone("weapon_socket", targetPelvis);
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
    AnimSkeleton    source;
    const int       sourceRoot   = source.addBone("root");
    const int       sourceParent = source.addBone("shoulder", sourceRoot);
    const int       sourceHand   = source.addBone("hand", sourceParent);
    source.setBindRotation(sourceParent, 0.f, 0.f, halfSqrt, halfSqrt);

    AnimSkeleton target;
    const int    targetRoot = target.addBone("root");
    const int    targetHand = target.addBone("hand", targetRoot);  // Deliberately different hierarchy and axes.

    AnimClip clip("axis_test");
    clip.setDuration(1.f);
    clip.setLoop(false);
    // Source hand world rotation is bind-parent Z90 followed by local X90.
    clip.addRotationKey(sourceHand, 0.f, halfSqrt, 0.f, 0.f, halfSqrt);
    clip.addRotationKey(sourceHand, 1.f, halfSqrt, 0.f, 0.f, halfSqrt);

    AnimRetargetProfile       profile;
    std::unique_ptr<AnimClip> retargeted(clip.retargetWithProfile(&source, &target, &profile));
    AnimPose                  pose;
    retargeted->sample(0.5f, &pose, &target);
    CHECK(std::fabs(pose.getLocalRotationX(targetHand) - halfSqrt) < 1e-4f);
    CHECK(std::fabs(pose.getLocalRotationY(targetHand)) < 1e-4f);
    CHECK(std::fabs(pose.getLocalRotationZ(targetHand)) < 1e-4f);
}
}  // namespace test_animation_retarget_compression
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_root_motion_policy)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <memory>
#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/RootMotionPolicy.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_root_motion_policy.cpp
namespace test_animation_root_motion_policy {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::animation;

namespace {

std::unique_ptr<AnimSkeleton> makeTwoBoneSkeleton() {
    auto sk   = std::make_unique<AnimSkeleton>();
    int  root = sk->addBone("root", -1);
    sk->setBindPosition(root, 0.f, 0.f, 0.f);
    int hip = sk->addBone("hip", root);
    sk->setBindPosition(hip, 0.f, 1.f, 0.f);
    return sk;
}

std::unique_ptr<AnimClip> makeLocomotionClip(float speed, float dur) {
    auto clip = std::make_unique<AnimClip>("walk");
    clip->setLoop(true);
    clip->setSampleRate(20.f);
    clip->setDuration(dur);
    clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip->addPositionKey(0, dur, 0.f, 0.f, speed * dur);
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, dur, 0.f, 1.f, speed * dur);
    return clip;
}

std::unique_ptr<AnimClip> makeDiagonalClip(float dur) {
    auto clip = std::make_unique<AnimClip>("diag");
    clip->setLoop(false);
    clip->setSampleRate(20.f);
    clip->setDuration(dur);
    clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip->addPositionKey(0, dur, 1.f, 0.5f, 2.f);
    return clip;
}

}  // namespace

TEST_CASE("animation.rootMotionPolicy.locksVerticalAxis") {
    auto             sk   = makeTwoBoneSkeleton();
    auto             clip = makeDiagonalClip(1.f);
    AnimPlayer       player(sk.get());
    RootMotionPolicy policy;
    policy.lockAxes = RootMotionLockAxes::VerticalY;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.5f);
    CHECK(std::fabs(player.getRootMotionX() - 0.5f) < 1e-4f);
    CHECK(std::fabs(player.getRootMotionY()) < 1e-5f);
    CHECK(std::fabs(player.getRootMotionZ() - 1.f) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.bakesUnlockedTranslationIntoPose") {
    auto             sk   = makeTwoBoneSkeleton();
    auto             clip = makeLocomotionClip(2.f, 1.f);
    AnimPlayer       player(sk.get());
    RootMotionPolicy policy;
    policy.lockAxes                = RootMotionLockAxes::VerticalY;
    policy.bakeTranslationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.3f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.6f) < 0.01f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
    player.update(0.3f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.6f) < 0.01f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.bakeHoldsBindAcrossLoop") {
    auto             sk   = makeTwoBoneSkeleton();
    auto             clip = makeLocomotionClip(2.f, 1.f);
    AnimPlayer       player(sk.get());
    RootMotionPolicy policy;
    policy.bakeTranslationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.9f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
    player.update(0.2f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
    CHECK(player.getRootMotionZ() > 0.3f);
}

TEST_CASE("animation.rootMotionPolicy.bakeRotationUsesBind") {
    auto sk   = makeTwoBoneSkeleton();
    auto clip = std::make_unique<AnimClip>("spin");
    clip->setLoop(true);
    clip->setDuration(1.f);
    clip->addRotationKey(0, 0.f, 0.f, 0.f, 0.f, 1.f);
    clip->addRotationKey(0, 1.f, 0.f, 1.f, 0.f, 0.f);  // 180° around Y
    AnimPlayer       player(sk.get());
    RootMotionPolicy policy;
    policy.bakeRotationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.5f);
    CHECK(std::fabs(player.getPose()->getLocalRotationX(0)) < 1e-4f);
    CHECK(std::fabs(player.getPose()->getLocalRotationY(0)) < 1e-4f);
    CHECK(std::fabs(player.getPose()->getLocalRotationZ(0)) < 1e-4f);
    CHECK(std::fabs(player.getPose()->getLocalRotationW(0) - 1.f) < 1e-4f);
    player.update(0.5f);
    CHECK(std::fabs(player.getPose()->getLocalRotationY(0)) < 1e-4f);
    CHECK(std::fabs(player.getPose()->getLocalRotationW(0) - 1.f) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.locksApplyAfterCharacterFacing") {
    TransformTRS raw = TransformTRS::identity();
    raw.pz           = 1.f;
    RootMotionPolicy policy;
    policy.lockAxes        = RootMotionLockAxes::X;
    policy.applySpace      = RootMotionApplySpace::CharacterFacing;
    policy.characterYaw    = static_cast<float>(M_PI * 0.5);  // local +Z → published +X, then lock X
    const TransformTRS out = applyRootMotionPolicy(raw, policy);
    CHECK(std::fabs(out.px) < 1e-5f);
    CHECK(std::fabs(out.pz) < 1e-5f);

    auto       sk   = makeTwoBoneSkeleton();
    auto       clip = makeLocomotionClip(1.f, 1.f);
    AnimPlayer player(sk.get());
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.25f);
    CHECK(std::fabs(player.getRootMotionX()) < 1e-4f);
    CHECK(std::fabs(player.getRootMotionZ()) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.crossFadeBlendsPublishedDeltaAndBakesBind") {
    auto walk = makeLocomotionClip(2.f, 1.f);
    auto idle = std::make_unique<AnimClip>("idle-offset");
    idle->setLoop(true);
    idle->setDuration(1.f);
    idle->addPositionKey(0, 0.f, 0.f, 0.f, 5.f);
    idle->addPositionKey(0, 1.f, 0.f, 0.f, 5.f);

    auto             sk = makeTwoBoneSkeleton();
    AnimPlayer       player(sk.get());
    RootMotionPolicy policy;
    policy.bakeTranslationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(walk.get());
    player.update(0.5f);
    CHECK(std::fabs(player.getRootMotionZ() - 1.f) < 0.02f);

    player.crossFade(idle.get(), 0.2f);
    player.update(0.1f);
    // Incoming idle has zero travel; outgoing walk still contributes ~0.2 at t=0.5.
    CHECK(player.getRootMotionZ() > 0.05f);
    CHECK(player.getRootMotionZ() < 0.2f);
    // Bake plants on bind, not the idle clip's offset root of 5.
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.parseRejectsUnknownNames") {
    auto axes = parseRootMotionLockAxes("sideways");
    CHECK(!axes.ok());
    auto space = parseRootMotionApplySpace("world");
    CHECK(!space.ok());
    auto okAxes = parseRootMotionLockAxes("horizontal");
    REQUIRE(okAxes.ok());
    CHECK(okAxes.value() == RootMotionLockAxes::HorizontalXZ);
    auto okSpace = parseRootMotionApplySpace("facing");
    REQUIRE(okSpace.ok());
    CHECK(okSpace.value() == RootMotionApplySpace::CharacterFacing);
}

TEST_CASE("animation.rootMotionPolicy.characterFacingRotatesPlanarDelta") {
    TransformTRS raw = TransformTRS::identity();
    raw.pz           = 1.f;
    RootMotionPolicy policy;
    policy.applySpace      = RootMotionApplySpace::CharacterFacing;
    policy.characterYaw    = static_cast<float>(M_PI * 0.5);  // +90° → local +Z becomes +X
    const TransformTRS out = applyRootMotionPolicy(raw, policy);
    CHECK(std::fabs(out.px - 1.f) < 1e-5f);
    CHECK(std::fabs(out.pz) < 1e-5f);
}

TEST_CASE("animation.rootMotionPolicy.playerCharacterFacing") {
    auto             sk   = makeTwoBoneSkeleton();
    auto             clip = makeLocomotionClip(1.f, 1.f);
    AnimPlayer       player(sk.get());
    RootMotionPolicy policy;
    policy.applySpace   = RootMotionApplySpace::CharacterFacing;
    policy.characterYaw = static_cast<float>(M_PI * 0.5);
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.25f);
    CHECK(std::fabs(player.getRootMotionX() - 0.25f) < 1e-4f);
    CHECK(std::fabs(player.getRootMotionZ()) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.rejectsNonFiniteYaw") {
    auto             sk = makeTwoBoneSkeleton();
    AnimPlayer       player(sk.get());
    RootMotionPolicy policy;
    policy.characterYaw = std::numeric_limits<float>::quiet_NaN();
    CHECK(!player.setRootMotionPolicy(policy).ok());
    CHECK(!player.setRootMotionCharacterYaw(std::numeric_limits<float>::infinity()).ok());
}

TEST_CASE("animation.rootMotionPolicy.defaultMatchesLegacyExtraction") {
    auto       sk   = makeTwoBoneSkeleton();
    auto       clip = makeLocomotionClip(2.f, 1.f);
    AnimPlayer player(sk.get());
    player.play(clip.get());
    player.update(0.3f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.6f) < 0.01f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0) - 0.6f) < 0.01f);
}
}  // namespace test_animation_root_motion_policy
#undef M_PI
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_skinned)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "Fixtures.h"
#include "PathBesideSource.h"
#include "ResourceTestSupport.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/Animation.h"
#include "common/Exception.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_skinned.cpp
namespace test_animation_skinned_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


using namespace eve::animation;
using namespace eve::graphics;



TEST_CASE("animation.skinned.mat4FromTRSIdentity") {
    TransformTRS t = TransformTRS::identity();
    Mat4         m = Mat4::fromTRS(t);
    CHECK(std::fabs(m.m[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[5] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[10] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[15] - 1.f) < 1e-5f);
    float ox, oy, oz;
    m.transformPoint(1.f, 2.f, 3.f, ox, oy, oz);
    CHECK(std::fabs(ox - 1.f) < 1e-5f);
    CHECK(std::fabs(oy - 2.f) < 1e-5f);
    CHECK(std::fabs(oz - 3.f) < 1e-5f);
}

TEST_CASE("animation.skinned.gpuPaletteOwnsAndValidatesMatrices") {
    Mesh                  mesh;
    std::array<float, 32> matrices{};
    matrices[0] = matrices[5] = matrices[10] = matrices[15] = 1.f;
    matrices[16] = matrices[21] = matrices[26] = matrices[31] = 1.f;
    matrices[28]                                              = 3.5f;
    CHECK(mesh.setSkinPalette(matrices.data(), 2).ok());
    CHECK(mesh.getSkinPaletteCount() == 2);
    CHECK(std::fabs(mesh.skinPalette()[28] - 3.5f) < 1e-6f);
    matrices[28] = 9.f;
    CHECK(std::fabs(mesh.skinPalette()[28] - 3.5f) < 1e-6f);
    CHECK(!mesh.setSkinPalette(nullptr, 1).ok());
    CHECK(!mesh.setSkinPalette(matrices.data(), -1).ok());
}

TEST_CASE("animation.skinned.worldMatrixMatchesTRS") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root  = sk->addBone("root", -1);
    const int                     child = sk->addBone("child", root);
    sk->setBindPosition(child, 0.f, 1.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 10.f, 0.f, 0.f);
    pose->computeWorld(sk.get());

    CHECK(std::fabs(pose->getWorldPositionX(child) - 10.f) < 1e-4f);
    CHECK(std::fabs(pose->getWorldPositionY(child) - 1.f) < 1e-4f);

    float mat[16];
    pose->getWorldMatrix(child, mat);
    CHECK(std::fabs(mat[12] - 10.f) < 1e-4f);
    CHECK(std::fabs(mat[13] - 1.f) < 1e-4f);
    CHECK(std::fabs(pose->getWorldMatrixElement(child, 12) - 10.f) < 1e-4f);
}
}  // namespace test_animation_skinned_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_smr_retarget)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSmr.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_smr_retarget.cpp
namespace test_animation_smr_retarget {
using namespace eve::animation;

namespace {

void buildArmature(AnimSkeleton &skel, float torsoHeight, float upperArm, float lowerArm) {
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

float handChestDistance(const AnimSkeleton &skel, const AnimClip &clip, float time) {
    AnimPose pose;
    clip.sample(time, &pose, &skel);
    pose.computeWorld(&skel);
    const int   chest = skel.findBone("Spine");
    const int   hand  = skel.findBone("LeftHand");
    const float dx    = pose.getWorldPositionX(hand) - pose.getWorldPositionX(chest);
    const float dy    = pose.getWorldPositionY(hand) - pose.getWorldPositionY(chest);
    const float dz    = pose.getWorldPositionZ(hand) - pose.getWorldPositionZ(chest);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool rotationKeysDiffer(const AnimClip &a, const AnimClip &b, int bone) {
    if (a.getRotationKeyCount(bone) == 0 || b.getRotationKeyCount(bone) == 0) return false;
    const float ax  = a.getRotationKeyX(bone, 0);
    const float ay  = a.getRotationKeyY(bone, 0);
    const float az  = a.getRotationKeyZ(bone, 0);
    const float aw  = a.getRotationKeyW(bone, 0);
    const float bx  = b.getRotationKeyX(bone, 0);
    const float by  = b.getRotationKeyY(bone, 0);
    const float bz  = b.getRotationKeyZ(bone, 0);
    const float bw  = b.getRotationKeyW(bone, 0);
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
    const float               fkGap = handChestDistance(target, *fk, 0.f);

    AnimRetargetProfile smr;
    smr.setSkinnedInteractionPreserve(true);
    smr.setInteractionContactThreshold(std::max(sourceGap * 1.25f, 0.35f));
    smr.setInteractionCorrectionWeight(1.f);
    smr.addInteractionIkChain("LeftShoulder", "LeftElbow", "LeftHand");
    std::unique_ptr<AnimClip> refined(clip.retargetWithProfile(&source, &target, &smr));
    const float               smrGap = handChestDistance(target, *refined, 0.f);

    CHECK(smr.getInteractionCorrectionCount() >= 1);
    CHECK(fkGap > sourceGap);
    // Either the world-space gap shrinks, or the IK rewrite clearly changed chain rotations.
    const bool gapImproved = smrGap + 1e-4f < fkGap;
    const bool keysChanged = rotationKeysDiffer(*fk, *refined, target.findBone("LeftElbow")) ||
                             rotationKeysDiffer(*fk, *refined, target.findBone("LeftShoulder"));
    const bool refinedDiffers = gapImproved || keysChanged;
    CHECK(refinedDiffers);
}
}  // namespace test_animation_smr_retarget
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_sprite_spine)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "animation/Animation.h"
#include "animation/SpineAnim.h"
#include "animation/SpineAtlas.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "common/Exception.h"
#include "filesystem/Filesystem.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_sprite_spine.cpp
namespace test_animation_sprite_spine_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


using namespace eve::animation;

TEST_CASE("animation.sprite.sheetGridAndNamed") {
    std::unique_ptr<SpriteSheet> sheet(new SpriteSheet());
    int                          added = sheet->setGrid(4, 2, 32, 48, 0, 0, 0, 0);
    CHECK_EQ(added, 8);
    CHECK_EQ(sheet->getFrameCount(), 8);
    CHECK_EQ(sheet->getFrameX(0), 0);
    CHECK_EQ(sheet->getFrameY(0), 0);
    CHECK_EQ(sheet->getFrameWidth(0), 32);
    CHECK_EQ(sheet->getFrameHeight(0), 48);
    CHECK_EQ(sheet->getFrameX(5), 32);  // row1 col1
    CHECK_EQ(sheet->getFrameY(5), 48);

    sheet->clear();
    int i0 = sheet->addFrame("idle0", 10, 20, 16, 16);
    int i1 = sheet->addFrame("idle1", 26, 20, 16, 16);
    CHECK_EQ(i0, 0);
    CHECK_EQ(i1, 1);
    CHECK_EQ(sheet->findFrame("idle1"), 1);
    CHECK_EQ(sheet->findFrame("missing"), -1);

    eve::graphics::Quad quad;
    sheet->applyToQuad(&quad, 1);
    CHECK_EQ(quad.getX(), 26);
    CHECK_EQ(quad.getY(), 20);
    CHECK_EQ(quad.getWidth(), 16);
    CHECK_EQ(quad.getHeight(), 16);
}

TEST_CASE("animation.sprite.clipAndPlayer") {
    auto                        *anim = Animation::create();
    std::unique_ptr<SpriteSheet> sheet(anim->newSpriteSheet());
    sheet->setGrid(4, 1, 16, 16);

    std::unique_ptr<SpriteClip> clip(anim->newSpriteClip("walk"));
    clip->setLoop(true);
    clip->addFrame(0, 0.1f);
    clip->addFrame(1, 0.1f);
    clip->addFrame(2, 0.1f);
    clip->addFrame(3, 0.1f);
    CHECK(std::fabs(clip->getDuration() - 0.4f) < 1e-5f);
    CHECK_EQ(clip->frameAtTime(0.05f), 0);
    CHECK_EQ(clip->frameAtTime(0.15f), 1);
    CHECK_EQ(clip->frameAtTime(0.45f), 0);  // loop

    std::unique_ptr<SpriteAnim> player(anim->newSpriteAnim());
    player->setSheet(sheet.get());
    eve::graphics::Quad quad;
    player->bindQuad(&quad);
    player->play(clip.get());
    CHECK(player->isPlaying());
    CHECK_EQ(player->getSheetFrame(), 0);
    CHECK_EQ(quad.getX(), 0);

    anim->update(0.15f);
    CHECK_EQ(player->getClipFrame(), 1);
    CHECK_EQ(player->getSheetFrame(), 1);
    CHECK_EQ(quad.getX(), 16);

    player->setLoop(false);
    player->setTime(0.f);
    player->play(clip.get());
    player->setLoop(false);
    // finish
    for (int i = 0; i < 10; ++i) player->update(0.1f);
    CHECK(player->isFinished());
    CHECK(!player->isPlaying());
    CHECK_EQ(player->getSheetFrame(), 3);
}

TEST_CASE("animation.sprite.uniformFpsRangeReverseAndEvents") {
    auto                       *anim = Animation::create();
    std::unique_ptr<SpriteClip> clip(anim->newSpriteClip("fx"));
    clip->addRange(2, 5, 20.f);
    CHECK_EQ(clip->getFrameCount(), 4);
    CHECK(std::fabs(clip->getFPS() - 20.f) < 1e-5f);
    clip->setFPS(10.f);
    CHECK(std::fabs(clip->getDuration() - 0.4f) < 1e-5f);

    std::unique_ptr<SpriteAnim> player(anim->newSpriteAnim());
    player->playReverse(clip.get());
    CHECK(player->getSpeed() < 0.f);
    CHECK_EQ(player->getSheetFrame(), 5);
    player->update(0.11f);
    CHECK_EQ(player->getSheetFrame(), 4);

    player->update(0.35f);
    CHECK(player->consumeLooped() >= 1);
    CHECK(player->getLoopCount() >= 1);

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
    player->addSpeedCurveKey(0.f, 0.5f);
    player->addSpeedCurveKey(1.f, 2.f);
    player->setSpeedCurveLoop(false);
    player->play(clip.get());
    CHECK(std::fabs(player->getSpeedCurveValue() - 0.5f) < 1e-5f);
    player->update(0.5f);
    CHECK(player->getSpeedCurveValue() > 1.2f);
    CHECK(player->getTime() > 0.5f);
    player->setSpeedCurveInterpolation("cubic");
    clip->addEvent(2, "impact");
    player->setFrame(2);
    CHECK_EQ(player->consumeEvent(), std::string("impact"));
    player->step(1);
    CHECK_EQ(player->getClipFrame(), 3);

    SpriteSheet trimmed;
    const int   tf = trimmed.addFrameTrimmed("trim", 2, 3, 10, 12, 32, 40, 7, 9);
    CHECK_EQ(trimmed.getFrameSourceWidth(tf), 32);
    CHECK_EQ(trimmed.getFrameSourceHeight(tf), 40);
    CHECK_EQ(trimmed.getFrameOffsetX(tf), 7);
    CHECK_EQ(trimmed.getFrameOffsetY(tf), 9);
}

TEST_CASE("animation.spine.atlasParse") {
    const char *atlasText =
        "hero.png\n"
        "size: 128,64\n"
        "format: RGBA8888\n"
        "filter: Linear,Linear\n"
        "repeat: none\n"
        "body\n"
        "  rotate: false\n"
        "  xy: 2, 2\n"
        "  size: 40, 50\n"
        "  orig: 40, 50\n"
        "  offset: 0, 0\n"
        "  index: -1\n"
        "head\n"
        "  rotate: false\n"
        "  xy: 50, 4\n"
        "  size: 24, 24\n"
        "  orig: 24, 24\n"
        "  offset: 0, 0\n"
        "  index: -1\n";

    std::unique_ptr<SpineAtlas> atlas(new SpineAtlas());
    std::string                 err;
    CHECK(atlas->loadFromText(atlasText, &err));
    CHECK_EQ(atlas->getPageCount(), 1);
    CHECK_EQ(atlas->getPageName(0), std::string("hero.png"));
    CHECK_EQ(atlas->getPageWidth(0), 128);
    CHECK_EQ(atlas->getPageHeight(0), 64);
    CHECK_EQ(atlas->getRegionCount(), 2);
    CHECK_EQ(atlas->findRegion("body"), 0);
    CHECK_EQ(atlas->getRegionWidth(0), 40);
    CHECK_EQ(atlas->getRegionHeight(0), 50);
    CHECK_EQ(atlas->getRegionX(1), 50);

    float u0, v0, u1, v1;
    atlas->getRegionUV(0, 128, 64, u0, v0, u1, v1);
    CHECK(std::fabs(u0 - 2.f / 128.f) < 1e-5f);
    CHECK(std::fabs(v0 - 2.f / 64.f) < 1e-5f);
}

TEST_CASE("animation.spine.skeletonAndAnim") {
    const char *json = R"JSON({
  "skeleton": { "spine": "4.1.00" },
  "bones": [
    { "name": "root" },
    { "name": "body", "parent": "root", "y": 40 },
    { "name": "head", "parent": "body", "y": 30 }
  ],
  "slots": [
    { "name": "body", "bone": "body", "attachment": "body" },
    { "name": "head", "bone": "head", "attachment": "head" }
  ],
  "skins": [
    {
      "name": "default",
      "attachments": {
        "body": { "body": { "width": 40, "height": 50 } },
        "head": { "head": { "width": 24, "height": 24, "y": 5 } }
      }
    }
  ],
  "animations": {
    "idle": {
      "bones": {
        "body": {
          "rotate": [
            { "time": 0, "value": 0 },
            { "time": 0.5, "value": 20 },
            { "time": 1.0, "value": 0 }
          ]
        }
      }
    },
    "nod": {
      "bones": {
        "head": {
          "translate": [
            { "time": 0, "x": 0, "y": 0 },
            { "time": 0.25, "x": 0, "y": -8 }
          ]
        }
      }
    }
  }
})JSON";

    const char *atlasText =
        "hero.png\n"
        "size: 128,64\n"
        "body\n"
        "  xy: 2, 2\n"
        "  size: 40, 50\n"
        "head\n"
        "  xy: 50, 4\n"
        "  size: 24, 24\n";

    auto                              *animMod = Animation::create();
    SpineSkeletonData                 *dataRaw = animMod->newSpineSkeletonDataFromJson(json);
    std::unique_ptr<SpineSkeletonData> data(dataRaw);
    CHECK(dataRaw != nullptr);
    if (!dataRaw) return;
    CHECK_EQ(data->getBoneCount(), 3);
    CHECK_EQ(data->findBone("head"), 2);
    CHECK_EQ(data->getSlotCount(), 2);
    CHECK_EQ(data->getAnimationCount(), 2);
    CHECK(std::fabs(data->getAnimationDuration(data->findAnimation("idle")) - 1.f) < 1e-4f);

    SpineAtlas                 *atlasRaw = animMod->newSpineAtlasFromText(atlasText);
    std::unique_ptr<SpineAtlas> atlas(atlasRaw);
    CHECK(atlasRaw != nullptr);
    if (!atlasRaw) return;

    std::unique_ptr<SpineSkeleton> sk(animMod->newSpineSkeleton(data.get()));
    sk->updateWorldTransform();
    CHECK(std::fabs(sk->getBoneWorldY(1) - 40.f) < 1e-3f);
    CHECK(std::fabs(sk->getBoneWorldY(2) - 70.f) < 1e-3f);

    std::unique_ptr<SpineAnim> player(animMod->newSpineAnim(sk.get()));
    player->setAtlas(atlas.get());
    player->setFlipY(false);  // keep Spine Y-up for assertions
    player->setLoop(true);
    CHECK(player->play("idle"));
    player->setTime(0.5f);
    player->apply();
    // body setup rot 0 + key 20
    CHECK(std::fabs(sk->getBoneLocalRotation(1) - 20.f) < 1e-3f);
    CHECK_EQ(player->getDrawSlotCount(), 2);

    CHECK(player->play("nod"));
    player->setLoop(false);
    player->setTime(0.25f);
    player->apply();
    // head setup y=30 + translate -8
    CHECK(std::fabs(sk->getBoneLocalY(2) - 22.f) < 1e-3f);

    std::vector<eve::graphics::DrawItem2D> items;
    player->setFlipY(true);
    player->setPosition(100, 200);
    player->collectDrawItems(items);
    CHECK_EQ(static_cast<int>(items.size()), 2);
    CHECK_EQ(items[0].layer, 0);

    std::vector<eve::graphics::DrawItem2D> mirrored;
    player->setScale(-1.f, 1.f);
    player->collectDrawItems(mirrored);
    REQUIRE_EQ(static_cast<int>(mirrored.size()), 2);
    // Negative X scale reconstructs the reflected quad with a 180-degree
    // rotation. A V-axis UV compensation keeps attachments upright while
    // preserving the requested left/right mirror.
    CHECK(std::fabs(mirrored[0].u0 - items[0].u0) < 1e-6f);
    CHECK(std::fabs(mirrored[0].u1 - items[0].u1) < 1e-6f);
    CHECK(std::fabs(mirrored[0].v0 - items[0].v1) < 1e-6f);
    CHECK(std::fabs(mirrored[0].v1 - items[0].v0) < 1e-6f);
}

TEST_CASE("animation.spine.modulePump") {
    auto                              *anim    = Animation::create();
    const char                        *json    = R"JSON({
  "bones": [ { "name": "root" } ],
  "slots": [ { "name": "slot", "bone": "root", "attachment": "a" } ],
  "skins": [ { "name": "default", "attachments": {
    "slot": { "a": { "width": 10, "height": 10 } }
  }}],
  "animations": {
    "spin": { "bones": { "root": { "rotate": [
      { "time": 0, "value": 0 }, { "time": 1, "value": 90 }
    ]}}}
  }
})JSON";
    SpineSkeletonData                 *dataRaw = anim->newSpineSkeletonDataFromJson(json);
    std::unique_ptr<SpineSkeletonData> data(dataRaw);
    CHECK(dataRaw != nullptr);
    if (!dataRaw) return;
    std::unique_ptr<SpineSkeleton> sk(anim->newSpineSkeleton(data.get()));
    std::unique_ptr<SpineAnim>     player(anim->newSpineAnim(sk.get()));
    player->play("spin");
    CHECK_EQ(anim->getSpineAnimCount(), 1);
    anim->update(0.5f);
    CHECK(std::fabs(sk->getBoneLocalRotation(0) - 45.f) < 1e-2f);
}

namespace {

[[maybe_unused]] std::string spineAssetsDir() {
    std::string       here  = eve_test_path::sourceBesideTestDir(__FILE__);
    const auto        slash = here.find_last_of("/\\");
    const std::string dir   = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/assets/spine";
}

[[maybe_unused]] bool spineAssetsReady() { return std::filesystem::is_regular_file(spineAssetsDir() + "/.downloaded"); }

struct SpineDemoModel {
    const char *relPath;  // virtual path under the mounted spine assets dir
    const char *atlasRel;
    const char *animation;
    float       cx = 0.f, cy = 0.f;  // target CENTER of the model's bounding box on screen
    float       scale = 1.f;
};

struct SpineDemoPlayer {
    std::unique_ptr<SpineSkeletonData>    data;
    std::unique_ptr<SpineAtlas>           atlas;
    std::unique_ptr<SpineSkeleton>        skeleton;
    std::unique_ptr<SpineAnim>            anim;
    std::vector<eve::graphics::Texture *> pageTextures;
};

/** Centroid of the current pose's draw quads (position at 0,0) — used to
 *  place each model by its own bounds instead of assuming the root anchor. */
[[maybe_unused]] void measurePoseCenter(SpineAnim *anim, float &cx, float &cy) {
    std::vector<eve::graphics::DrawItem2D> items;
    anim->setPosition(0.f, 0.f);
    anim->collectDrawItems(items);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const auto &it : items) {
        minX = std::min(minX, it.x);
        minY = std::min(minY, it.y);
        maxX = std::max(maxX, it.x + it.w);
        maxY = std::max(maxY, it.y + it.h);
    }
    if (items.empty()) {
        cx = 0.f;
        cy = 0.f;
        return;
    }
    cx = (minX + maxX) * 0.5f;
    cy = (minY + maxY) * 0.5f;
}

}  // namespace

/**
 * Renders three real Spine models (downloaded via scripts/download_spine_models.py)
 * into a window for a few seconds, each playing a looped animation.
 * Skips (returns) when the assets have not been downloaded.
 */
}  // namespace test_animation_sprite_spine_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_two_bone_precision)
#include "PrivateTestScope.h"
#include <array>
#include <cmath>
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_two_bone_precision.cpp
namespace test_animation_two_bone_precision {
using namespace eve::animation;
TEST_CASE("animation.pose.twoBoneIkRespectsRotatedScaledParent") {
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
TEST_CASE("animation.pose.twoBoneIkReachesTargetsWithoutIteration") {
    AnimSkeleton skeleton;
    skeleton.addBone("hip");
    skeleton.addBone("knee", 0);
    skeleton.addBone("ankle", 1);
    skeleton.setBindPosition(1, 0, 1, 0);
    skeleton.setBindPosition(2, 0, 1, 0);
    AnimPose pose;
    for (auto target : {std::array{.2f, 1.4f, .1f}, std::array{0.f, 1.99f, 0.f}, std::array{0.f, .01f, 0.f},
                        std::array{-.3f, -1.5f, .2f}}) {
        skeleton.applyBindPose(&pose);
        REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, target[0], target[1], target[2], 1));
        float       error = 0;
        const auto &p     = pose.world(2);
        error             = std::sqrt(std::pow(p.px - target[0], 2.f) + std::pow(p.py - target[1], 2.f) +
                                      std::pow(p.pz - target[2], 2.f));
        CHECK(error < .0003f);
        const auto before = pose.world(1);
        REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, target[0], target[1], target[2], 1));
        const auto after = pose.world(1);
        CHECK(std::abs(before.px - after.px) < .0003f);
        CHECK(std::abs(before.py - after.py) < .0003f);
        CHECK(std::abs(before.pz - after.pz) < .0003f);
    }
}
TEST_CASE("animation.pose.twoBoneIkClampsReachAndKeepsZeroWeightPose") {
    AnimSkeleton skeleton;
    skeleton.addBone("hip");
    skeleton.addBone("knee", 0);
    skeleton.addBone("ankle", 1);
    skeleton.setBindPosition(1, 0, 1, 0);
    skeleton.setBindPosition(2, 0, 1, 0);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, 1, 0, 1, 0));
    CHECK(std::abs(pose.world(2).py - 2) < 1e-6f);
    REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, 0, 3, 0, 1));
    CHECK(std::abs(pose.world(2).py - 2) < 1e-6f);
    skeleton.setBindPosition(1, 0, 0, 0);
    skeleton.applyBindPose(&pose);
    CHECK(!pose.solveTwoBoneIK(&skeleton, 0, 1, 2, 1, 0, 1, 1));
    CHECK(std::abs(pose.world(2).py - 1) < 1e-6f);
}
}  // namespace test_animation_two_bone_precision
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hair_dynamic_bones)
#include "PrivateTestScope.h"
#include "animation/AnimSkeleton.h"
#include "animation/DynamicBoneSolver.h"
#include "animation/HairDynamicBones.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// hair_dynamic_bones.cpp
namespace test_hair_dynamic_bones_unit {
using eve::animation::AnimSkeleton;
using eve::animation::DynamicBoneSolver;

TEST_CASE("animation.HairDynamicBones.setupChainsAndHeadCollider") {
    AnimSkeleton skeleton;
    const int    head = skeleton.addBone("head");
    const int    root = skeleton.addBone("hair_root", head);
    const int    mid  = skeleton.addBone("hair_mid", root);
    const int    tip  = skeleton.addBone("hair_tip", mid);
    REQUIRE(tip >= 0);
    skeleton.setBindPosition(mid, 0.f, -0.2f, 0.f);
    skeleton.setBindPosition(tip, 0.f, -0.2f, 0.f);

    DynamicBoneSolver               solver(&skeleton);
    eve::animation::hair::ChainDesc chain;
    chain.rootBone      = "hair_root";
    chain.tipBone       = "hair_tip";
    chain.stiffness     = 0.12f;
    chain.damping       = 0.18f;
    chain.inertia       = 0.8f;
    chain.endLength     = 0.05f;
    chain.selfCollision = true;

    auto added = eve::animation::hair::addChain(solver, chain);
    REQUIRE(added.ok());
    CHECK_EQ(added.value(), 0);
    CHECK_EQ(solver.getChainCount(), 1);

    auto headCol = eve::animation::hair::addHeadCollider(solver, "head", 0.12f);
    REQUIRE(headCol.ok());
    CHECK(headCol.value() >= 1);

    std::vector<eve::animation::hair::ChainDesc> batch = {chain};
    batch[0].rootBone                                  = "missing_root";
    auto bad                                           = eve::animation::hair::setupChains(solver, batch, false);
    CHECK(!bad.ok());
}
}  // namespace test_hair_dynamic_bones_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_control_rig)
#include <cmath>
#include <limits>
#include "PrivateTestScope.h"
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "common/Value.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_animation_control_rig {
using namespace eve::animation;
namespace {
RigDefinition fkRig() {
    RigDefinition d;
    RigControl    c;
    c.id                   = "rootControl";
    c.initial.px           = 2;
    c.keys                 = {{0, c.initial}, {2, c.initial}};
    c.keys[1].transform.px = 4;
    d.controls.push_back(c);
    RigNode n;
    n.id      = "rootFK";
    n.control = c.id;
    n.bone    = "root";
    d.nodes.push_back(n);
    return d;
}
}  // namespace
TEST_CASE("animation.controlRig.fkTimelineBakeAndOwnedSkeleton") {
    std::unique_ptr<ControlRig> rig;
    {
        AnimSkeleton skeleton;
        skeleton.addBone("root");
        auto compiled = ControlRig::compile(fkRig(), skeleton);
        REQUIRE(compiled.ok());
        rig = std::move(compiled).takeValue();
    }
    AnimPose input(1), output;
    REQUIRE(rig->evaluate(1, input, output).ok());
    CHECK(std::abs(output.local(0).px - 3) < 1e-5f);
    auto baked = rig->bake(30, input);
    REQUIRE(baked.ok());
    AnimPose sampled;
    baked.value()->sample(1, &sampled);
    CHECK(std::abs(sampled.local(0).px - output.local(0).px) < 1e-5f);
    auto matched = rig->matchFK(output);
    REQUIRE(matched.ok());
    CHECK(std::abs(matched.value()[0].px - 3) < 1e-5f);
    auto encoded = encodeControlRig(fkRig());
    REQUIRE(encoded.ok());
    auto decoded = decodeControlRig(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().controls[0].keys.size() == 2);
    encoded.value().set("unknown", true);
    CHECK(!decodeControlRig(encoded.value()).ok());
}
TEST_CASE("animation.controlRig.rejectsCyclesAndPreservesOutputOnFailure") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto d               = fkRig();
    d.controls[0].parent = "rootControl";
    CHECK(!ControlRig::compile(d, skeleton).ok());
    d                = fkRig();
    d.nodes[0].after = {"rootFK"};
    CHECK(!ControlRig::compile(d, skeleton).ok());
    d               = fkRig();
    d.nodes[0].bone = "missing";
    CHECK(!ControlRig::compile(d, skeleton).ok());
    d                          = fkRig();
    d.controls[0].keys[1].time = 0;
    CHECK(!validateRigDefinition(d).ok());
    d                        = fkRig();
    d.controls[0].initial.sz = 1.0000012f;
    CHECK(validateRigDefinition(d).ok());
    d.controls[0].initial.sz = 1.01f;
    CHECK(!validateRigDefinition(d).ok());
    auto compiled = ControlRig::compile(fkRig(), skeleton);
    REQUIRE(compiled.ok());
    AnimPose input(1), output(1);
    output.local(0).px = 99;
    CHECK(!compiled.value()->evaluate(std::numeric_limits<double>::quiet_NaN(), input, output).ok());
    CHECK(output.local(0).px == 99);
    input.local(0).sx = 0;
    CHECK(!compiled.value()->evaluate(0, input, output).ok());
    CHECK(output.local(0).px == 99);
}
TEST_CASE("animation.controlRig.ikAndReparent") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    mid = skeleton.addBone("mid", root), tip = skeleton.addBone("tip", mid);
    skeleton.setBindPosition(mid, 0, 1, 0);
    skeleton.setBindPosition(tip, 0, 1, 0);
    RigDefinition d;
    RigControl    c;
    c.id         = "hand";
    c.initial.px = 1;
    c.initial.py = 1;
    d.controls.push_back(c);
    RigNode n;
    n.id        = "ik";
    n.control   = "hand";
    n.bone      = "root";
    n.middle    = "mid";
    n.tip       = "tip";
    n.operation = RigOperation::TwoBoneIK;
    d.nodes.push_back(n);
    auto compiled = ControlRig::compile(d, skeleton);
    REQUIRE(compiled.ok());
    AnimPose input, output;
    skeleton.applyBindPose(&input);
    REQUIRE(compiled.value()->evaluate(0, input, output).ok());
    CHECK(std::abs(output.world(tip).px - 1) < 1e-4f);
    CHECK(std::abs(output.world(tip).py - 1) < 1e-4f);
    CHECK(!compiled.value()->matchFK(output).ok());
    RigControl parent;
    parent.id         = "space";
    parent.initial.px = 5;
    d.controls.push_back(parent);
    REQUIRE(reparentRigControl(d, "hand", "space").ok());
    CHECK(std::abs(d.controls[0].initial.px + 4) < 1e-5f);
    CHECK(!reparentRigControl(d, "space", "hand").ok());
    CHECK(d.controls[1].parent.empty());
    auto moved = ControlRig::compile(d, skeleton);
    REQUIRE(moved.ok());
    REQUIRE(moved.value()->evaluate(0, input, output).ok());
    CHECK(std::abs(output.world(tip).px - 1) < 1e-4f);
}

TEST_CASE("animation.controlRig.aimComposesOverInputAndFailsAtomically") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("unaffected");
    auto d = fkRig();
    d.controls[0].keys.clear();
    d.controls[0].initial.px = 1;
    d.nodes[0].operation     = RigOperation::Aim;
    auto compiled            = ControlRig::compile(d, skeleton);
    REQUIRE(compiled.ok());
    AnimPose input(2), output;
    input.local(1).py = 7;
    REQUIRE(compiled.value()->evaluate(0, input, output).ok());
    CHECK(std::abs(output.local(0).qy - std::sqrt(0.5f)) < 1e-4f);
    CHECK(output.local(1).py == 7);
    std::vector<TransformTRS> overrides(1);
    CHECK(!compiled.value()->evaluate(0, input, output, overrides).ok());
    CHECK(output.local(1).py == 7);
    CHECK(std::abs(output.local(0).qy - std::sqrt(0.5f)) < 1e-4f);
}

TEST_CASE("animation.controlRig.quaternionShortestPathAndBakeEndpoints") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto d                             = fkRig();
    d.controls[0].keys[1].transform.qw = -1;
    auto compiled                      = ControlRig::compile(d, skeleton);
    REQUIRE(compiled.ok());
    auto sampled = compiled.value()->sampleControls(1);
    REQUIRE(sampled.ok());
    CHECK(std::abs(std::abs(sampled.value()[0].qw) - 1) < 1e-5f);
    AnimPose input(1), direct, bakedPose;
    auto     clip = compiled.value()->bake(29.5, input);
    REQUIRE(clip.ok());
    for (double t : {0.0, 1.0, 2.0}) {
        REQUIRE(compiled.value()->evaluate(t, input, direct).ok());
        clip.value()->sample(static_cast<float>(t), &bakedPose);
        CHECK(std::abs(direct.local(0).px - bakedPose.local(0).px) < 1e-4f);
    }
}
}  // namespace test_animation_control_rig
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_control_rig_limits)
#include <cmath>
#include <limits>
#include "PrivateTestScope.h"
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "common/Value.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_animation_control_rig_limits {
using namespace eve::animation;

TEST_CASE("animation.controlRig.limits.axesExtensionAndInvalidConfiguration") {
    RigControlLimits l;
    l.reference.py        = 2;
    l.translation[0].mode = RigAxisMode::Locked;
    l.translation[1]      = {RigAxisMode::Limited, -0.5f, 0.5f};
    l.rotation[0].mode = l.rotation[1].mode = RigAxisMode::Locked;
    l.rotation[2]                           = {RigAxisMode::Limited, -30, 45};
    l.scale                                 = {RigAxisMode::Limited, 0.8f, 1.2f};
    TransformTRS t;
    t.px = 9;
    t.py = 7;
    t.qz = 0.70710678f;
    t.qw = 0.70710678f;
    t.sx = t.sy = t.sz = 3;
    auto p             = constrainRigControl(l, t);
    REQUIRE(p.ok());
    CHECK(p.value().px == 0);
    CHECK(p.value().py == 2.5f);
    CHECK(std::abs(p.value().qz - std::sin(3.14159265f / 8)) < 1e-5f);
    CHECK(std::abs(p.value().sx - 1.2f) < 1e-5f);
    l.extension = true;
    CHECK(!constrainRigControl(l, t).ok());
    l.translation   = {};
    l.minimumLength = 0.5f;
    l.maximumLength = 1.5f;
    p               = constrainRigControl(l, t);
    REQUIRE(p.ok());
    CHECK(p.value().px == 0);
    CHECK(p.value().py == 3);
    t.py = -8;
    p    = constrainRigControl(l, t);
    REQUIRE(p.ok());
    CHECK(p.value().py == 1);
    l.reference.py = 0;
    CHECK(!constrainRigControl(l, t).ok());
    l.extension   = false;
    l.rotation[1] = {RigAxisMode::Limited, -100, 100};
    CHECK(!constrainRigControl(l, t).ok());
    l.rotation[1] = {};
    t.px          = std::numeric_limits<float>::quiet_NaN();
    CHECK(!constrainRigControl(l, t).ok());
}
TEST_CASE("animation.controlRig.limits.referenceRotationAndInterpolation") {
    RigControlLimits limits;
    limits.reference.qz = limits.reference.qw = std::sqrt(0.5f);
    limits.rotation[0]                        = {RigAxisMode::Limited, -30, 30};
    limits.rotation[1].mode = limits.rotation[2].mode = RigAxisMode::Locked;
    TransformTRS t;
    t.qx = t.qy = t.qz = t.qw = 0.5f;
    auto projected            = constrainRigControl(limits, t);
    REQUIRE(projected.ok());
    CHECK(std::abs(projected.value().qx - std::sin(3.14159265f / 12) * std::sqrt(0.5f)) < 1e-5f);
    auto again = constrainRigControl(limits, projected.value());
    REQUIRE(again.ok());
    CHECK(again.value().qx == projected.value().qx);
    RigDefinition d;
    RigControl    c;
    c.id                   = "root";
    c.limits.rotation[2]   = {RigAxisMode::Limited, -170, 170};
    c.keys                 = {{0, {}}, {2, {}}};
    c.keys[0].transform.qz = std::sin(160 * 3.14159265f / 360);
    c.keys[1].transform.qz = -c.keys[0].transform.qz;
    c.keys[0].transform.qw = c.keys[1].transform.qw = std::cos(160 * 3.14159265f / 360);
    d.controls.push_back(c);
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto rig = ControlRig::compile(d, skeleton);
    REQUIRE(rig.ok());
    auto middle = rig.value()->sampleControls(1);
    REQUIRE(middle.ok());
    const auto &q = middle.value()[0];
    CHECK(std::abs(2 * std::atan2(q.qz, q.qw) * 180 / 3.14159265f) <= 170.001f);
}
TEST_CASE("animation.controlRig.limits.codecMigrationSamplingAndBake") {
    RigDefinition d;
    RigControl    c;
    c.id                    = "root";
    c.limits.translation[0] = {RigAxisMode::Limited, -1, 1};
    c.keys                  = {{0, {}}, {2, {}}};
    c.keys[1].transform.px  = 9;
    d.controls.push_back(c);
    RigNode n;
    n.id      = "fk";
    n.control = n.bone = "root";
    d.nodes.push_back(n);
    auto encoded = encodeControlRig(d);
    REQUIRE(encoded.ok());
    auto decoded = decodeControlRig(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().controls[0].keys[1].transform.px == 1);
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto rig = ControlRig::compile(decoded.value(), skeleton);
    REQUIRE(rig.ok());
    AnimPose input(1), out;
    REQUIRE(rig.value()->evaluate(1, input, out).ok());
    CHECK(std::abs(out.local(0).px - 0.5f) < 1e-5f);
    auto clip = rig.value()->bake(30, input);
    REQUIRE(clip.ok());
    AnimPose baked;
    clip.value()->sample(1, &baked);
    CHECK(std::abs(baked.local(0).px - out.local(0).px) < 1e-5f);
    auto legacy = eve::Value::fromJson(
        R"({"schema":"eve.control-rig","version":1,"duration":2,"controls":[{"id":"old","parent":"","initial":[0,2,0,0,0,0,1,1,1,1],"shapeSize":0.1,"keys":[]}],"nodes":[]})");
    REQUIRE(legacy.ok());
    auto migrated = decodeControlRig(legacy.value());
    REQUIRE(migrated.ok());
    CHECK(migrated.value().controls[0].limits.reference.py == 2);
    CHECK(migrated.value().controls[0].limits.translation[0].mode == RigAxisMode::Free);
    encoded.value().find("controls")->at(0).find("limits")->set("unknown", true);
    CHECK(!decodeControlRig(encoded.value()).ok());
}
TEST_CASE("animation.controlRig.limits.ikCannotBypassLockedJoints") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("mid", 0);
    skeleton.addBone("tip", 1);
    skeleton.setBindPosition(1, 1, 0, 0);
    skeleton.setBindPosition(2, 1, 0, 0);
    RigDefinition d;
    for (int i = 0; i < 3; ++i) {
        RigControl c;
        c.id               = skeleton.getBoneName(i);
        c.parent           = i ? skeleton.getBoneName(i - 1) : "";
        c.initial          = skeleton.bindLocal(i);
        c.limits.reference = c.initial;
        for (auto &axis : c.limits.translation) axis.mode = RigAxisMode::Locked;
        for (auto &axis : c.limits.rotation) axis.mode = RigAxisMode::Locked;
        d.controls.push_back(c);
        RigNode n;
        n.id      = "fk" + std::to_string(i);
        n.control = n.bone = c.id;
        if (i) n.after = {"fk" + std::to_string(i - 1)};
        d.nodes.push_back(n);
    }
    RigControl target;
    target.id         = "target";
    target.initial.py = 1.5f;
    target.initial.px = 0.5f;
    d.controls.push_back(target);
    RigNode ik;
    ik.id        = "ik";
    ik.control   = "target";
    ik.bone      = "root";
    ik.middle    = "mid";
    ik.tip       = "tip";
    ik.operation = RigOperation::TwoBoneIK;
    ik.after     = {"fk2"};
    d.nodes.push_back(ik);
    auto rig = ControlRig::compile(d, skeleton);
    REQUIRE(rig.ok());
    AnimPose input, out;
    skeleton.applyBindPose(&input);
    REQUIRE(rig.value()->evaluate(0, input, out).ok());
    CHECK(std::abs(out.world(2).px - 2) < 1e-5f);
    CHECK(std::abs(out.world(2).py) < 1e-5f);
    RigNode duplicate = d.nodes[0];
    duplicate.id      = "duplicate";
    duplicate.control = "target";
    d.nodes.push_back(duplicate);
    CHECK(!ControlRig::compile(d, skeleton).ok());
    d.nodes.pop_back();
    d.controls[1].parent = "";
    CHECK(!ControlRig::compile(d, skeleton).ok());
}
}  // namespace test_animation_control_rig_limits
#endif
