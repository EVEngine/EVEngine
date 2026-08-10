#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/Tween.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimStateMachine.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"

#include "common/Exception.h"

#include <cmath>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::animation;

TEST_CASE("animation.tween.lerpAbsolute") {
    auto *anim = Animation::create();
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
    auto *anim = Animation::create();
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
    auto *anim = Animation::create();
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
    tw->setFromAngle("rot", float(M_PI * 0.9));   // ~162°
    tw->setToAngle("rot", float(-M_PI * 0.9));    // ~-162°, short path crosses ±π
    tw->start();
    tw->update(0.5f);
    // Midpoint of shortest arc should be near ±π
    float mid = tw->get("rot");
    CHECK(std::fabs(std::fabs(mid) - float(M_PI)) < 0.05f);
}

TEST_CASE("animation.tween.delayPauseResume") {
    auto *anim = Animation::create();
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
    bool threw = false;
    try {
        tw->setEase("notARealEase");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
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
    auto sk = std::make_unique<AnimSkeleton>();
    int root = sk->addBone("root", -1);
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
    std::unique_ptr<AnimClip> clip(new AnimClip("wave"));
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
    std::unique_ptr<AnimClip> a(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip> b(makeLocomotionClip("run", 3.f, 1.f));

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

TEST_CASE("animation.stateMachine.floatTransition") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip> idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip> walk(makeLocomotionClip("walk", 1.f, 1.f));

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
    std::unique_ptr<AnimClip> idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip> jump(makeLocomotionClip("jump", 0.f, 0.5f));
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

TEST_CASE("animation.motionMatching.selectsFasterClip") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip> walk(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip> run(makeLocomotionClip("run", 3.f, 1.f));

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
    auto *anim = Animation::create();
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
    std::unique_ptr<AnimPose> a(new AnimPose(2));
    std::unique_ptr<AnimPose> b(new AnimPose(2));
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
    std::unique_ptr<AnimClip> clip(makeLocomotionClip("once", 1.f, 1.f));
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
    std::unique_ptr<AnimClip> idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip> crouch(makeLocomotionClip("crouch", 0.f, 1.f));
    std::unique_ptr<AnimClip> walk(makeLocomotionClip("walk", 1.f, 1.f));

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
    std::unique_ptr<AnimClip> a(makeLocomotionClip("a", 0.f, 1.f));
    std::unique_ptr<AnimClip> b(makeLocomotionClip("b", 0.f, 1.f));

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
    std::unique_ptr<AnimClip> idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip> walk(makeLocomotionClip("walk", 1.f, 1.f));

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
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    std::unique_ptr<AnimClip> clip(makeLocomotionClip("c", 0.f, 1.f));
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
    std::unique_ptr<AnimClip> idle(makeLocomotionClip("idle", 0.f, 1.f));
    std::unique_ptr<AnimClip> walk(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip> run(makeLocomotionClip("run", 3.f, 1.f));

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
    std::unique_ptr<AnimClip> walk(makeLocomotionClip("walk", 1.f, 1.f));
    std::unique_ptr<AnimClip> run(makeLocomotionClip("run", 3.f, 1.f));

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
    std::unique_ptr<AnimClip> walk(makeLocomotionClip("walk", 1.f, 0.5f));
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

TEST_CASE("animation.eva.roundTrip") {
    std::unique_ptr<AnimSkeleton> sk(makeTwoBoneSkeleton());
    sk->setBindRotation(1, 0.f, 0.f, 0.f, 1.f);
    std::unique_ptr<AnimClip> clip(makeLocomotionClip("walk", 1.5f, 0.8f));
    clip->setSampleRate(12.f);

    const std::string text = AnimImporter::exportEva(sk.get(), clip.get());
    CHECK(text.find("EVA 1") != std::string::npos);

    AnimSkeleton *sk2 = nullptr;
    AnimClip *clip2   = nullptr;
    AnimImporter::importEva(text, &sk2, &clip2);
    std::unique_ptr<AnimSkeleton> skOwned(sk2);
    std::unique_ptr<AnimClip> clipOwned(clip2);

    CHECK_EQ(skOwned->getBoneCount(), 2);
    CHECK(skOwned->getBoneName(1) == "hip");
    CHECK(std::fabs(clipOwned->getDuration() - 0.8f) < 1e-4f);
    CHECK(std::fabs(clipOwned->getSampleRate() - 12.f) < 1e-4f);
    CHECK(clipOwned->getPositionKeyCount(0) >= 2);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    clipOwned->sample(0.4f, pose.get(), skOwned.get());
    CHECK(std::fabs(pose->getLocalPositionZ(0) - 1.5f * 0.4f) < 0.05f);
}

TEST_CASE("animation.importer.invalidThrows") {
    bool threw = false;
    try {
        AnimImporter::importEva("not an eva file\n", nullptr, nullptr);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    AnimSkeleton *sk = nullptr;
    AnimClip *clip   = nullptr;
    try {
        AnimImporter::importEva("EVA 99\n", &sk, &clip);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    CHECK(sk == nullptr);
    CHECK(clip == nullptr);
}
