#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimStateMachine.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"

#include "common/Exception.h"

#include <cmath>
#include <fstream>
#include <memory>
#include <string>

using namespace eve::animation;

namespace {

std::string assetPath(const char *filename) {
    std::string here = __FILE__;
    auto slash       = here.find_last_of("/\\");
    std::string dir  = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/assets/mixamo/" + filename;
}

bool fileExists(const std::string &path) {
    std::ifstream in(path);
    return static_cast<bool>(in);
}

struct MixamoClips {
    std::unique_ptr<AnimSkeleton> skeleton;
    std::unique_ptr<AnimClip>     idle;
    std::unique_ptr<AnimClip>     run;
    std::unique_ptr<AnimClip>     jump;
};

MixamoClips loadMixamoEva() {
    MixamoClips out;
    AnimSkeleton *sk = nullptr;
    AnimClip *idle   = nullptr;
    AnimImporter::importEvaFile(assetPath("Idle.eva"), &sk, &idle);
    out.skeleton.reset(sk);
    out.idle.reset(idle);

    AnimSkeleton *sk2 = nullptr;
    AnimClip *run     = nullptr;
    AnimImporter::importEvaFile(assetPath("SlowRun.eva"), &sk2, &run);
    delete sk2;
    out.run.reset(run);
    // Mixamo Slow Run is in-place; bake forward root motion on hips for MM.
    const int hips = out.skeleton->findBone("mixamorig:Hips");
    REQUIRE(hips >= 0);
    // ~300 cm/s forward in Mixamo units.
    out.run->applyPlanarRootMotion(hips, 0.f, 300.f);

    AnimSkeleton *sk3 = nullptr;
    AnimClip *jump    = nullptr;
    AnimImporter::importEvaFile(assetPath("Jumping.eva"), &sk3, &jump);
    delete sk3;
    out.jump.reset(jump);
    out.jump->setLoop(false);
    return out;
}

}  // namespace

TEST_CASE("animation.mixamo.evaFixturesPresent") {
    CHECK(fileExists(assetPath("Idle.eva")));
    CHECK(fileExists(assetPath("SlowRun.eva")));
    CHECK(fileExists(assetPath("Jumping.eva")));
}

TEST_CASE("animation.mixamo.importSkeletonAndSample") {
    auto pack = loadMixamoEva();
    CHECK(pack.skeleton->getBoneCount() == 70);
    CHECK(pack.skeleton->findBone("mixamorig:Hips") == 1);
    CHECK(pack.skeleton->findBone("mixamorig:LeftFoot") >= 0);
    CHECK(pack.idle->getDuration() > 1.f);
    CHECK(pack.run->getDuration() > 0.2f);
    CHECK(pack.jump->getDuration() > 0.5f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    pack.idle->sample(0.5f, pose.get(), pack.skeleton.get());
    pose->computeWorld(pack.skeleton.get());
    const int hips = pack.skeleton->findBone("mixamorig:Hips");
    // Character stands roughly 1m tall in Mixamo cm units.
    CHECK(pose->getWorldPositionY(hips) > 50.f);
    CHECK(pose->getWorldPositionY(hips) < 150.f);
}

TEST_CASE("animation.mixamo.stateMachine.idleRunJump") {
    auto pack = loadMixamoEva();
    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(pack.skeleton.get()));
    sm->addState("Idle", pack.idle.get());
    sm->addState("Run", pack.run.get());
    sm->addState("Jump", pack.jump.get());
    sm->setEntry("Idle");

    int toRun = sm->addTransition("Idle", "Run", 0.12f);
    sm->addFloatCondition(toRun, "speed", ">", 50.f);
    int toIdle = sm->addTransition("Run", "Idle", 0.12f);
    sm->addFloatCondition(toIdle, "speed", "<", 10.f);

    int toJump = sm->addTransition("Idle", "Jump", 0.08f);
    sm->addTriggerCondition(toJump, "jump");
    int jumpToIdle = sm->addTransition("Jump", "Idle", 0.1f);
    sm->setExitTime(jumpToIdle, 0.85f);

    // Idle → Run
    sm->setFloat("speed", 200.f);
    for (int i = 0; i < 40; ++i) sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Run");

    // Pose should be sampling (hips exist)
    AnimPose *pose = sm->getPose();
    CHECK(pose->getBoneCount() == 70);

    // Run → Idle
    sm->setFloat("speed", 0.f);
    for (int i = 0; i < 40; ++i) sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Idle");

    // Idle → Jump via trigger, then back after exit time
    sm->setTrigger("jump");
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "Jump");
    for (int i = 0; i < 80; ++i) sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Idle");
}

TEST_CASE("animation.mixamo.motionMatching.prefersRunWhenFast") {
    auto pack = loadMixamoEva();
    const int hips = pack.skeleton->findBone("mixamorig:Hips");
    const int lFoot = pack.skeleton->findBone("mixamorig:LeftFoot");
    const int rFoot = pack.skeleton->findBone("mixamorig:RightFoot");
    REQUIRE(hips >= 0);

    std::unique_ptr<MotionDatabase> db(new MotionDatabase(pack.skeleton.get()));
    db->setRootBone(hips);
    if (lFoot >= 0) db->addFeatureBone(lFoot);
    if (rFoot >= 0) db->addFeatureBone(rFoot);
    db->addFeatureBone(hips);
    db->addClip(pack.idle.get());
    db->addClip(pack.run.get());
    db->bake();
    CHECK(db->getFrameCount() > 10);

    std::unique_ptr<MotionMatcher> mm(new MotionMatcher(pack.skeleton.get(), db.get()));
    mm->setSearchInterval(0.08f);
    mm->setBlendTime(0.08f);
    mm->setIgnoreRadius(0);
    mm->setTrajectoryWeight(1.5f);
    mm->setVelocityWeight(2.f);
    mm->setPoseWeight(0.5f);

    // High desired forward speed → SlowRun (clip index 1)
    mm->setDesiredVelocity(0.f, 300.f);
    mm->setDesiredYaw(0.f);
    mm->search();
    CHECK(mm->getMatchedFrame() >= 0);
    CHECK_EQ(mm->getMatchedClipIndex(), 1);

    for (int i = 0; i < 10; ++i) mm->update(0.05f);
    CHECK(mm->getPose()->getBoneCount() == 70);

    // Near-zero desired speed → Idle (clip index 0)
    mm->setDesiredVelocity(0.f, 0.f);
    mm->setIgnoreRadius(0);
    mm->search();
    CHECK_EQ(mm->getMatchedClipIndex(), 0);
}

TEST_CASE("animation.mixamo.playerCrossFadeIdleToRun") {
    auto pack = loadMixamoEva();
    std::unique_ptr<AnimPlayer> player(new AnimPlayer(pack.skeleton.get()));
    player->play(pack.idle.get());
    player->update(0.2f);
    CHECK(player->isPlaying());
    player->crossFade(pack.run.get(), 0.15f);
    for (int i = 0; i < 10; ++i) player->update(0.03f);
    CHECK(player->getClip() == pack.run.get());
    player->getPose()->computeWorld(pack.skeleton.get());
    const int hips = pack.skeleton->findBone("mixamorig:Hips");
    CHECK(std::isfinite(player->getPose()->getWorldPositionY(hips)));
}
