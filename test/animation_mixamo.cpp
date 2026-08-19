#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimStateMachine.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"

#include "common/Exception.h"
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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"

// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

#include <SDL2/SDL.h>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

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

TEST_CASE("animation.mixamo.stateMachine.runJumpIdleCycle") {
    auto pack = loadMixamoEva();
    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(pack.skeleton.get()));
    sm->addState("Idle", pack.idle.get());
    sm->addState("Run", pack.run.get());
    sm->addState("Jump", pack.jump.get());
    sm->setEntry("Idle");

    int idleToRun = sm->addTransition("Idle", "Run", 0.1f);
    sm->addFloatCondition(idleToRun, "speed", ">", 50.f);
    int runToIdle = sm->addTransition("Run", "Idle", 0.1f);
    sm->addFloatCondition(runToIdle, "speed", "<", 10.f);

    // Jump can interrupt Idle or Run
    int idleToJump = sm->addTransition("Idle", "Jump", 0.05f);
    sm->addTriggerCondition(idleToJump, "jump");
    int runToJump = sm->addTransition("Run", "Jump", 0.05f);
    sm->addTriggerCondition(runToJump, "jump");
    int jumpToIdle = sm->addTransition("Jump", "Idle", 0.08f);
    sm->setExitTime(jumpToIdle, 0.8f);

    sm->setFloat("speed", 180.f);
    for (int i = 0; i < 30; ++i) sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Run");

    sm->setTrigger("jump");
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "Jump");

    // Wait until Jump exits (exitTime 0.8 * 1.9s ≈ 1.5s), then observe Idle land.
    bool sawIdle = false;
    for (int i = 0; i < 80; ++i) {
        sm->update(0.05f);
        if (sm->getCurrentState() == "Idle") {
            sawIdle = true;
            break;
        }
    }
    CHECK(sawIdle);

    // Speed still high → resume Run after landing.
    for (int i = 0; i < 30; ++i) sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Run");
}

TEST_CASE("animation.mixamo.stateMachine.boolArmedGate") {
    auto pack = loadMixamoEva();
    std::unique_ptr<AnimStateMachine> sm(new AnimStateMachine(pack.skeleton.get()));
    sm->addState("Idle", pack.idle.get());
    sm->addState("Run", pack.run.get());
    sm->setEntry("Idle");

    int t = sm->addTransition("Idle", "Run", 0.f);
    sm->addFloatCondition(t, "speed", ">", 50.f);
    sm->addBoolCondition(t, "armed", true);

    sm->setFloat("speed", 200.f);
    sm->setBool("armed", false);
    sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Idle");

    sm->setBool("armed", true);
    sm->update(0.05f);
    CHECK(sm->getCurrentState() == "Run");
}

TEST_CASE("animation.mixamo.motionMatching.stableWithIgnoreRadius") {
    auto pack = loadMixamoEva();
    const int hips = pack.skeleton->findBone("mixamorig:Hips");
    REQUIRE(hips >= 0);

    std::unique_ptr<MotionDatabase> db(new MotionDatabase(pack.skeleton.get()));
    db->setRootBoneByName("mixamorig:Hips");
    db->addFeatureBoneByName("mixamorig:LeftFoot");
    db->addFeatureBoneByName("mixamorig:RightFoot");
    db->addClip(pack.idle.get());
    db->addClip(pack.run.get());
    db->bake();

    std::unique_ptr<MotionMatcher> mm(new MotionMatcher(pack.skeleton.get(), db.get()));
    mm->setSearchInterval(0.1f);
    mm->setBlendTime(0.1f);
    mm->setIgnoreRadius(3);
    mm->setVelocityWeight(2.f);
    mm->setTrajectoryWeight(1.5f);
    mm->setDesiredVelocity(0.f, 300.f);
    mm->search();
    CHECK_EQ(mm->getMatchedClipIndex(), 1);

    int switches = 0;
    int lastClip = mm->getMatchedClipIndex();
    for (int i = 0; i < 30; ++i) {
        mm->update(0.05f);
        const int clip = mm->getMatchedClipIndex();
        if (clip != lastClip) {
            ++switches;
            lastClip = clip;
        }
    }
    // Holding a constant high desired speed should stay on run (few/no flips).
    CHECK(switches <= 2);
    CHECK_EQ(mm->getMatchedClipIndex(), 1);
}

TEST_CASE("animation.mixamo.evaExportRoundTripPreservesHips") {
    auto pack = loadMixamoEva();
    const std::string text = AnimImporter::exportEva(pack.skeleton.get(), pack.idle.get());

    AnimSkeleton *sk = nullptr;
    AnimClip *clip   = nullptr;
    AnimImporter::importEva(text, &sk, &clip);
    std::unique_ptr<AnimSkeleton> skOwned(sk);
    std::unique_ptr<AnimClip> clipOwned(clip);

    CHECK_EQ(skOwned->getBoneCount(), 70);
    CHECK_EQ(skOwned->findBone("mixamorig:Hips"), 1);
    CHECK(std::fabs(clipOwned->getDuration() - pack.idle->getDuration()) < 1e-3f);

    std::unique_ptr<AnimPose> a(new AnimPose());
    std::unique_ptr<AnimPose> b(new AnimPose());
    pack.idle->sample(0.35f, a.get(), pack.skeleton.get());
    clipOwned->sample(0.35f, b.get(), skOwned.get());
    const int hips = 1;
    CHECK(std::fabs(a->getLocalPositionY(hips) - b->getLocalPositionY(hips)) < 0.05f);
}

TEST_CASE("animation.mixamo.moduleEvaFactories") {
    auto *anim = Animation::create();
    std::unique_ptr<AnimSkeleton> sk(anim->newSkeletonFromEvaFile(assetPath("Idle.eva")));
    std::unique_ptr<AnimClip> idle(anim->newClipFromEvaFile(assetPath("Idle.eva")));
    std::unique_ptr<AnimClip> run(anim->newClipFromEvaFile(assetPath("SlowRun.eva")));
    CHECK(sk->getBoneCount() == 70);
    CHECK(idle->getDuration() > 1.f);

    const int hips = sk->findBone("mixamorig:Hips");
    run->applyPlanarRootMotion(hips, 0.f, 300.f);

    std::unique_ptr<AnimStateMachine> sm(anim->newStateMachine(sk.get()));
    sm->addState("Idle", idle.get());
    sm->addState("Run", run.get());
    sm->setEntry("Idle");
    int t = sm->addTransition("Idle", "Run", 0.f);
    sm->addFloatCondition(t, "speed", ">", 1.f);
    sm->setFloat("speed", 10.f);
    sm->update(0.016f);
    CHECK(sm->getCurrentState() == "Run");
}

/** Project Mixamo world cm → screen; Y-up world, Y-down screen. */
static void projectBone(float wx, float wy, float wz, float rootX, float rootZ, float scale,
                        float originX, float originY, float &sx, float &sy) {
    sx = originX + (wx - rootX) * scale;
    sy = originY - wy * scale;
    (void)wz;
    (void)rootZ;
}

static void drawBoneSegment(eve::graphics::Graphics *gfx, float x0, float y0, float x1, float y1,
                            const Color &color) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float steps = std::max(1.f, len / 3.f);
    for (float t = 0.f; t <= 1.f; t += 1.f / steps) {
        const float x = x0 + dx * t;
        const float y = y0 + dy * t;
        gfx->drawSolidRect(x - 1.5f, y - 1.5f, 3.f, 3.f, color);
    }
}

TEST_CASE("animation.mixamo.skeletonIdleRunJumpPreview") {
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

    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 480;
    s.height = 640;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const int hips = pack.skeleton->findBone("mixamorig:Hips");
    REQUIRE(hips >= 0);
    const float scale = 2.2f;
    const float originX = float(gfx->getWidth()) * 0.5f;
    const float originY = float(gfx->getHeight()) - 40.f;

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);

    // Phase timeline (~2.4s): Idle → Run → Idle → Jump → Idle
    enum Phase { IdleA, RunA, IdleB, JumpA, IdleC };
    Phase phase = IdleA;
    int phaseFrames = 0;
    int drawnBones = 0;

    for (int frame = 0; frame < 150; ++frame) {
        ++phaseFrames;
        if (phase == IdleA && phaseFrames > 30) {
            sm->setFloat("speed", 200.f);
            phase = RunA;
            phaseFrames = 0;
        } else if (phase == RunA && phaseFrames > 40) {
            sm->setFloat("speed", 0.f);
            phase = IdleB;
            phaseFrames = 0;
        } else if (phase == IdleB && phaseFrames > 20) {
            sm->setTrigger("jump");
            phase = JumpA;
            phaseFrames = 0;
        } else if (phase == JumpA && phaseFrames > 35) {
            phase = IdleC;
            phaseFrames = 0;
        }

        sm->update(0.016f);
        AnimPose *pose = sm->getPose();
        pose->computeWorld(pack.skeleton.get());

        const float rootX = pose->getWorldPositionX(hips);
        const float rootZ = pose->getWorldPositionZ(hips);

        gfx->clearScreen();
        // Ground line
        gfx->drawSolidRect(40.f, originY, float(gfx->getWidth()) - 80.f, 2.f,
                           Color(0.25f, 0.28f, 0.35f, 1.f));

        drawnBones = 0;
        const int n = pack.skeleton->getBoneCount();
        for (int b = 0; b < n; ++b) {
            const int parent = pack.skeleton->getParent(b);
            float sx, sy;
            projectBone(pose->getWorldPositionX(b), pose->getWorldPositionY(b),
                        pose->getWorldPositionZ(b), rootX, rootZ, scale, originX, originY, sx, sy);

            if (parent >= 0) {
                float px, py;
                projectBone(pose->getWorldPositionX(parent), pose->getWorldPositionY(parent),
                            pose->getWorldPositionZ(parent), rootX, rootZ, scale, originX, originY,
                            px, py);
                Color boneCol(0.55f, 0.85f, 1.f, 1.f);
                if (phase == RunA) boneCol = Color(0.4f, 1.f, 0.55f, 1.f);
                if (phase == JumpA) boneCol = Color(1.f, 0.75f, 0.3f, 1.f);
                drawBoneSegment(gfx, px, py, sx, sy, boneCol);
            }
            gfx->drawSolidRect(sx - 2.f, sy - 2.f, 4.f, 4.f, Color(1.f, 1.f, 1.f, 1.f));
            ++drawnBones;
        }

        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_EQ(drawnBones, 70);
    CHECK(sm->getCurrentState() == "Idle");
    win->close();
}
