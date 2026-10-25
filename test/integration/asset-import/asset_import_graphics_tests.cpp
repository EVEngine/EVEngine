// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "asset-files",
  "cases": {
    "concurrency": [
      "thread.resource executor bounds concurrent decoding",
      "resource.requestAsyncThenWaitFor",
      "resource.getJoinsInFlightRequest",
      "resource.asyncLoadFailureSurfacesInWaitFor",
      "SceneLoader.prewarmAsync.reusesDecodedScene"
    ],
    "platform-file": [
      "animation.mixamo.anim.txtFixturesPresent",
      "font.newFontData.ttf",
      "font.newFontDataFromFile.cached",
      "font.newFontData.invalidSize",
      "model3d.newModelDataFromFile.cached",
      "model3d.newModelData.rock1Fbx",
      "model3d.newModelDataFromFile.sofaObjMtl",
      "model3d.materialApi.objMtl",
      "model3d.materialApi.gltf",
      "spritestack.render.pure2DStack",
      "spritestack.render.batchUses2DStacks"
    ],
    "render-window": [
      "animation.mixamo.skeletonIdleRunJumpPreview",
      "map.render.orthoAndIsoPreview",
      "model3d.render.rock1Fbx",
      "model3d.render.rock1ToonShader",
      "model3d.render.sofaObjMtl",
      "model3d.buildRenderable.objMtl",
      "model3d.render.gltfEmbeddedUvOrientation",
      "model3d.render.cesiumManGltf",
      "particles.render.fireAndSmokePreview",
      "SceneLoader.load.buildsGameObjectTreeWithRenderables",
      "SceneLoader.load.pbrMaterialAndLightsAndCamera",
      "SceneLoader.load.pbrTextureCacheReusesTexture",
      "SceneLoader.load.gltfCesiumManPbrAndAnimation",
      "SceneLoader.loadAsync.mountsOnPoll",
      "SceneLoader.loadAsync.precodesTextures"
    ],
    "physics-backend": [
      "particles.groundCulling.filtersVisitorAndControlsPlayback"
    ]
  },
  "module": "filesystem",
  "modules": [
    "animation",
    "editor",
    "filesystem",
    "font",
    "map",
    "model3d",
    "particles",
    "sceneloader",
    "sound",
    "spritestack"
  ],
  "origins": [
    "ModelPrefetch.cpp",
    "PreparedFile.cpp",
    "animation_mixamo.cpp",
    "editor_disk_asset_catalog_v2.cpp",
    "font.cpp",
    "map.cpp",
    "model3d.cpp",
    "particles.cpp",
    "resource.cpp",
    "resource_decoder_lifetime.cpp",
    "resource_format_model.cpp",
    "resource_format_sound.cpp",
    "sceneloader.cpp",
    "spritestack.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ModelPrefetch)
#include <filesystem>
#include <future>
#include <thread>
#include <vector>
#include "PrivateTestScope.h"
#include "common/AsyncWork.h"
#include "common/Capability.h"
#include "common/Resource.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ModelPrefetch.cpp
namespace test_ModelPrefetch {
namespace {
struct ExecutorScope {
    eve::caps::IAsyncWorkExecutor *previous = eve::cap::query<eve::caps::IAsyncWorkExecutor>();
    explicit ExecutorScope(eve::caps::IAsyncWorkExecutor *current) {
        if (previous) eve::cap::revoke(previous);
        if (current) eve::cap::provide(current);
    }
    ~ExecutorScope() { eve::cap::provide(previous); }
};
class Worker final : public eve::caps::IAsyncWorkExecutor {
public:
    std::vector<std::future<void>> jobs;
    std::thread::id                workerId;
    eve::Result<void>              submit(std::function<void()> work) override {
        jobs.push_back(std::async(std::launch::async, [this, work = std::move(work)] {
            workerId = std::this_thread::get_id();
            work();
        }));
        return eve::Result<void>::success();
    }
    void join() {
        for (auto &job : jobs)
            if (job.valid()) job.get();
    }
    ~Worker() { join(); }
};
}  // namespace

TEST_CASE("model3d.prefetch rejects missing executor and empty paths") {
    ExecutorScope scope(nullptr);
    auto         *models = eve::model3d::Model3D::create();
    auto          empty  = models->requestModelData("");
    REQUIRE(!empty.ok());
    auto unsupported = models->requestModelData("prefetch.obj");
    REQUIRE(!unsupported.ok());
}

TEST_CASE("model3d.prefetch decodes on worker and joins the canonical cache") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("eve_model_prefetch_contract", true));
    // Other isolated tests create the shared application-data parent concurrently.
    // Prepare this fixture with an idempotent OS operation before mounting it.
    const std::string           saveDirectory = fs->getSaveDirectory();
    const std::filesystem::path savePath(std::u8string(saveDirectory.begin(), saveDirectory.end()));
    std::error_code             directoryError;
    std::filesystem::create_directories(savePath, directoryError);
    REQUIRE(!directoryError);
    REQUIRE(fs->setupWriteDirectory());
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    fs->write("prefetch.obj", obj.data(), obj.size());
    Worker        worker;
    ExecutorScope scope(&worker);
    auto         *models = eve::model3d::Model3D::create();
    REQUIRE(models->requestModelData("prefetch.obj").ok());
    REQUIRE(models->requestModelData("prefetch.obj").ok());
    auto *first = models->newModelDataFromFile("prefetch.obj");
    REQUIRE(first != nullptr);
    REQUIRE(first->getMeshCount() == 1);
    REQUIRE(models->newModelDataFromFile("prefetch.obj") == first);
    worker.join();
    REQUIRE(worker.jobs.size() == 1);
    REQUIRE(worker.workerId != std::this_thread::get_id());
    eve::model3d::ModelLoadOptions options;
    options.joinIdenticalVertices = false;
    options.improveCacheLocality  = false;
    REQUIRE(models->requestModelData("prefetch.obj", options).ok());
    REQUIRE(models->requestModelData("prefetch.obj", options).ok());
    auto *explicitOptions = models->newModelDataFromFile("prefetch.obj", options);
    REQUIRE(explicitOptions != first);
    REQUIRE(explicitOptions->getMeshCount() == 1);
    REQUIRE(models->newModelDataFromFile("prefetch.obj", options) == explicitOptions);
    worker.join();
    REQUIRE(worker.jobs.size() == 2);
    REQUIRE(models->requestModelData("missing-prefetch-model.glb").ok());
    bool reportedFailure = false;
    try {
        models->newModelDataFromFile("missing-prefetch-model.glb");
    } catch (const std::exception &) {
        reportedFailure = true;
    }
    worker.join();
    REQUIRE(reportedFailure);
    REQUIRE(eve::ResourceManager::getInstance().pendingCount() == 0);
}
}  // namespace test_ModelPrefetch
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_PreparedFile)
#include "PrivateTestScope.h"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include "common/AsyncWork.h"
#include "common/Capability.h"
#include "common/Resource.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "filesystem/PreparedFile.h"
#include "thread/Thread.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// PreparedFile.cpp
namespace test_PreparedFile {
TEST_CASE("filesystem.prepared snapshots deduplicate and survive cache eviction") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("eve_prepared_file_contract", true));
    REQUIRE(fs->setupWriteDirectory());
    auto *threads = eve::thread::Thread::create();
    REQUIRE(threads != nullptr);
    fs->write("snapshot.bin", "old", 3);
    REQUIRE(eve::filesystem::requestPreparedFile("snapshot.bin").ok());
    auto first  = eve::filesystem::readPreparedFile("snapshot.bin", 3);
    auto second = eve::filesystem::readPreparedFile("snapshot.bin", 3);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value() == second.value());
    REQUIRE(!eve::filesystem::readPreparedFile("snapshot.bin", 2).ok());
    eve::ResourceManager::getInstance().unloadPath("snapshot.bin");
    fs->write("snapshot.bin", "new", 3);
    auto replacement = eve::filesystem::readPreparedFile("snapshot.bin", 3);
    REQUIRE(replacement.ok());
    REQUIRE(replacement.value() != first.value());
    REQUIRE(std::string(static_cast<const char *>(first.value()->getData()), 3) == "old");
    REQUIRE(std::string(static_cast<const char *>(replacement.value()->getData()), 3) == "new");
    REQUIRE(eve::filesystem::requestPreparedFile("missing-snapshot.bin").ok());
    REQUIRE(!eve::filesystem::readPreparedFile("missing-snapshot.bin", 1024).ok());
}

TEST_CASE("thread.resource executor bounds concurrent decoding") {
    auto *threads = eve::thread::Thread::create();
    REQUIRE(threads != nullptr);
    struct State {
        std::atomic<int>   active = 0, maximum = 0, completed = 0;
        std::promise<void> done;
    };
    auto  state    = std::make_shared<State>();
    auto  done     = state->done.get_future();
    auto *executor = eve::cap::query<eve::caps::IAsyncWorkExecutor>();
    REQUIRE(executor != nullptr);
    for (int i = 0; i < 12; ++i) {
        REQUIRE(executor
                    ->submit([state] {
                        const int n        = ++state->active;
                        int       previous = state->maximum;
                        while (previous < n && !state->maximum.compare_exchange_weak(previous, n)) {
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        --state->active;
                        if (++state->completed == 12) state->done.set_value();
                    })
                    .ok());
    }
    REQUIRE(done.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(state->maximum.load() <= 8);
    REQUIRE(state->completed.load() == 12);
}

TEST_CASE("filesystem.prepared rejects missing executor without submitting") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    auto *executor = eve::cap::query<eve::caps::IAsyncWorkExecutor>();
    struct Restore {
        eve::caps::IAsyncWorkExecutor *executor;
        ~Restore() {
            if (executor) eve::cap::provide<eve::caps::IAsyncWorkExecutor>(executor);
        }
    } restore{executor};
    if (executor) eve::cap::revoke<eve::caps::IAsyncWorkExecutor>(executor);
    REQUIRE(!eve::filesystem::requestPreparedFile("snapshot.bin").ok());
}
}  // namespace test_PreparedFile
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_mixamo)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimStateMachine.h"
#include "animation/Animation.h"
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
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_mixamo.cpp
namespace test_animation_mixamo {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


using namespace eve::animation;

namespace {

std::string assetPath(const char *filename) {
    return eve_test_path::pathBesideTestDir(__FILE__, std::string("assets/mixamo/") + filename);
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

MixamoClips loadMixamoAnimationFixtures() {
    MixamoClips   out;
    AnimSkeleton *sk   = nullptr;
    AnimClip     *idle = nullptr;
    AnimImporter::importAnimationFixtureTextFile(assetPath("Idle.anim.txt"), &sk, &idle);
    out.skeleton.reset(sk);
    out.idle.reset(idle);

    AnimSkeleton *sk2 = nullptr;
    AnimClip     *run = nullptr;
    AnimImporter::importAnimationFixtureTextFile(assetPath("SlowRun.anim.txt"), &sk2, &run);
    delete sk2;
    out.run.reset(run);
    // Mixamo Slow Run is in-place; bake forward root motion on hips for MM.
    const int hips = out.skeleton->findBone("mixamorig:Hips");
    REQUIRE(hips >= 0);
    // ~300 cm/s forward in Mixamo units.
    out.run->applyPlanarRootMotion(hips, 0.f, 300.f);

    AnimSkeleton *sk3  = nullptr;
    AnimClip     *jump = nullptr;
    AnimImporter::importAnimationFixtureTextFile(assetPath("Jumping.anim.txt"), &sk3, &jump);
    delete sk3;
    out.jump.reset(jump);
    out.jump->setLoop(false);
    return out;
}

}  // namespace

TEST_CASE("animation.mixamo.anim.txtFixturesPresent") {
    CHECK(fileExists(assetPath("Idle.anim.txt")));
    CHECK(fileExists(assetPath("SlowRun.anim.txt")));
    CHECK(fileExists(assetPath("Jumping.anim.txt")));
}

TEST_CASE("animation.mixamo.importSkeletonAndSample") {
    auto pack = loadMixamoAnimationFixtures();
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
    auto                              pack = loadMixamoAnimationFixtures();
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
    auto      pack  = loadMixamoAnimationFixtures();
    const int hips  = pack.skeleton->findBone("mixamorig:Hips");
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
    auto                        pack = loadMixamoAnimationFixtures();
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
    auto                              pack = loadMixamoAnimationFixtures();
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
    auto                              pack = loadMixamoAnimationFixtures();
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
    auto      pack = loadMixamoAnimationFixtures();
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

TEST_CASE("animation.mixamo.anim.txtExportRoundTripPreservesHips") {
    auto              pack = loadMixamoAnimationFixtures();
    const std::string text = AnimImporter::exportAnimationFixtureText(pack.skeleton.get(), pack.idle.get());

    AnimSkeleton *sk   = nullptr;
    AnimClip     *clip = nullptr;
    AnimImporter::importAnimationFixtureText(text, &sk, &clip);
    std::unique_ptr<AnimSkeleton> skOwned(sk);
    std::unique_ptr<AnimClip>     clipOwned(clip);

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
    auto                         *anim = Animation::create();
    std::unique_ptr<AnimSkeleton> sk(anim->newSkeletonFromAnimationFixtureText(assetPath("Idle.anim.txt")));
    std::unique_ptr<AnimClip>     idle(anim->newClipFromAnimationFixtureText(assetPath("Idle.anim.txt")));
    std::unique_ptr<AnimClip>     run(anim->newClipFromAnimationFixtureText(assetPath("SlowRun.anim.txt")));
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
static void projectBone(float wx, float wy, float wz, float rootX, float rootZ, float scale, float originX,
                        float originY, float &sx, float &sy) {
    sx = originX + (wx - rootX) * scale;
    sy = originY - wy * scale;
    (void)wz;
    (void)rootZ;
}

static void drawBoneSegment(eve::graphics::Graphics *gfx, float x0, float y0, float x1, float y1, const Color &color) {
    const float dx  = x1 - x0;
    const float dy  = y1 - y0;
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
    auto                              pack = loadMixamoAnimationFixtures();
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
    s.width    = 480;
    s.height   = 640;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const int hips = pack.skeleton->findBone("mixamorig:Hips");
    REQUIRE(hips >= 0);
    const float scale   = 2.2f;
    const float originX = float(gfx->getWidth()) * 0.5f;
    const float originY = float(gfx->getHeight()) - 40.f;

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);

    // Allow the imported jump clip to reach its exit time and finish blending.
    const int previewFrames = 100 + static_cast<int>(std::ceil((pack.jump->getDuration() + 0.2f) / 0.016f));
    // Phase timeline: Idle → Run → Idle → Jump → Idle
    enum Phase { IdleA, RunA, IdleB, JumpA, IdleC };
    Phase phase       = IdleA;
    int   phaseFrames = 0;
    int   drawnBones  = 0;

    for (int frame = 0; frame < previewFrames; ++frame) {
        ++phaseFrames;
        if (phase == IdleA && phaseFrames > 30) {
            sm->setFloat("speed", 200.f);
            phase       = RunA;
            phaseFrames = 0;
        } else if (phase == RunA && phaseFrames > 40) {
            sm->setFloat("speed", 0.f);
            phase       = IdleB;
            phaseFrames = 0;
        } else if (phase == IdleB && phaseFrames > 20) {
            sm->setTrigger("jump");
            phase       = JumpA;
            phaseFrames = 0;
        } else if (phase == JumpA && phaseFrames > 35) {
            phase       = IdleC;
            phaseFrames = 0;
        }

        sm->update(0.016f);
        AnimPose *pose = sm->getPose();
        pose->computeWorld(pack.skeleton.get());

        const float rootX = pose->getWorldPositionX(hips);
        const float rootZ = pose->getWorldPositionZ(hips);

        gfx->clearScreen();
        // Ground line
        gfx->drawSolidRect(40.f, originY, float(gfx->getWidth()) - 80.f, 2.f, Color(0.25f, 0.28f, 0.35f, 1.f));

        drawnBones  = 0;
        const int n = pack.skeleton->getBoneCount();
        for (int b = 0; b < n; ++b) {
            const int parent = pack.skeleton->getParent(b);
            float     sx, sy;
            projectBone(pose->getWorldPositionX(b), pose->getWorldPositionY(b), pose->getWorldPositionZ(b), rootX,
                        rootZ, scale, originX, originY, sx, sy);

            if (parent >= 0) {
                float px, py;
                projectBone(pose->getWorldPositionX(parent), pose->getWorldPositionY(parent),
                            pose->getWorldPositionZ(parent), rootX, rootZ, scale, originX, originY, px, py);
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
}  // namespace test_animation_mixamo
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_disk_asset_catalog_v2)
#include "PrivateTestScope.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "editor/EditorDiskAssetCatalog.h"
#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorValueJson.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_disk_asset_catalog_v2.cpp
namespace test_editor_disk_asset_catalog_v2 {
// editor_disk_asset_catalog_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.disk_asset_sidecar_preserves_guid_across_move_and_poll") {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_asset_catalog_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "Content" / "Trees");
    {
        std::ofstream source(root / "Content" / "Trees" / "Oak.asset", std::ios::binary);
        source << "oak-v1";
    }
    MemoryAssetDatabase database;
    DiskAssetCatalog    catalog(root, &database);
    REQUIRE(catalog.writeSidecar("Trees/Oak.asset", AssetGuid("guid-oak"), "park.tree").ok());
    auto first = catalog.scan();
    REQUIRE(first.ok());
    CHECK_EQ(first.value().indexed, static_cast<std::size_t>(1));
    CHECK_EQ(first.value().changed, static_cast<std::size_t>(1));
    CHECK_EQ(database.find(AssetGuid("guid-oak")).value().logicalUri, "content://Trees/Oak.asset");

    std::filesystem::create_directories(root / "Content" / "Plants");
    std::filesystem::rename(root / "Content" / "Trees" / "Oak.asset", root / "Content" / "Plants" / "Oak.asset");
    std::filesystem::rename(root / "Content" / "Trees" / "Oak.asset.evmeta",
                            root / "Content" / "Plants" / "Oak.asset.evmeta");
    auto moved = catalog.poll();
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value().changed, static_cast<std::size_t>(1));
    CHECK_EQ(database.find(AssetGuid("guid-oak")).value().logicalUri, "content://Plants/Oak.asset");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// editor_disk_document_v2.cpp
using namespace eve::editor;

namespace {

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           ("eve_editor_disk_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

EditorValue versionValue(int version) { return EditorValue(EditorValue::Object{{"version", EditorValue(version)}}); }

}  // namespace

TEST_CASE("editor.v2.disk_document_atomic_conflict_and_autosave") {
    const std::filesystem::path root = testRoot();
    std::filesystem::create_directories(root);
    DiskAtomicDocumentStore store(root);
    CHECK(store.resolve("content://World/Park.evscene").generic_string() ==
          (std::filesystem::canonical(root) / "Content" / "World" / "Park.evscene").generic_string());
    CHECK(store.resolve("content://../outside.evscene").empty());

    DocumentService   documents(&store);
    const DocumentKey key{DocumentKind::Scene, AssetGuid("park-disk")};
    auto              opened = documents.open(key, "Park", "content://World/Park.evscene", versionValue(0));
    REQUIRE(opened.ok());
    const DocumentId id = opened.value().id;
    REQUIRE(documents.edit(id, versionValue(1)).ok());
    const auto firstTicket = documents.requestSave(id);
    REQUIRE(firstTicket.ok());
    REQUIRE(documents.executeSave(firstTicket.value()).ok());

    const std::filesystem::path path = store.resolve("content://World/Park.evscene");
    CHECK(std::filesystem::is_regular_file(path));
    const auto savedOne = store.read("content://World/Park.evscene");
    REQUIRE(savedOne.ok());
    CHECK(savedOne.value().content == versionValue(1));

    // A timestamp-only external touch does not create a false conflict.
    std::error_code ec;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
    CHECK(!ec);
    REQUIRE(documents.edit(id, versionValue(2)).ok());
    auto sameContentBase = documents.requestSave(id);
    REQUIRE(documents.executeSave(sameContentBase.value()).ok());

    // Manual external content mutation with a stale envelope revision is still
    // detected because CAS validates the computed disk content hash.
    std::ifstream     input(path, std::ios::binary);
    const std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto              envelope = editorValueFromJson(json);
    REQUIRE(envelope.ok());
    auto *envelopeObject = envelope.value().getIf<EditorValue::Object>();
    REQUIRE(envelopeObject != nullptr);
    (*envelopeObject)["content"] = versionValue(99);
    input.close();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << editorValueToJson(envelope.value());
    output.close();

    REQUIRE(documents.edit(id, versionValue(3)).ok());
    auto       stale    = documents.requestSave(id);
    const auto conflict = documents.executeSave(stale.value());
    CHECK(static_cast<int>(conflict.code()) == static_cast<int>(EditorStatus::Conflict));
    const auto conflictedDocument = documents.snapshot(id);
    REQUIRE(conflictedDocument.ok());
    CHECK(static_cast<int>(conflictedDocument.value().state) == static_cast<int>(DocumentState::Conflict));

    // A failed atomic replace leaves the previously persisted file intact.
    DiskAtomicDocumentStore failureStore(root);
    const auto              beforeFailure = failureStore.read("content://World/Park.evscene");
    REQUIRE(beforeFailure.ok());
    failureStore.setBeforeReplaceHook([](const std::filesystem::path &) { return false; });
    const auto failed = failureStore.compareAndSwap("content://World/Park.evscene", beforeFailure.value().revision,
                                                    beforeFailure.value().contentHash, versionValue(4));
    CHECK(static_cast<int>(failed.code()) == static_cast<int>(EditorStatus::Failed));
    const auto afterFailure = store.read("content://World/Park.evscene");
    REQUIRE(afterFailure.ok());
    CHECK(afterFailure.value().content == beforeFailure.value().content);

    // Drafts live outside Content and are recoverable only against the same base.
    const auto formal = documents.snapshot(id);
    REQUIRE(formal.ok());
    AutosaveService autosave(&store, "park-project");
    const auto      working = documents.content(id);
    REQUIRE(working.ok());
    REQUIRE(autosave.writeDraft(formal.value(), working.value()).ok());
    const auto draft = autosave.readDraft(id);
    REQUIRE(draft.ok());
    CHECK(draft.value().content == versionValue(3));
    CHECK(autosave.shouldOfferRecovery(draft.value(), formal.value()));

    REQUIRE(documents.close(id).ok());
    std::filesystem::remove_all(root, ec);
}
}  // namespace test_editor_disk_asset_catalog_v2
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_font)
#include "PrivateTestScope.h"
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "common/Exception.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "font/Font.h"
#include "font/FontData.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// font.cpp
namespace test_font {
namespace {

// FontAwesome private-use icon with a non-empty bitmap in the fixture.
constexpr int kIconCodepoint = 0xF000;


EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool expectException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

eve::font::Font *fontModule() { return eve::font::Font::create(); }

std::unique_ptr<eve::font::FontData> loadFixture(int size = 24) {
    auto raw = readBinaryFile(pathBesideThisSource("fonts/FontAwesome.ttf"));
    if (raw.empty()) return nullptr;
    eve::data::ByteData data(raw.data(), raw.size());
    return std::unique_ptr<eve::font::FontData>(fontModule()->newFontData(&data, size));
}

}  // namespace

TEST_CASE("font.create") {
    auto *module = fontModule();
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("Font"));
}

TEST_CASE("font.newFontData.empty") {
    auto *module = fontModule();
    CHECK(expectException([&] { module->newFontData(nullptr); }));
    CHECK(expectException([&] { module->newFontDataFromFile(""); }));
}

TEST_CASE("font.newFontData.garbage") {
    auto               *module    = fontModule();
    const char          garbage[] = "this is not a font";
    eve::data::ByteData bytes(garbage, sizeof(garbage) - 1);
    CHECK(expectException([&] { module->newFontData(&bytes, 16); }));
}

TEST_CASE("font.newFontData.ttf") {
    auto fd = loadFixture(24);
    REQUIRE(fd.get() != nullptr);
    CHECK_EQ(fd->getSize(), 24);
    CHECK(fd->getAscent() > 0.f);
    CHECK(fd->getLineHeight() > 0.f);
    CHECK(fd->getGlyphCount() > 0);
    CHECK(!fd->getFamilyName().empty());
    CHECK(fd->hasGlyph(kIconCodepoint));
    CHECK(!fd->hasGlyph(static_cast<int>(0x4E00)));  // CJK not in fixture
    CHECK(fd->getWidth("\xEF\x80\x80") > 0.f);       // UTF-8 for U+F000
    CHECK(fd->getGlyphAdvance(kIconCodepoint) > 0);

    std::unique_ptr<eve::image::ImageData> glyph(fd->newGlyphImageData(kIconCodepoint));
    REQUIRE(glyph.get() != nullptr);
    CHECK(glyph->getWidth() > 0);
    CHECK(glyph->getHeight() > 0);
    CHECK_EQ(glyph->getFormat(), std::string("RGBA8"));
}

TEST_CASE("font.newFontDataFromFile.cached") {
    auto raw = readBinaryFile(pathBesideThisSource("fonts/FontAwesome.ttf"));
    REQUIRE(!raw.empty());

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity("ev_ut_font_cache", true));
    REQUIRE(fs->setupWriteDirectory());
    const char *name = "cached_font.ttf";
    fs->write(name, raw.data(), raw.size());

    auto                *module = fontModule();
    eve::font::FontData *a      = module->newFontDataFromFile(name, 16);
    eve::font::FontData *b      = module->newFontDataFromFile(name, 16);
    REQUIRE(a != nullptr);
    CHECK(a == b);  // one decoded face per (path, size)
    CHECK_EQ(a->getSize(), 16);

    eve::font::FontData *c = module->newFontDataFromFile(name, 24);
    REQUIRE(c != nullptr);
    CHECK(c != a);  // different size is a different cache entry
    CHECK_EQ(c->getSize(), 24);

    fs->remove(name);
}

TEST_CASE("font.newFontData.invalidSize") {
    auto raw = readBinaryFile(pathBesideThisSource("fonts/FontAwesome.ttf"));
    REQUIRE(!raw.empty());
    eve::data::ByteData data(raw.data(), raw.size());
    auto               *module = fontModule();
    CHECK(expectException([&] { module->newFontData(&data, 0); }));
    CHECK(expectException([&] { module->newFontData(&data, -8); }));
}
}  // namespace test_font
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map)
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "data/DataModule.h"
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
#include "map/DualGrid.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/MapObject.h"
#include "map/Pathfinder.h"
#include "map/TileCollision.h"
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "map/TileProjection.h"
#include "map/TileSystem.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// map.cpp
namespace test_map {
using namespace eve::map;

namespace {
class CollisionSinkMock final : public ITileCollisionSink {
public:
    void replaceTileCollision(TileLayerRef layer, const TileCollisionRect *rects, size_t count) override {
        source = layer;
        values.assign(rects, rects + count);
    }
    TileLayerRef                   source;
    std::vector<TileCollisionRect> values;
};
}  // namespace

namespace {

[[maybe_unused]] void hideAllTileLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw]   = *it;
        draw->visible = false;
    }
}

}  // namespace


TEST_CASE("map.tileset.loadsExternalTsjRelativeToMap") {
    std::string       error;
    const std::string dataDir = std::string(EVENGINE_SOURCE_DIR) + "/test/data/";
    auto              layers  = loadMapFile(dataDir + "tiled_external_map.json", &error);
    CHECK_EQ(error, std::string());
    REQUIRE(layers.size() == 1);
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getTile(0, 0), 20);
    CHECK_EQ(layer->getTile(1, 0), 30);
    CHECK_EQ(layer->getTilesetFirstGid(), 20);
    REQUIRE(layer->tileset()->atlases.size() == 2);
    CHECK_EQ(layer->tileset()->atlases[1].firstGid, 30);
    CHECK_EQ(layer->getTileDataType(20, "wet"), std::string("bool"));
    CHECK(layer->getTileDataBool(20, "wet"));
    CHECK_EQ(layer->getTileDataType(30, "walkable"), std::string("bool"));
    REQUIRE(layer->tileset()->animations.size() == 1);
    CHECK_EQ(layer->tileset()->animations[0].gid, 20);
    REQUIRE(layer->tileset()->animations[0].frames.size() == 2);
    CHECK_EQ(layer->tileset()->animations[0].frames[0].gid, 21);
    CHECK_EQ(layer->tileset()->animations[0].frames[1].gid, 20);
    REQUIRE(layer->tileset()->terrainRules.size() == 1);
    CHECK_EQ(layer->tileset()->terrainRules[0].gid, 20);
    CHECK_EQ(layer->tileset()->terrainRules[0].neighborMask, 1 << 3);
    CHECK_EQ(layer->resource()->texturePath, dataDir + "shore-missing.png");
    REQUIRE(layer->resource()->dependencyPaths.size() == 2);
    CHECK_EQ(layer->resource()->dependencyPaths[0], dataDir + "shore.tsj");
    CHECK_EQ(layer->resource()->dependencyPaths[1], dataDir + "walls.tsj");
    Pathfinder pathfinder(layer);
    CHECK(pathfinder.isWalkable(0, 0));
    CHECK(!pathfinder.isWalkable(1, 0));
    layer->setVisible(false);
}

TEST_CASE("map.tileset.loadsExternalTsxWithMetadataAnimationAndWang") {
    std::string       error;
    const std::string path   = std::string(EVENGINE_SOURCE_DIR) + "/test/data/tiled_external_tsx_map.json";
    auto              layers = loadMapFile(path, &error);
    CHECK_EQ(error, std::string());
    REQUIRE(layers.size() == 1);
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getTile(0, 0), 40);
    CHECK_EQ(layer->getTilesetFirstGid(), 40);
    CHECK_EQ(layer->getTileAnimationFrameCount(40), 2);
    CHECK_EQ(layer->getTileDataString(40, "terrainTag"), std::string("3"));
    REQUIRE(layer->tileset()->terrainRules.size() == 1);
    CHECK_EQ(layer->tileset()->terrainRules[0].gid, 40);
    CHECK_EQ(layer->tileset()->terrainRules[0].neighborMask, 1 << 1);
    Pathfinder pathfinder(layer);
    CHECK(!pathfinder.isWalkable(0, 0));
    Map map;
    CHECK_EQ(map.publishCollision(layer), 1);
    CHECK_EQ(map.getCollisionRectWidth(0), 8.f);
    layer->setVisible(false);
}


TEST_CASE("map.render.orthoAndIsoPreview") {
    hideAllTileLayers();

    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 640;
    s.height   = 400;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Map::create();

    // Left: orthogonal concentric rings (solidForGid colors, no atlas needed).
    TileLayer *ortho             = mod->newLayer(10, 8, 28.f, 28.f);
    ortho->config()->orientation = MapOrientation::Orthogonal;
    ortho->setOrigin(40.f, 40.f);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 10; ++x) {
            const int ring = std::min({x, y, 9 - x, 7 - y});
            ortho->setTile(x, y, uint32_t(1 + ring));
        }
    }
    ortho->setVisible(true);

    // Right: isometric diamond so projection is visibly different.
    TileLayer *iso             = mod->newLayer(6, 6, 48.f, 24.f);
    iso->config()->orientation = MapOrientation::Isometric;
    iso->setOrigin(420.f, 80.f);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            iso->setTile(x, y, uint32_t(3 + ((x + y) % 4)));
        }
    }
    iso->setVisible(true);

    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    REQUIRE(items.size() == 10u * 8u + 6u * 6u);

    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.12f, 1.f);
    for (int frame = 0; frame < 75; ++frame) {
        ortho->setOrigin(40.f + float(frame) * 0.15f, 40.f);
        iso->setOrigin(420.f, 80.f + float(std::sin(float(frame) * 0.08f)) * 6.f);

        gfx->clearScreen();
        mod->render(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    ortho->setVisible(false);
    iso->setVisible(false);
    win->close();
}
}  // namespace test_map
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_model3d)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/texture.h>
#include <assimp/vector3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Fixtures.h"
#include "PathBesideSource.h"
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
#include "model3d/ModelRenderer.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// model3d.cpp
namespace test_model3d {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


#include "graphics/shaders/mesh3d_toon_frag_spv.inc"
#include "graphics/shaders/mesh3d_toon_vert_spv.inc"

using namespace eve::graphics;

namespace {


EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string base64Encode(const uint8_t *data, size_t size) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string       out;
    out.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const uint32_t v = (a << 16) | (b << 8) | c;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += (i + 1 < size) ? tbl[(v >> 6) & 63] : '=';
        out += (i + 2 < size) ? tbl[v & 63] : '=';
    }
    return out;
}

void appendF32LE(std::vector<uint8_t> &bin, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    bin.push_back(uint8_t(bits));
    bin.push_back(uint8_t(bits >> 8));
    bin.push_back(uint8_t(bits >> 16));
    bin.push_back(uint8_t(bits >> 24));
}

void appendU16LE(std::vector<uint8_t> &bin, uint16_t v) {
    bin.push_back(uint8_t(v));
    bin.push_back(uint8_t(v >> 8));
}

/**
 * Minimal glTF 2.0 quad (XY plane facing +Z) with an embedded PNG base color
 * texture: top half red, bottom half blue. Y=+1 maps to v=0 so a correctly
 * oriented render shows red on top / blue on bottom.
 */
std::string makeUvOrientationGltf(const std::string &pngBase64) {
    std::vector<uint8_t> bin;
    const float          pos[4][3] = {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {-1.f, 1.f, 0.f}, {1.f, 1.f, 0.f}};
    const float          nrm[4][3] = {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}};
    const float          uv[4][2]  = {{0.f, 1.f}, {1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f}};
    for (const auto &p : pos)
        for (float c : p) appendF32LE(bin, c);
    for (const auto &n : nrm)
        for (float c : n) appendF32LE(bin, c);
    for (const auto &t : uv)
        for (float c : t) appendF32LE(bin, c);
    const uint16_t idx[6] = {0, 1, 2, 2, 1, 3};
    for (uint16_t i : idx) appendU16LE(bin, i);
    if (bin.size() != 140) return {};

    const std::string binBase64 = base64Encode(bin.data(), bin.size());
    return std::string(
               "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
               "\"nodes\":[{\"mesh\":0}],"
               "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
               "\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
               "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},"
               "\"metallicFactor\":0.0,\"roughnessFactor\":1.0}}],"
               "\"textures\":[{\"source\":0}],"
               "\"images\":[{\"uri\":\"data:image/png;base64,") +
           pngBase64 + "\"}],\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," + binBase64 +
           "\",\"byteLength\":140}],\"bufferViews\":[" +
           "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12,\"target\":34963}],"
           "\"accessors\":[" +
           "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
           "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
           "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"}," +
           "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]}";
}

float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }


void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

Texture *makeSolidRGB(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

Texture *textureFromImageData(Graphics *gfx, eve::image::ImageData *img) {
    if (!img) return nullptr;
    return gfx->newTexture(img);
}

Texture *loadAssimpDiffuseTexture(Graphics *gfx, const aiScene *scene, const aiMaterial *mat) {
    if (!scene || !mat) return nullptr;

    aiString path;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS) return nullptr;

    const char *p = path.C_Str();
    if (!p || !p[0]) return nullptr;

    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    // Embedded: "*0", "*1", ...
    if (p[0] == '*') {
        int idx = std::atoi(p + 1);
        if (idx < 0 || static_cast<unsigned>(idx) >= scene->mNumTextures) return nullptr;
        const aiTexture *tex = scene->mTextures[idx];
        if (!tex || !tex->pcData) return nullptr;

        if (tex->mHeight == 0) {
            // Compressed blob (png/jpg/...).
            eve::data::ByteData bytes(tex->pcData, static_cast<size_t>(tex->mWidth));
            try {
                eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
                Texture               *t   = textureFromImageData(gfx, img);
                delete img;
                return t;
            } catch (...) {
                return nullptr;
            }
        }

        // Uncompressed BGRA8888.
        const unsigned       w = tex->mWidth;
        const unsigned       h = tex->mHeight;
        std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
        const aiTexel       *src = tex->pcData;
        for (unsigned i = 0; i < w * h; ++i) {
            rgba[i * 4 + 0] = src[i].r;
            rgba[i * 4 + 1] = src[i].g;
            rgba[i * 4 + 2] = src[i].b;
            rgba[i * 4 + 3] = src[i].a;
        }
        return gfx->newTexture(int(w), int(h), rgba.data());
    }

    // External file — try VFS then OS path beside test assets.
    try {
        auto                                      *fs = eve::filesystem::Filesystem::create();
        std::unique_ptr<eve::filesystem::FileData> fd(fs->read(p));
        if (fd && fd->getSize() > 0) {
            eve::image::ImageData *img = eve::image::Image::create()->newImageData(fd.get());
            Texture               *t   = textureFromImageData(gfx, img);
            delete img;
            return t;
        }
    } catch (...) {
    }

    // Basename / common relative layouts under test/
    std::string base  = p;
    auto        slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);

    const char *candidates[] = {
        base.c_str(),
        nullptr,  // filled below for Textures/
    };
    std::string underTextures = std::string("Textures/") + base;
    candidates[1]             = underTextures.c_str();

    for (const char *rel : candidates) {
        if (!rel || !rel[0]) continue;
        auto fileBytes = readBinaryFile(pathBesideThisSource(rel));
        if (fileBytes.empty()) continue;
        try {
            eve::data::ByteData    bytes(fileBytes.data(), fileBytes.size());
            eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
            Texture               *t   = textureFromImageData(gfx, img);
            delete img;
            return t;
        } catch (...) {
        }
    }
    return nullptr;
}

Texture *loadTextureFile(Graphics *gfx, const char *relPathBesideTest) {
    if (!relPathBesideTest || !relPathBesideTest[0]) return nullptr;
    auto fileBytes = readBinaryFile(pathBesideThisSource(relPathBesideTest));
    if (fileBytes.empty()) return nullptr;
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    try {
        eve::data::ByteData    bytes(fileBytes.data(), fileBytes.size());
        eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
        Texture               *t   = textureFromImageData(gfx, img);
        delete img;
        return t;
    } catch (...) {
        return nullptr;
    }
}

struct Bounds {
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
    bool  valid = false;

    void expand(float x, float y, float z) {
        if (!valid) {
            minX = maxX = x;
            minY = maxY = y;
            minZ = maxZ = z;
            valid       = true;
            return;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        minZ = std::min(minZ, z);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        maxZ = std::max(maxZ, z);
    }

    float centerX() const { return 0.5f * (minX + maxX); }
    float centerY() const { return 0.5f * (minY + maxY); }
    float centerZ() const { return 0.5f * (minZ + maxZ); }
    float radius() const {
        const float dx = maxX - minX;
        const float dy = maxY - minY;
        const float dz = maxZ - minZ;
        return 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

Bounds boundsOf(eve::model3d::ModelData *md) {
    Bounds         b;
    const aiScene *scene = md->getScene();
    if (!scene || !scene->mRootNode) return b;
    std::function<void(const aiNode *, const aiMatrix4x4 &)> walk = [&](const aiNode *node, const aiMatrix4x4 &parent) {
        const aiMatrix4x4 world = parent * node->mTransformation;
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            const unsigned mi = node->mMeshes[i];
            if (mi >= scene->mNumMeshes) continue;
            const aiMesh *mesh = scene->mMeshes[mi];
            if (!mesh) continue;
            for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
                const aiVector3D p = world * mesh->mVertices[v];
                b.expand(p.x, p.y, p.z);
            }
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c) walk(node->mChildren[c], world);
    };
    walk(scene->mRootNode, aiMatrix4x4());
    return b;
}

/** Upload every mesh with Assimp material tint/texture; frame camera; save PNG.
 *  @param fallbackDiffuse optional path relative to test/ when Assimp has no map.
 *  @param makeStyle optional factory: given Graphics*, return Mesh3D Shader for all entities. */
void renderModelSmoke(eve::model3d::ModelData *md, const char *pngName, const char *fallbackDiffuse = nullptr,
                      std::function<Shader *(Graphics *)> makeStyle = {}) {
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 1);
    REQUIRE(pngName != nullptr);
    const aiScene *scene = md->getScene();
    REQUIRE(scene != nullptr);

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Shader *styleShader = makeStyle ? makeStyle(gfx) : nullptr;

    Bounds b = boundsOf(md);
    REQUIRE(b.valid);
    const float cx  = b.centerX();
    const float cy  = b.centerY();
    const float cz  = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    Texture *white       = makeSolidGray(gfx, 255);
    Texture *fallbackTex = loadTextureFile(gfx, fallbackDiffuse);
    struct MatLook {
        Texture *tex = nullptr;
        float    tr = 1.f, tg = 1.f, tb = 1.f;
    };
    std::unordered_map<unsigned, MatLook> matCache;

    int                                                      spawned = 0;
    std::function<void(const aiNode *, const aiMatrix4x4 &)> walk = [&](const aiNode *node, const aiMatrix4x4 &parent) {
        const aiMatrix4x4 world = parent * node->mTransformation;
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            const unsigned mi = node->mMeshes[i];
            if (mi >= scene->mNumMeshes) continue;
            const aiMesh *ai = scene->mMeshes[mi];
            if (!ai || ai->mNumFaces == 0) continue;
            Mesh *mesh = gfx->newMeshFromAssimp(*ai, world);
            REQUIRE(mesh != nullptr);
            REQUIRE_GT(mesh->indexCount, 0);

            MatLook        look{white, 1.f, 1.f, 1.f};
            const unsigned matIndex = ai->mMaterialIndex;
            auto           cached   = matCache.find(matIndex);
            if (cached != matCache.end()) {
                look = cached->second;
            } else if (scene->mMaterials && matIndex < scene->mNumMaterials) {
                const aiMaterial *mat = scene->mMaterials[matIndex];
                aiColor3D         kd(1.f, 1.f, 1.f);
                mat->Get(AI_MATKEY_COLOR_DIFFUSE, kd);

                Texture *fromMat = loadAssimpDiffuseTexture(gfx, scene, mat);
                if (fromMat) {
                    look.tex = fromMat;
                    look.tr  = kd.r;
                    look.tg  = kd.g;
                    look.tb  = kd.b;
                } else if (fallbackTex) {
                    look.tex = fallbackTex;
                    look.tr = look.tg = look.tb = 1.f;
                } else {
                    look.tex = makeSolidRGB(gfx, uint8_t(std::clamp(kd.r, 0.f, 1.f) * 255.f + 0.5f),
                                            uint8_t(std::clamp(kd.g, 0.f, 1.f) * 255.f + 0.5f),
                                            uint8_t(std::clamp(kd.b, 0.f, 1.f) * 255.f + 0.5f));
                    look.tr = look.tg = look.tb = 1.f;
                }
                matCache[matIndex] = look;
            } else if (fallbackTex) {
                look.tex           = fallbackTex;
                matCache[matIndex] = look;
            }

            auto *ent                    = Renderable3D::create();
            ent->meshRenderer()->mesh    = mesh;
            ent->meshRenderer()->texture = look.tex;
            ent->meshRenderer()->shader  = styleShader;
            ent->meshRenderer()->visible = true;
            ent->setTint(look.tr, look.tg, look.tb, 1.f);
            ++spawned;
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c) walk(node->mChildren[c], world);
    };
    REQUIRE(scene->mRootNode != nullptr);
    walk(scene->mRootNode, aiMatrix4x4());
    REQUIRE(spawned >= 1);

    auto *cam = Camera3D::createCamera();
    // 3/4 view: above + along +Z. Fit clip planes to asset scale (Rock1 is ~1e3 units).
    cam->setTarget(cx, cy, cz);
    cam->setEye(cx + rad * 0.9f, cy + rad * 0.55f, cz + rad * 1.8f);
    cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
    cam->data()->farZ  = std::max(100.f, rad * 20.f);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.12f, 0.14f, 0.18f, 1.f));

    // Light rays travel from camera toward the subject (matches RenderSystem3D lighting test).
    const float lx = cx - (cx + rad * 0.9f);
    const float ly = cy - (cy + rad * 0.55f);
    const float lz = cz - (cz + rad * 1.8f);
    RenderSystem3D::setDirectionalLight(lx, ly + rad * 1.2f, lz, 1.8f, 1.8f, 1.8f);

    // ~1s live rotation so the asset is visibly on screen before PNG capture.
    for (int i = 0; i < 60; ++i) {
        if (ecs::current()->getManager<Renderable3D>() != nullptr) {
            auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [xf, mr] = *it;
                if (!mr->visible) continue;
                xf->yaw = float(i) * 0.05f;
            }
        }
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);

    eve::image::ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *png = frame->encode(medialoader::FormatHandler::ENCODED_PNG, pngName, false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);

    // Under the CMake test binary dir (build/.../test/out), already gitignored via build*/.
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    std::error_code   ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string outPath = outDir + "/" + pngName;
    {
        std::ofstream out(outPath, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    std::printf("model3d render saved: %s (materials=%u cached=%zu style=%s)\n", outPath.c_str(), scene->mNumMaterials,
                matCache.size(), styleShader ? "yes" : "no");
    delete png;
    delete frame;

    win->close();
}

}  // namespace

// Minimal unit cube as Wavefront OBJ (same shape as medialoader model tests).
static const char kCubeObj[] =
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "f 1 2 3\n"
    "f 1 3 4\n"
    "f 5 6 7\n"
    "f 5 7 8\n"
    "f 1 2 6\n"
    "f 1 6 5\n"
    "f 2 3 7\n"
    "f 2 7 6\n"
    "f 3 4 8\n"
    "f 3 8 7\n"
    "f 4 1 5\n"
    "f 4 5 8\n";

TEST_CASE("model3d.newModelData.objFromMemory") {
    auto               *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(kCubeObj, sizeof(kCubeObj) - 1);
    auto               *md = mod->newModelData(&data, ".obj");
    REQUIRE(md != nullptr);
    CHECK(!md->empty());
    CHECK(md->getMeshCount() >= 1);
    CHECK(md->getVertexCount(0) > 0);
    CHECK(md->getFaceCount(0) > 0);
    CHECK(md->getMesh(0) != nullptr);
    CHECK(md->getScene() != nullptr);
    CHECK(md->getMaterialCount() >= 0);
    // Assimp's OBJ importer always computes per-face normals even without
    // `vn` lines, but never synthesizes UVs without `vt` lines.
    CHECK(md->hasNormals(0));
    CHECK(!md->hasTexCoords(0));
    CHECK_EQ(md->getTexCoordChannelCount(0), 0);
    CHECK_EQ(md->getVertexColorChannelCount(0), 0);
    CHECK(!md->hasTangents(0));
    delete md;
}

TEST_CASE("model3d.newModelData.emptyThrows") {
    auto *mod   = eve::model3d::Model3D::create();
    bool  threw = false;
    try {
        mod->newModelData(nullptr, ".obj");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("model3d.newModelData.invalidMeshIndexThrows") {
    auto               *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(kCubeObj, sizeof(kCubeObj) - 1);
    auto               *md = mod->newModelData(&data, ".obj");
    REQUIRE(md != nullptr);

    auto expectThrows = [](const std::function<void()> &fn) {
        try {
            fn();
        } catch (const eve::Exception &) {
            return true;
        }
        return false;
    };

    CHECK(expectThrows([&] { md->getVertexCount(999); }));
    CHECK(expectThrows([&] { md->getFaceCount(999); }));
    CHECK(expectThrows([&] { md->hasNormals(999); }));
    CHECK(expectThrows([&] { md->hasTexCoords(999); }));
    CHECK(md->getMesh(999) == nullptr);
    delete md;
}

TEST_CASE("model3d.newModelDataFromFile.missingThrows") {
    auto *mod   = eve::model3d::Model3D::create();
    bool  threw = false;
    try {
        mod->newModelDataFromFile("definitely_missing_eve_model3d.obj");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("model3d.newModelDataFromFile.cached") {
    const std::string objPath = pathBesideThisSource("sofa.obj");
    REQUIRE(!readBinaryFile(objPath).empty());

    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\')) testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_cache", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto                    *mod = eve::model3d::Model3D::create();
    eve::model3d::ModelData *a   = mod->newModelDataFromFile("sofa.obj");
    eve::model3d::ModelData *b   = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(a != nullptr);
    CHECK(a == b);  // identical requests share one decoded scene

    eve::model3d::ModelLoadOptions opt;
    opt.joinIdenticalVertices  = false;
    eve::model3d::ModelData *c = mod->newModelDataFromFile("sofa.obj", opt);
    REQUIRE(c != nullptr);
    CHECK(c != a);  // different decode options are a different cache entry

    CHECK(fs->unmount(testDir));
}

TEST_CASE("model3d.Model3D.getName") {
    auto *mod = eve::model3d::Model3D::create();
    CHECK(mod->getName() == "Model3D");
}

TEST_CASE("model3d.evmodel.roundTrip") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_evmodel", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->write("source.obj", kCubeObj, sizeof(kCubeObj) - 1);

    auto *mod = eve::model3d::Model3D::create();
    REQUIRE(mod->bakeModel("source.obj", "cube.evmodel"));
    auto *md = mod->newModelDataFromFile("cube.evmodel");
    REQUIRE(md != nullptr);
    CHECK(md->getMeshCount() >= 1);
    CHECK(md->getFaceCount(0) > 0);
    delete md;
}

TEST_CASE("model3d.newModelData.rock1Fbx") {
    const std::string path  = pathBesideThisSource("Rock1.fbx");
    auto              bytes = readBinaryFile(path);
    REQUIRE(!bytes.empty());

    auto               *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(bytes.data(), bytes.size());
    auto               *md = mod->newModelData(&data, ".fbx");
    REQUIRE(md != nullptr);
    CHECK(!md->empty());
    CHECK(md->getMeshCount() >= 1);
    CHECK(md->getScene() != nullptr);
    CHECK(md->hasTexCoords(0));

    int totalVerts = 0;
    int totalFaces = 0;
    for (int i = 0; i < md->getMeshCount(); ++i) {
        CHECK(md->getMesh(i) != nullptr);
        int verts = md->getVertexCount(i);
        int faces = md->getFaceCount(i);
        CHECK(verts > 0);
        CHECK(faces > 0);
        totalVerts += verts;
        totalFaces += faces;
    }
    CHECK(totalVerts > 0);
    CHECK(totalFaces > 0);
    delete md;
}

TEST_CASE("model3d.newModelDataFromFile.sofaObjMtl") {
    const std::string objPath = pathBesideThisSource("sofa.obj");
    const std::string mtlPath = pathBesideThisSource("sofa.mtl");
    REQUIRE(!readBinaryFile(objPath).empty());
    REQUIRE(!readBinaryFile(mtlPath).empty());

    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\')) testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_sofa", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md  = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(md != nullptr);
    CHECK(!md->empty());
    CHECK(md->getMeshCount() >= 1);
    // Blender MTL lists 5 materials; Assimp may add a default.
    CHECK(md->getMaterialCount() >= 5);
    CHECK(md->getScene() != nullptr);

    int totalVerts = 0;
    int totalFaces = 0;
    for (int i = 0; i < md->getMeshCount(); ++i) {
        CHECK(md->getMesh(i) != nullptr);
        int verts = md->getVertexCount(i);
        int faces = md->getFaceCount(i);
        CHECK(verts > 0);
        CHECK(faces > 0);
        totalVerts += verts;
        totalFaces += faces;
    }
    CHECK(totalVerts > 0);
    CHECK(totalFaces > 0);

    CHECK(fs->unmount(testDir));
}

TEST_CASE("model3d.render.rock1Fbx") {
    const std::string path  = pathBesideThisSource("Rock1.fbx");
    auto              bytes = readBinaryFile(path);
    REQUIRE(!bytes.empty());
    REQUIRE(!readBinaryFile(pathBesideThisSource("Textures/Rock1_Diffuse.png")).empty());

    auto               *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(bytes.data(), bytes.size());
    auto               *md = mod->newModelData(&data, ".fbx");
    REQUIRE(md != nullptr);
    // FBX material has no embedded map path; bind companion diffuse next to the asset.
    renderModelSmoke(md, "model3d_rock1.png", "Textures/Rock1_Diffuse.png");
    delete md;
}

TEST_CASE("model3d.render.rock1ToonShader") {
    const std::string path  = pathBesideThisSource("Rock1.fbx");
    auto              bytes = readBinaryFile(path);
    REQUIRE(!bytes.empty());
    REQUIRE(!readBinaryFile(pathBesideThisSource("Textures/Rock1_Diffuse.png")).empty());

    auto               *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(bytes.data(), bytes.size());
    auto               *md = mod->newModelData(&data, ".fbx");
    REQUIRE(md != nullptr);

    renderModelSmoke(md, "model3d_rock1_toon.png", "Textures/Rock1_Diffuse.png", [](Graphics *gfx) -> Shader * {
        std::vector<uint32_t> vert(mesh3d_toon_vert_spv, mesh3d_toon_vert_spv + mesh3d_toon_vert_spv_count);
        std::vector<uint32_t> frag(mesh3d_toon_frag_spv, mesh3d_toon_frag_spv + mesh3d_toon_frag_spv_count);
        Shader               *toon = gfx->newMeshShaderFromSpv(vert, frag);
        if (!toon || !toon->gpuHandle) throw eve::Exception("failed to create toon mesh shader");
        toon->declareFloat("bands");
        toon->declareFloat("rimPower");
        toon->declareFloat("rimStrength");
        toon->declareFloat("posterize");
        toon->sendFloat("bands", 4.f);
        toon->sendFloat("rimPower", 2.8f);
        toon->sendFloat("rimStrength", 0.55f);
        toon->sendFloat("posterize", 6.f);
        return toon;
    });
    delete md;
}

TEST_CASE("model3d.render.sofaObjMtl") {
    const std::string objPath = pathBesideThisSource("sofa.obj");
    const std::string mtlPath = pathBesideThisSource("sofa.mtl");
    REQUIRE(!readBinaryFile(objPath).empty());
    REQUIRE(!readBinaryFile(mtlPath).empty());

    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\')) testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_sofa_render", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md  = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(md != nullptr);
    renderModelSmoke(md, "model3d_sofa.png");

    CHECK(fs->unmount(testDir));
}

TEST_CASE("model3d.materialApi.objMtl") {
    const std::string objPath = pathBesideThisSource("sofa.obj");
    const std::string mtlPath = pathBesideThisSource("sofa.mtl");
    REQUIRE(!readBinaryFile(objPath).empty());
    REQUIRE(!readBinaryFile(mtlPath).empty());

    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\')) testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_material_obj", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md  = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMaterialCount() >= 5);

    const int mi = md->getMaterialIndex(0);
    REQUIRE(mi >= 0);
    REQUIRE(mi < md->getMaterialCount());

    const std::string name = md->getMaterialName(mi);
    REQUIRE(!name.empty());

    // Blender Kd lands in DIFFUSE (BASE_COLOR fallback).
    const float r = md->getMaterialBaseColorR(mi);
    const float g = md->getMaterialBaseColorG(mi);
    const float b = md->getMaterialBaseColorB(mi);
    CHECK(r > 0.05f);
    CHECK(g > 0.05f);
    CHECK(b > 0.05f);
    CHECK(md->getMaterialBaseColorA(mi) == 1.f);
    // No PBR factors in OBJ/MTL: defaults.
    CHECK(md->getMaterialMetallicFactor(mi) == 0.f);
    CHECK(md->getMaterialRoughnessFactor(mi) == 0.45f);
    // sofa.mtl has no map_Kd.
    CHECK(md->getMaterialTextureSlotCount(mi, "diffuse") == 0);
    CHECK(md->getMaterialTexturePath(mi, "diffuse").empty());
    CHECK(md->getMaterialTextureEmbeddedIndex(mi, "diffuse") == -1);
    CHECK(md->getMaterialTextureSlotCount(mi, "base_color") == 0);
    // Unknown texture type names are rejected.
    CHECK(md->getMaterialTextureSlotCount(mi, "bogus") == 0);
    CHECK(md->getMaterialTexturePath(mi, "bogus").empty());

    delete md;
    CHECK(fs->unmount(testDir));
}

TEST_CASE("model3d.materialApi.gltf") {
    // CesiumMan.gltf references the external CesiumMan_data.bin buffer, so the
    // glTF must be loaded through the VFS (in-memory decode cannot resolve it).
    const std::string dir = pathBesideThisSource("assets/skinned/cesium_man/glTF/");
    REQUIRE(!readBinaryFile(dir + "CesiumMan.gltf").empty());
    REQUIRE(!readBinaryFile(dir + "CesiumMan_data.bin").empty());

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_material_gltf", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md  = mod->newModelDataFromFile("CesiumMan.gltf");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMaterialCount() >= 1);
    CHECK(md->getMaterialIndex(0) == 0);
    CHECK(!md->getMaterialName(0).empty());
    CHECK(md->hasNormals(0));
    CHECK(md->hasTexCoords(0));

    // CesiumMan material: metallic 0 / roughness 1, external base color JPEG.
    CHECK(md->getMaterialMetallicFactor(0) == 0.f);
    CHECK(md->getMaterialRoughnessFactor(0) == 1.f);
    CHECK(md->getMaterialTextureSlotCount(0, "base_color") == 1);
    CHECK(md->getMaterialTexturePath(0, "base_color") == "CesiumMan_img0.jpg");
    CHECK(md->getMaterialTextureEmbeddedIndex(0, "base_color") == -1);
    // Double-sided flag exists on glTF assets that set it; absence is fine here.
    CHECK(!md->getMaterialTwoSided(0));

    CHECK(fs->unmount(dir));
}

TEST_CASE("model3d.buildRenderable.objMtl") {
    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\')) testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_build_obj", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md  = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 1);

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *ent = mod->createRenderable(gfx, md, 0);
    REQUIRE(ent != nullptr);
    REQUIRE(ent->meshRenderer()->mesh != nullptr);
    REQUIRE(ent->meshRenderer()->mesh->indexCount > 0);
    const int mi = md->getMaterialIndex(0);
    if (mi >= 0) {
        CHECK(ent->meshRenderer()->r == md->getMaterialBaseColorR(mi));
        CHECK(ent->meshRenderer()->g == md->getMaterialBaseColorG(mi));
        CHECK(ent->meshRenderer()->b == md->getMaterialBaseColorB(mi));
        CHECK(ent->meshRenderer()->metallic == md->getMaterialMetallicFactor(mi));
        CHECK(ent->meshRenderer()->roughness == md->getMaterialRoughnessFactor(mi));
    }

    auto prepared = mod->prepareFoliageDeformation(md, 0, ent);
    REQUIRE(prepared.ok());
    const auto deformation = ent->getMesh()->vegetationDeformationFactors();
    REQUIRE_EQ(deformation.size(), size_t(ent->getMesh()->gpuVertexCount) * 9u);
    std::vector<float> first(deformation.begin(), deformation.end());
    for (size_t i = 0; i < first.size(); i += 9) {
        CHECK(first[i + 3] >= 0.f);
        CHECK(first[i + 3] <= 1.f);
        CHECK(first[i + 4] >= 0.f);
        CHECK(first[i + 4] <= 1.f);
        CHECK(first[i + 5] >= 0.f);
        CHECK(first[i + 5] <= 1.f);
        CHECK(first[i + 7] > 0.f);
        CHECK(first[i + 8] > 0.f);
    }
    REQUIRE(mod->prepareFoliageDeformation(md, 0, ent).ok());
    CHECK(std::equal(first.begin(), first.end(), ent->getMesh()->vegetationDeformationFactors().begin()));
    const auto beforeInvalid = std::vector<float>(ent->getMesh()->vegetationDeformationFactors().begin(),
                                                  ent->getMesh()->vegetationDeformationFactors().end());
    CHECK(!mod->prepareFoliageDeformation(md, 999, ent).ok());
    CHECK(
        std::equal(beforeInvalid.begin(), beforeInvalid.end(), ent->getMesh()->vegetationDeformationFactors().begin()));

    // Invalid indices return nullptr without throwing.
    CHECK(mod->createRenderable(gfx, md, 999) == nullptr);
    CHECK(eve::model3d::buildRenderable(*gfx, md, 999) == nullptr);
    CHECK(eve::model3d::buildRenderable(*gfx, nullptr, 0) == nullptr);

    win->close();
    CHECK(fs->unmount(testDir));
}

TEST_CASE("model3d.render.gltfEmbeddedUvOrientation") {
    // 4x2 texture: top row red, bottom row blue.
    const int            tw = 4;
    const int            th = 2;
    std::vector<uint8_t> px(size_t(tw) * size_t(th) * 4);
    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            uint8_t   *p   = px.data() + (size_t(y) * tw + size_t(x)) * 4;
            const bool top = (y == 0);
            p[0]           = top ? 255 : 0;
            p[1]           = 0;
            p[2]           = top ? 0 : 255;
            p[3]           = 255;
        }
    }
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::image::ImageData        pngImg(tw, th, "RGBA8", px.data(), false);
    eve::filesystem::FileData   *png =
        pngImg.encode(eve::image::ImageData::FormatHandler::ENCODED_PNG, "gltf_uv.png", false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);
    const std::string gltfText =
        makeUvOrientationGltf(base64Encode(static_cast<const uint8_t *>(png->getData()), png->getSize()));
    delete png;

    auto               *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(gltfText.data(), gltfText.size());
    auto               *md = mod->newModelData(&data, ".gltf");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 1);

    const aiMesh *sourceMesh = md->getMesh(0);
    REQUIRE(sourceMesh != nullptr);
    REQUIRE(sourceMesh->mNumFaces > 0);
    const aiFace &sourceFace = sourceMesh->mFaces[0];
    REQUIRE(sourceFace.mNumIndices == 3);
    const aiVector3D centroid =
        (sourceMesh->mVertices[sourceFace.mIndices[0]] + sourceMesh->mVertices[sourceFace.mIndices[1]] +
         sourceMesh->mVertices[sourceFace.mIndices[2]]) /
        3.f;
    auto mapped = md->mapSurfacePointToUv(0, 0, centroid.x, centroid.y, centroid.z, 0);
    REQUIRE(mapped.ok());
    const auto  surfaceUv = std::move(mapped).takeValue();
    const float expectedU = (sourceMesh->mTextureCoords[0][sourceFace.mIndices[0]].x +
                             sourceMesh->mTextureCoords[0][sourceFace.mIndices[1]].x +
                             sourceMesh->mTextureCoords[0][sourceFace.mIndices[2]].x) /
                            3.f;
    const float expectedV = (sourceMesh->mTextureCoords[0][sourceFace.mIndices[0]].y +
                             sourceMesh->mTextureCoords[0][sourceFace.mIndices[1]].y +
                             sourceMesh->mTextureCoords[0][sourceFace.mIndices[2]].y) /
                            3.f;
    CHECK(std::fabs(surfaceUv.u - expectedU) < 1e-5f);
    CHECK(std::fabs(surfaceUv.v - expectedV) < 1e-5f);
    CHECK(std::fabs(surfaceUv.barycentricA - 1.f / 3.f) < 1e-5f);
    CHECK(std::fabs(surfaceUv.barycentricB - 1.f / 3.f) < 1e-5f);
    CHECK(std::fabs(surfaceUv.barycentricC - 1.f / 3.f) < 1e-5f);

    // Material API on the synthetic file: PBR factors + embedded texture.
    CHECK(md->getMaterialMetallicFactor(0) == 0.f);
    CHECK(md->getMaterialRoughnessFactor(0) == 1.f);
    CHECK(md->getMaterialTextureSlotCount(0, "base_color") == 1);
    CHECK(md->getMaterialTexturePath(0, "base_color") == "*0");
    CHECK(md->getMaterialTextureEmbeddedIndex(0, "base_color") == 0);
    CHECK(md->getEmbeddedTextureCount() == 1);
    CHECK(md->getEmbeddedTextureWidth(0) > 0);    // compressed blob: width == byte length
    CHECK(md->getEmbeddedTextureHeight(0) == 0);  // compressed blob

    eve::image::ImageData *decoded = md->getEmbeddedTextureImageData(0);
    REQUIRE(decoded != nullptr);
    CHECK(decoded->getWidth() == tw);
    CHECK(decoded->getHeight() == th);
    CHECK(decoded->getFormat() == "RGBA8");
    {
        const auto top = decoded->getPixel(0, 0);
        const auto bot = decoded->getPixel(0, 1);
        CHECK(top.r > top.b);
        CHECK(bot.b > bot.r);
    }
    delete decoded;

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *ent = mod->createRenderable(gfx, md, 0);
    REQUIRE(ent != nullptr);
    REQUIRE(ent->meshRenderer()->mesh != nullptr);
    REQUIRE(ent->meshRenderer()->texture != nullptr);  // embedded texture decoded + uploaded

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 3.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setUp(0.f, 1.f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 100.f;

    RenderSystem3D::setDirectionalLight(0.f, 0.f, -1.f, 1.5f, 1.5f, 1.5f);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.f, 0.f, 0.f, 1.f));

    for (int i = 0; i < 8; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    // Straight-on quad: world +Y (texture v=0, red) must land on the upper half.
    const Color top = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 4);
    const Color bot = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() * 3 / 4);
    CHECK(luma(top) > 0.05f);
    CHECK(luma(bot) > 0.05f);
    CHECK(top.r > top.b);  // red on top → not V-flipped
    CHECK(bot.b > bot.r);  // blue on bottom

    eve::image::ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *framePng =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, "model3d_gltf_uv.png", false);
    REQUIRE(framePng != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    std::error_code   ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string outPath = outDir + "/model3d_gltf_uv.png";
    {
        std::ofstream out(outPath, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(framePng->getData()), static_cast<std::streamsize>(framePng->getSize()));
        REQUIRE(out.good());
    }
    std::printf("model3d render saved: %s\n", outPath.c_str());
    delete framePng;
    delete frame;

    win->close();
    delete md;
}

TEST_CASE("model3d.render.cesiumManGltf") {
    const std::string dir = pathBesideThisSource("assets/skinned/cesium_man/glTF/");
    REQUIRE(!readBinaryFile(dir + "CesiumMan.gltf").empty());
    REQUIRE(!readBinaryFile(dir + "CesiumMan_img0.jpg").empty());

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_gltf", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md  = mod->newModelDataFromFile("CesiumMan.gltf");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 1);

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *ent = mod->createRenderable(gfx, md, 0);
    REQUIRE(ent != nullptr);
    REQUIRE(ent->meshRenderer()->mesh != nullptr);
    REQUIRE(ent->meshRenderer()->texture != nullptr);  // CesiumMan_img0.jpg resolved via VFS

    Bounds b = boundsOf(md);
    REQUIRE(b.valid);
    const float cx  = b.centerX();
    const float cy  = b.centerY();
    const float cz  = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    auto *cam = Camera3D::createCamera();
    cam->setTarget(cx, cy, cz);
    cam->setEye(cx + rad * 0.9f, cy + rad * 0.55f, cz + rad * 1.8f);
    cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
    cam->data()->farZ  = std::max(100.f, rad * 20.f);

    RenderSystem3D::setDirectionalLight(-(cx + rad * 0.9f), -(cy + rad * 0.55f) + rad * 1.2f, -(cz + rad * 1.8f), 1.8f,
                                        1.8f, 1.8f);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.12f, 0.14f, 0.18f, 1.f));

    for (int i = 0; i < 60; ++i) {
        if (ecs::current()->getManager<Renderable3D>() != nullptr) {
            auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [xf, mr] = *it;
                if (!mr->visible) continue;
                xf->yaw = float(i) * 0.05f;
            }
        }
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);

    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::image::ImageData       *frame       = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *png =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, "model3d_cesium_man.png", false);
    REQUIRE(png != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    std::error_code   ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string outPath = outDir + "/model3d_cesium_man.png";
    {
        std::ofstream out(outPath, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    std::printf("model3d render saved: %s (materials=%d)\n", outPath.c_str(), md->getMaterialCount());
    delete png;
    delete frame;

    win->close();
    CHECK(fs->unmount(dir));
}
}  // namespace test_model3d
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <string>
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
#include "particles/FloatingPointFixParticles.h"
#include "particles/GroundParticleCulling.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "particles/UnderwaterParticles.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles.cpp
namespace test_particles {
using namespace eve::particles;

namespace {

[[maybe_unused]] void hideAllEmitters() {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw]   = *it;
        draw->visible = false;
    }
}

}  // namespace


TEST_CASE("particles.groundCulling.filtersVisitorAndControlsPlayback") {
    constexpr int                          zoneTag   = 7201;
    constexpr int                          playerTag = 7202;
    auto                                  *mod       = Particles::create();
    auto                                  *emitter   = mod->newEmitter(32);
    auto                                  *physics   = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.0F, 0.0F, 0.0F, false));
    auto                                  *zone   = world->newBody("static", 0.0F, 0.0F, 0.0F);
    auto                                  *sensor = zone->newSphereShape(5.0F);
    sensor->setTag(zoneTag);
    sensor->setSensor(true);
    auto *player = world->newBody("dynamic", 0.0F, 0.0F, 0.0F);
    player->newSphereShape(0.5F)->setTag(playerTag);
    REQUIRE(emitter->isStopped());
    REQUIRE(static_cast<bool>(applyGroundParticleCulling(emitter, playerTag, 99, true, false)));
    CHECK(emitter->isStopped());
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    REQUIRE_EQ(world->getBeginTriggerSensorShapeTag(0), zoneTag);
    REQUIRE(static_cast<bool>(
        applyGroundParticleCulling(emitter, playerTag, world->getBeginTriggerVisitorShapeTag(0), true, false)));
    CHECK(emitter->isActive());
    CHECK(!applyGroundParticleCulling(emitter, playerTag, playerTag, true, true));
    CHECK(emitter->isActive());
    player->setPosition(10.0F, 0.0F, 0.0F);
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    REQUIRE(static_cast<bool>(
        applyGroundParticleCulling(emitter, playerTag, world->getEndTriggerVisitorShapeTag(0), false, true)));
    CHECK(emitter->isStopped());
    CHECK(!applyGroundParticleCulling(nullptr, playerTag, playerTag, true, false));
}


TEST_CASE("particles.config.loadFileAndHotReload") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_particles_cfg", true));
    REQUIRE(fs->setupWriteDirectory());

    const char *name  = "fx_spark.json";
    const char *json1 = R"({"emissionRate":11,"x":1,"y":2,"particleLifetime":[1,1]})";
    fs->write(name, json1, std::strlen(json1));

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    CHECK(e->loadConfig(name));
    CHECK_EQ(e->getConfigPath(), std::string(name));
    CHECK(std::abs(e->getEmissionRate() - 11.f) < 1e-4f);
    CHECK(e->getAutoReload());

    // Force stale modtime so poll reloads without waiting on FS resolution.
    e->resource()->modtime = 0;
    const char *json2      = R"({"emissionRate":77,"x":9,"y":8,"particleLifetime":[2,2]})";
    fs->write(name, json2, std::strlen(json2));

    CHECK_EQ(ParticleConfigSystem::poll(), 1);
    CHECK(std::abs(e->getEmissionRate() - 77.f) < 1e-4f);
    CHECK(std::abs(e->getX() - 9.f) < 1e-4f);

    e->setAutoReload(false);
    e->resource()->modtime = 0;
    const char *json3      = R"({"emissionRate":1})";
    fs->write(name, json3, std::strlen(json3));
    CHECK_EQ(ParticleConfigSystem::poll(), 0);
    CHECK(std::abs(e->getEmissionRate() - 77.f) < 1e-4f);

    CHECK(e->reloadConfig());
    CHECK(std::abs(e->getEmissionRate() - 1.f) < 1e-4f);
}

TEST_CASE("particles.config.newEmitterFromFile") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_particles_fromfile", true));
    REQUIRE(fs->setupWriteDirectory());

    const char *name = "fx_fromfile.json";
    const char *json = R"({"buffer":48,"preset":"smoke","emissionRate":25,"autoStart":true})";
    fs->write(name, json, std::strlen(json));

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitterFromFile(name);
    REQUIRE(e != nullptr);
    CHECK_EQ(e->getBufferSize(), 48);
    CHECK(std::abs(e->getEmissionRate() - 25.f) < 1e-4f);
    CHECK(e->isActive());
    CHECK_EQ(e->getConfigPath(), std::string(name));
}

TEST_CASE("particles.render.fireAndSmokePreview") {
    hideAllEmitters();

    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 480;
    s.height   = 360;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto            *mod  = Particles::create();
    ParticleEmitter *fire = mod->newEmitter(256);
    fire->applyPreset("fire");
    fire->setPosition(float(gfx->getWidth()) * 0.5f, float(gfx->getHeight()) * 0.72f);
    fire->setVisible(true);
    fire->start();

    ParticleEmitter *smoke = mod->newEmitter(128);
    smoke->applyPreset("smoke");
    smoke->setPosition(float(gfx->getWidth()) * 0.5f, float(gfx->getHeight()) * 0.62f);
    smoke->setVisible(true);
    smoke->start();

    ParticleEmitter *spark = mod->newEmitter(96);
    spark->applyPreset("spark");
    spark->setPosition(float(gfx->getWidth()) * 0.5f, float(gfx->getHeight()) * 0.55f);
    spark->setVisible(true);
    spark->start();

    // Seed a few frames so the first present already has particles.
    for (int i = 0; i < 8; ++i) ParticleSimSystem::update(0.016f);

    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    int framesAlive = 0;
    for (int frame = 0; frame < 90; ++frame) {
        ParticleSimSystem::update(0.016f);
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();

        if (fire->getCount() > 0) ++framesAlive;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(framesAlive, 30);
    CHECK_GT(fire->getCount(), 0);

    fire->setVisible(false);
    smoke->setVisible(false);
    spark->setVisible(false);
    win->close();
}
}  // namespace test_particles
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_resource)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "PathBesideSource.h"
#include "common/AsyncWork.h"
#include "common/Capability.h"
#include "common/Resource.h"
#include "common/ResourceRef.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "filesystem/ResourceReader.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// resource.cpp
namespace test_resource {
// resource.cpp
namespace {

/** Payload = a per-path generation counter observed at load time. */
class TestResource : public eve::Resource {
public:
    TestResource() : eve::Resource("") { ++alive; }
    ~TestResource() override { --alive; }

    void adopt(eve::Resource &replacement) override {
        auto &other = static_cast<TestResource &>(replacement);
        if (other.failOnAdopt) throw std::runtime_error("injected adopt failure");
        std::swap(value, other.value);
    }

    int  value       = 0;
    bool failOnAdopt = false;

    /** @brief Live instances, so a case can assert "destroyed exactly once". */
    static int alive;
};

/** Fake asset provider: claims "*.dat" and serves a fresh generation each load. */
class TestProvider : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "test"; }

    bool handlesPath(const std::string &normPath) const override {
        const std::string path = eve::ResourceManager::pathOfKey(normPath);
        return path.size() >= 4 && path.compare(path.size() - 4, 4, ".dat") == 0;
    }

    eve::Result<bool> reload(const std::string &) override {
        // ResourceManager owns the test cache refresh; this provider only
        // supplies detached candidates through load().
        return eve::Result<bool>::success(false);
    }

    eve::Resource *load(const std::string &key) override {
        if (!handlesPath(key)) return nullptr;
        // Failure injection: throw before any allocation, so the cache must end up
        // unchanged and no half-built candidate may leak.
        if (throwing.count(key)) throw std::runtime_error("injected provider failure");
        if (failures.count(key)) return nullptr;
        auto *r        = new TestResource();
        r->value       = ++generation[key];
        r->failOnAdopt = adoptFailures.count(key) != 0;
        return r;
    }

    static std::map<std::string, int> generation;
    static std::set<std::string>      failures;
    static std::set<std::string>      adoptFailures;
    static std::set<std::string>      throwing;
};

std::map<std::string, int> TestProvider::generation;
std::set<std::string>      TestProvider::failures;
std::set<std::string>      TestProvider::adoptFailures;
std::set<std::string>      TestProvider::throwing;
int                        TestResource::alive = 0;

/** Every case starts from an empty cache + empty capability registry. */
struct Reset {
    Reset() {
        eve::ResourceManager::getInstance().clear();
        eve::cap::detail::clearAllRaw();
    }
    ~Reset() {
        eve::ResourceManager::getInstance().clear();
        eve::cap::detail::clearAllRaw();
    }
    Reset(const Reset &)            = delete;
    Reset &operator=(const Reset &) = delete;
};

[[maybe_unused]] TestProvider &provider() {
    static TestProvider p;
    eve::cap::addListener<eve::caps::IAssetReloader>(&p, eve::caps::IAssetReloader::kCache);
    TestProvider::generation.clear();
    TestProvider::failures.clear();
    TestProvider::adoptFailures.clear();
    TestProvider::throwing.clear();
    return p;
}

[[maybe_unused]] eve::Resource *get(const std::string &key) { return eve::ResourceManager::getInstance().get(key); }

// A cache wait observes publication, not the executor's final thread teardown.
// Own and join workers before destroying their gate or clearing capabilities.
class DelayedResourceExecutor final : public eve::caps::IAsyncWorkExecutor {
public:
    ~DelayedResourceExecutor() override {
        release();
        for (auto &worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();
        eve::cap::revoke<eve::caps::IAsyncWorkExecutor>(this);
    }

    eve::Result<void> submit(std::function<void()> work) override {
        workers_.emplace_back([this, work = std::move(work)]() {
            { std::lock_guard<std::mutex> hold(gate_); }
            work();
        });
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    void release() {
        if (hold_.owns_lock()) hold_.unlock();
    }

private:
    std::mutex                   gate_;
    std::unique_lock<std::mutex> hold_{gate_};
    std::vector<std::thread>     workers_;
};

}  // namespace


TEST_CASE("resource.requestAsyncThenWaitFor") {
    Reset reset;
    provider();

    DelayedResourceExecutor executor;
    eve::cap::provide<eve::caps::IAsyncWorkExecutor>(&executor);

    auto queued = eve::ResourceManager::getInstance().request("async.dat");
    CHECK(queued.ok());
    CHECK_EQ(queued.code(), eve::StatusCode::Applied);
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 1u);
    CHECK(!eve::ResourceManager::getInstance().peek("async.dat").has_value());

    executor.release();
    auto ready = eve::ResourceManager::getInstance().waitFor("async.dat");
    REQUIRE(ready.ok());
    CHECK_EQ(static_cast<TestResource &>(ready.value().get()).value, 1);
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 0u);
}

TEST_CASE("resource.getJoinsInFlightRequest") {
    Reset reset;
    provider();

    DelayedResourceExecutor executor;
    eve::cap::provide<eve::caps::IAsyncWorkExecutor>(&executor);

    auto queued = eve::ResourceManager::getInstance().request("join.dat");
    CHECK(queued.ok());
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 1u);
    executor.release();
    eve::Resource *loaded = get("join.dat");
    REQUIRE(loaded != nullptr);
    CHECK_EQ(static_cast<TestResource *>(loaded)->value, 1);
}


TEST_CASE("resource.asyncLoadFailureSurfacesInWaitFor") {
    Reset reset;
    provider();

    // Failure injection on the asynchronous path: the worker fails to produce the
    // resource, so waitFor must report a failure and leave no pending job behind.
    TestProvider::failures.insert("async-fail.dat");
    DelayedResourceExecutor executor;
    eve::cap::provide<eve::caps::IAsyncWorkExecutor>(&executor);

    auto queued = eve::ResourceManager::getInstance().request("async-fail.dat");
    CHECK(queued.ok());
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 1u);

    executor.release();
    auto ready = eve::ResourceManager::getInstance().waitFor("async-fail.dat");
    CHECK(!ready.ok());
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 0u);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
    CHECK(!eve::ResourceManager::getInstance().peek("async-fail.dat").has_value());
}

// resource_format_image.cpp
namespace {
EVE_DEFINE_PATH_BESIDE_SOURCE()

[[maybe_unused]] std::vector<char> fixture(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &name) {
    std::ifstream input(pathBesideThisSource(("fixtures/resource_formats/" + name).c_str()), std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[maybe_unused]] void decodePattern(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension,
                                    bool alpha, float tolerance, float divisor = 255.f) {
    const auto                             bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern." + extension);
    eve::data::ByteData                    source(bytes.data(), bytes.size());
    auto                                  *module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> decoded(module->newImageData(&source));
    REQUIRE(decoded != nullptr);
    REQUIRE_EQ(decoded->getWidth(), 5);
    REQUIRE_EQ(decoded->getHeight(), 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            const auto pixel = decoded->getPixel(x, y);
            CHECK(std::fabs(pixel.r - (17 + x * 37) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.g - (23 + y * 71) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.b - (11 + x * 13 + y * 29) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.a - (alpha ? (x + y * 5) * 17 / 255.f : 1.f)) <= tolerance);
        }
    }
}

[[maybe_unused]] void exportPattern(zeroerr::TestContext                     *_ZEROERR_TEST_CONTEXT,
                                    medialoader::FormatHandler::EncodedFormat format, const std::string &pixelFormat,
                                    const std::string &filename) {
    auto                                  *module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> source(module->newImageData(5, 3, pixelFormat));
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) {
            const float lowBits = pixelFormat == "RGBA16" ? (x + 1) / 65535.f : 0.f;
            source->setPixel(x, y,
                             {(17 + x * 37) / 255.f + lowBits, (23 + y * 71) / 255.f + lowBits,
                              (11 + x * 13 + y * 29) / 255.f + lowBits, (x + y * 5) * 17 / 255.f + lowBits});
        }
    std::unique_ptr<eve::filesystem::FileData> encoded(source->encode(format, filename.c_str(), false));
    REQUIRE(encoded != nullptr);
    std::unique_ptr<eve::image::ImageData> decoded(module->newImageData(encoded.get()));
    REQUIRE_EQ(decoded->getWidth(), source->getWidth());
    REQUIRE_EQ(decoded->getHeight(), source->getHeight());
    REQUIRE_EQ(decoded->getFormat(), pixelFormat);
    REQUIRE_EQ(decoded->getSize(), source->getSize());
    CHECK_EQ(std::memcmp(decoded->getData(), source->getData(), source->getSize()), 0);
    if (const char *directory = std::getenv("EVENGINE_FORMAT_EXPORT_DIR")) {
        std::filesystem::create_directories(directory);
        std::ofstream output(std::filesystem::path(directory) / filename, std::ios::binary);
        REQUIRE(output.good());
        output.write(static_cast<const char *>(encoded->getData()), static_cast<std::streamsize>(encoded->getSize()));
        REQUIRE(output.good());
    }
}
}  // namespace

TEST_CASE("resourceFormats.image.png.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "png", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.tga.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "tga", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.bmp.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "bmp", false, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.jpg.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "jpg", false, 3.f / 255.f);
}
TEST_CASE("resourceFormats.image.jpeg.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "jpeg", false, 3.f / 255.f);
}
TEST_CASE("resourceFormats.image.gif.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "gif", false, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.webp.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "webp", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.hdr.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "hdr", false, 1.f / 256.f, 256.f);
}
TEST_CASE("resourceFormats.image.exr.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "exr", false, 2.f / 255.f);
}
TEST_CASE("resourceFormats.image.png.rgba8Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_PNG, "RGBA8", "rgba8.png");
}
TEST_CASE("resourceFormats.image.png.rgba16Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_PNG, "RGBA16", "rgba16.png");
}
TEST_CASE("resourceFormats.image.tga.rgba8Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_TGA, "RGBA8", "rgba8.tga");
}

TEST_CASE("resourceFormats.image.truncatedHeadersRejectWithoutPoisoningDecoder") {
    auto *module = eve::image::Image::create();
    for (const auto *extension : {"png", "tga", "bmp", "jpg", "exr", "hdr", "gif", "webp"}) {
        auto bytes = fixture(_ZEROERR_TEST_CONTEXT, std::string("pattern.") + extension);
        bytes.resize(4);
        eve::data::ByteData source(bytes.data(), bytes.size());
        bool                rejected = false;
        try {
            std::unique_ptr<eve::image::ImageData> unexpected(module->newImageData(&source));
        } catch (const std::exception &) {
            rejected = true;
        }
        CHECK(rejected);
    }
    decodePattern(_ZEROERR_TEST_CONTEXT, "png", true, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.gif.animatedFirstFrame") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "animated.gif", false, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.webp.animatedFirstFrame") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "animated.webp", false, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.webp.lossyPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "lossy.webp", false, 0.15f);
}

TEST_CASE("resourceFormats.image.hdr.truncatedPayloadIsRejected") {
    auto bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern.hdr");
    bytes.pop_back();
    eve::data::ByteData source(bytes.data(), bytes.size());
    bool                rejected = false;
    try {
        std::unique_ptr<eve::image::ImageData> unexpected(eve::image::Image::create()->newImageData(&source));
    } catch (const std::exception &) {
        rejected = true;
    }
    REQUIRE(rejected);
}

TEST_CASE("resourceFormats.image.webp.truncatedPayloadIsRejected") {
    auto bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern.webp");
    bytes.resize(bytes.size() / 2);
    eve::data::ByteData source(bytes.data(), bytes.size());
    bool                rejected = false;
    try {
        std::unique_ptr<eve::image::ImageData> unexpected(eve::image::Image::create()->newImageData(&source));
    } catch (const std::exception &) {
        rejected = true;
    }
    REQUIRE(rejected);
}

// resource_uri.cpp
namespace {

class MemoryFileSystem final : public eve::service::IFileSystem {
public:
    bool readFile(const std::string &path, std::vector<std::uint8_t> &out) override {
        lastPath         = path;
        const auto found = files.find(path);
        if (found == files.end()) return false;
        out = found->second;
        return true;
    }

    bool writeFile(const std::string &, const void *, size_t) override { return false; }

    bool fileExists(const std::string &path) override { return files.find(path) != files.end(); }

    std::map<std::string, std::vector<std::uint8_t>> files;
    std::string                                      lastPath;
};

template <typename T>
[[maybe_unused]] T take(eve::Result<T> result) {
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace
}  // namespace test_resource
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_resource_decoder_lifetime)
#include "PrivateTestScope.h"
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "medialoader/sound/WaveDecoder.h"
#include "sound/Decoder.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// resource_decoder_lifetime.cpp
namespace test_resource_decoder_lifetime {
namespace {

EVE_DEFINE_PATH_BESIDE_SOURCE()

std::unique_ptr<eve::sound::Decoder> decoder(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    std::ifstream input(pathBesideThisSource("fixtures/resource_formats/tone.wav"), std::ios::binary);
    REQUIRE(input.good());
    std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto              provider = std::make_unique<medialoader::WaveDecoder>(bytes.data(), bytes.size(), 1024);
    return std::make_unique<eve::sound::Decoder>(std::move(provider), std::move(bytes));
}

std::vector<char> drain(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, eve::sound::Decoder &stream) {
    std::vector<char> result;
    for (int count = 0; count < 100 && !stream.isFinished(); ++count) {
        const int size = stream.decode();
        REQUIRE(size >= 0);
        const auto *buffer = static_cast<const char *>(stream.getBuffer());
        result.insert(result.end(), buffer, buffer + size);
        if (size == 0) break;
    }
    REQUIRE_EQ(result.size(), 44100u);
    return result;
}
}  // namespace

TEST_CASE("resourceFormats.sound.clone.originalDestroyedFirst") {
    const auto                           expected = drain(_ZEROERR_TEST_CONTEXT, *decoder(_ZEROERR_TEST_CONTEXT));
    auto                                 original = decoder(_ZEROERR_TEST_CONTEXT);
    std::unique_ptr<eve::sound::Decoder> clone(original->clone());
    original.reset();
    CHECK(drain(_ZEROERR_TEST_CONTEXT, *clone) == expected);
}

TEST_CASE("resourceFormats.sound.clone.cloneDestroyedFirst") {
    const auto expected = drain(_ZEROERR_TEST_CONTEXT, *decoder(_ZEROERR_TEST_CONTEXT));
    auto       original = decoder(_ZEROERR_TEST_CONTEXT);
    {
        std::unique_ptr<eve::sound::Decoder> clone(original->clone());
        CHECK(drain(_ZEROERR_TEST_CONTEXT, *clone) == expected);
    }
    CHECK(drain(_ZEROERR_TEST_CONTEXT, *original) == expected);
}

TEST_CASE("resourceFormats.sound.clone.nestedClonesRetainStorageAndIndependentPosition") {
    const auto expected = drain(_ZEROERR_TEST_CONTEXT, *decoder(_ZEROERR_TEST_CONTEXT));
    auto       original = decoder(_ZEROERR_TEST_CONTEXT);
    REQUIRE(original->decode() > 0);
    std::unique_ptr<eve::sound::Decoder> clone(original->clone());
    std::unique_ptr<eve::sound::Decoder> nested(clone->clone());
    original.reset();
    CHECK(drain(_ZEROERR_TEST_CONTEXT, *clone) == expected);
    clone.reset();
    CHECK(drain(_ZEROERR_TEST_CONTEXT, *nested) == expected);
}
}  // namespace test_resource_decoder_lifetime
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_resource_format_model)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "data/ByteData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// resource_format_model.cpp
namespace test_resource_format_model {
namespace {

EVE_DEFINE_PATH_BESIDE_SOURCE()

void triangle(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension) {
    const auto    path = pathBesideThisSource(("fixtures/resource_formats/triangle." + extension).c_str());
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::vector<char>   bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    eve::data::ByteData source(bytes.data(), bytes.size());
    std::unique_ptr<eve::model3d::ModelData> model(
        eve::model3d::Model3D::create()->newModelData(&source, "." + extension));
    REQUIRE(model != nullptr);
    REQUIRE_EQ(model->getMeshCount(), 1);
    REQUIRE_EQ(model->getVertexCount(0), 3);
    REQUIRE_EQ(model->getFaceCount(0), 1);
    std::vector<std::array<float, 3>> positions;
    for (int vertex = 0; vertex < 3; ++vertex)
        positions.push_back({model->getVertexPosition(0, vertex, 0), model->getVertexPosition(0, vertex, 1),
                             model->getVertexPosition(0, vertex, 2)});
    std::sort(positions.begin(), positions.end());
    const std::array<std::array<float, 3>, 3> expected{{{0, 0, 0}, {0, 3, 0}, {2, 0, 0}}};
    for (size_t vertex = 0; vertex < positions.size(); ++vertex)
        for (size_t component = 0; component < 3; ++component)
            CHECK(std::fabs(positions[vertex][component] - expected[vertex][component]) < 1e-6f);
}
}  // namespace

TEST_CASE("resourceFormats.model.obj.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "obj"); }
TEST_CASE("resourceFormats.model.stl.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "stl"); }
TEST_CASE("resourceFormats.model.ply.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "ply"); }
TEST_CASE("resourceFormats.model.off.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "off"); }
TEST_CASE("resourceFormats.model.x.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "x"); }
TEST_CASE("resourceFormats.model.dae.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "dae"); }
TEST_CASE("resourceFormats.model.gltf.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "gltf"); }
TEST_CASE("resourceFormats.model.glb.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "glb"); }
}  // namespace test_resource_format_model
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_resource_format_sound)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "data/ByteData.h"
#include "sound/Decoder.h"
#include "sound/Sound.h"
#include "sound/SoundData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// resource_format_sound.cpp
namespace test_resource_format_sound {
namespace {

EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> tone(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension) {
    std::ifstream input(pathBesideThisSource(("fixtures/resource_formats/tone." + extension).c_str()),
                        std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void decodeTone(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension, bool lossless) {
    const auto                             bytes = tone(_ZEROERR_TEST_CONTEXT, extension);
    eve::data::ByteData                    source(bytes.data(), bytes.size());
    auto                                  *module = eve::sound::Sound::create();
    std::unique_ptr<eve::sound::SoundData> decoded(module->newSoundData(&source));
    REQUIRE(decoded != nullptr);
    REQUIRE_EQ(decoded->getSampleRate(), 44100);
    REQUIRE_EQ(decoded->getChannelCount(), 2);
    REQUIRE_EQ(decoded->getBitDepth(), 16);
    REQUIRE(decoded->getSampleCount() >= 11025);
    CHECK(decoded->getDuration() < 0.32);
    const auto *samples = static_cast<const int16_t *>(decoded->getData());
    // Distinct frequencies/amplitudes catch channel swaps and silent output.
    double error = 0;
    for (int n = 0; n < 11025; ++n) {
        const auto left  = std::round(12000 * std::sin(2 * 3.14159265358979323846 * 440 * n / 44100));
        const auto right = std::round(6000 * std::sin(2 * 3.14159265358979323846 * 880 * n / 44100));
        error += std::fabs(samples[n * 2] - left) + std::fabs(samples[n * 2 + 1] - right);
    }
    CHECK(error / 22050 < (lossless ? 0.01 : 800.0));
    std::unique_ptr<eve::sound::Decoder> stream(module->newDecoder(&source, 1024));
    std::vector<char>                    streaming;
    for (int count = 0; count < 1000 && !stream->isFinished(); ++count) {
        const int size = stream->decode();
        REQUIRE(size >= 0);
        const auto *buffer = static_cast<const char *>(stream->getBuffer());
        streaming.insert(streaming.end(), buffer, buffer + size);
        if (size == 0) break;
    }
    REQUIRE_EQ(streaming.size(), decoded->getSize());
    CHECK_EQ(std::memcmp(streaming.data(), decoded->getData(), streaming.size()), 0);
    REQUIRE(stream->rewind());
    const int firstChunk = stream->decode();
    REQUIRE(firstChunk > 0);
    CHECK_EQ(std::memcmp(stream->getBuffer(), streaming.data(), static_cast<size_t>(firstChunk)), 0);
}
}  // namespace

TEST_CASE("resourceFormats.sound.wav.independentPcmAndStreaming") { decodeTone(_ZEROERR_TEST_CONTEXT, "wav", true); }
TEST_CASE("resourceFormats.sound.flac.independentPcmAndStreaming") { decodeTone(_ZEROERR_TEST_CONTEXT, "flac", true); }
TEST_CASE("resourceFormats.sound.ogg.independentPcmAndStreaming") { decodeTone(_ZEROERR_TEST_CONTEXT, "ogg", false); }
TEST_CASE("resourceFormats.sound.mp3.independentPcmAndStreaming") { decodeTone(_ZEROERR_TEST_CONTEXT, "mp3", false); }
}  // namespace test_resource_format_sound
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_sceneloader)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/vector3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "PathBesideSource.h"
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
#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
#include "scene/loader/SceneLoader.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// sceneloader.cpp
namespace test_sceneloader {
using namespace eve::scene;
using namespace eve::sceneloader;

namespace {

EVE_DEFINE_PATH_BESIDE_SOURCE()

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

template <class T>
T *sceneValue(eve::Result<T *> result) {
    if (!result.ok()) return nullptr;
    return result.value();
}

SceneNode *sceneNode(SceneHost *host, const std::string &id) {
    if (!host) return nullptr;
    return sceneValue(host->findById(id));
}

void openGfx(eve::window::Window *&win, eve::graphics::Graphics *&gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = static_cast<uint16_t>(w);
    s.height   = static_cast<uint16_t>(h);
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

std::string base64Encode(const uint8_t *data, size_t len) {
    static const char *kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string        out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < len) n |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < len) n |= uint32_t(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? kTable[(n >> 6) & 63] : '=');
        out.push_back((i + 2 < len) ? kTable[n & 63] : '=');
    }
    return out;
}

std::vector<uint8_t> buildTriangleBuffer() {
    auto pushF = [](std::vector<uint8_t> &b, float v) {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, sizeof(v));
        for (unsigned i = 0; i < 4; ++i) b.push_back(bytes[i]);
    };
    auto pushU16 = [](std::vector<uint8_t> &b, uint16_t v) {
        b.push_back(uint8_t(v & 0xff));
        b.push_back(uint8_t((v >> 8) & 0xff));
    };
    std::vector<uint8_t> buf;
    const float          positions[9] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    const float          normals[9]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float          uvs[6]       = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    for (float v : positions) pushF(buf, v);
    for (float v : normals) pushF(buf, v);
    for (float v : uvs) pushF(buf, v);
    pushU16(buf, 0);
    pushU16(buf, 1);
    pushU16(buf, 2);
    return buf;
}

std::string buildPbrGltf() {
    std::vector<uint8_t> buf = buildTriangleBuffer();
    const std::string    b64 = base64Encode(buf.data(), buf.size());
    return "{\"asset\":{\"version\":\"2.0\"},"
           "\"extensionsUsed\":[\"KHR_lights_punctual\"],"
           "\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[{\"type\":\"directional\","
           "\"color\":[1.0,0.5,0.25],\"intensity\":2.0,\"name\":\"sun\"}]}},"
           "\"scene\":0,\"scenes\":[{\"nodes\":[0,1,2]}],"
           "\"nodes\":["
           "{\"name\":\"tri\",\"mesh\":0},"
           "{\"name\":\"light\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}},"
           "{\"name\":\"cam\",\"camera\":0}],"
           "\"cameras\":[{\"type\":\"perspective\",\"perspective\":{\"yfov\":0.7,\"znear\":0.1,"
           "\"zfar\":100.0}}],"
           "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
           "\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
           "\"materials\":[{\"name\":\"pbr\",\"pbrMetallicRoughness\":{\"baseColorFactor\":"
           "[0.2,0.4,0.6,1.0],\"metallicFactor\":0.9,\"roughnessFactor\":0.25},\"doubleSided\":true}],"
           "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
           b64 + "\",\"byteLength\":" + std::to_string(buf.size()) +
           "}],"
           "\"bufferViews\":["
           "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36,\"target\":34962},"
           "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36,\"target\":34962},"
           "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24,\"target\":34962},"
           "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":6,\"target\":34963}],"
           "\"accessors\":["
           "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
           "\"min\":[0,0,0],\"max\":[1,1,0]},"
           "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
           "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
           "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";
}
}  // namespace

// ---- pure diff / apply logic (no graphics required) ----

TEST_CASE("SceneLoader.diff.addRemoveModifyMove") {
    SceneHost *h = sceneValue(SceneHost::createHost("sl"));
    h->setTree(node("root", {node("a"), node("b").withPosition(1.f, 0.f, 0.f), node("c", {node("c1")})}));

    // b modified, a removed, d added, c stays.
    NodeDesc newRoot = node("root", {
                                        node("b").withPosition(9.f, 0.f, 0.f),
                                        node("d"),
                                        node("c", {node("c1")}),
                                    });

    SceneDiff d = SceneLoader::diffTree(h, newRoot);
    CHECK_EQ(d.removed, 1);   // a
    CHECK_EQ(d.added, 1);     // d
    CHECK_EQ(d.modified, 1);  // b
    CHECK_EQ(d.moved, 0);

    REQUIRE(SceneLoader::applyTreeDiff(h, newRoot, d, nullptr, nullptr));
    CHECK(sceneNode(h, "a") == nullptr);
    CHECK(sceneNode(h, "d") != nullptr);
    CHECK(sceneNode(h, "b") != nullptr);
    CHECK(approx(sceneNode(h, "b")->x, 9.f));
    CHECK(sceneNode(h, "c") != nullptr);
    CHECK(sceneNode(h, "c1") != nullptr);
    // Root keeps its identity.
    CHECK(h->getRoot()->id == "root");
}

TEST_CASE("SceneLoader.diff.movesReparent") {
    SceneHost *h = sceneValue(SceneHost::createHost("slmove"));
    h->setTree(node("root", {node("a"), node("b")}));

    // Move b under a.
    NodeDesc  newRoot = node("root", {node("a", {node("b")})});
    SceneDiff d       = SceneLoader::diffTree(h, newRoot);
    CHECK_EQ(d.moved, 1);
    CHECK_EQ(d.removed, 0);
    CHECK_EQ(d.added, 0);

    REQUIRE(SceneLoader::applyTreeDiff(h, newRoot, d, nullptr, nullptr));
    CHECK(h->getParentById("b")->id == "a");
    CHECK_EQ(h->getChildCountById("root"), 1);
}

TEST_CASE("SceneLoader.diff.unchangedIsNoop") {
    SceneHost *h = sceneValue(SceneHost::createHost("slnop"));
    h->setTree(node("root", {node("m1").withPosition(1.f, 2.f, 3.f), node("m2")}));

    NodeDesc  same = node("root", {node("m1").withPosition(1.f, 2.f, 3.f), node("m2")});
    SceneDiff d    = SceneLoader::diffTree(h, same);
    CHECK(d.empty());
    CHECK_EQ(d.added, 0);
    CHECK_EQ(d.removed, 0);
    CHECK_EQ(d.modified, 0);
}

TEST_CASE("SceneLoader.apply.preservesUnchangedRenderableIdentity") {
    SceneHost *h = sceneValue(SceneHost::createHost("slid"));
    h->setTree(node("root", {node("keep"), node("gone")}));

    // Manually link a Renderable3D to "keep" (pure ECS entity; no mesh upload here).
    auto *r = eve::graphics::Renderable3D::create();
    REQUIRE(r != nullptr);
    REQUIRE(h->linkRenderable3D("keep", r));

    // New tree: "gone" removed, "keep" stays (unchanged), "keep2" added.
    NodeDesc  newRoot = node("root", {node("keep"), node("keep2")});
    SceneDiff d       = SceneLoader::diffTree(h, newRoot);
    CHECK_EQ(d.removed, 1);
    CHECK_EQ(d.added, 1);

    REQUIRE(SceneLoader::applyTreeDiff(h, newRoot, d, nullptr, nullptr));
    SceneNode *after = sceneNode(h, "keep");
    REQUIRE(after != nullptr);
    // The unchanged GameObject keeps its linked Renderable3D — no rebuild / re-upload.
    REQUIRE_EQ(after->links.size(), 1u);
    CHECK(after->links[0].target == r);
    CHECK(after->links[0].kind == eve::scene::findLinkKind("renderable3d"));
    CHECK(sceneNode(h, "keep2") != nullptr);
    CHECK(sceneNode(h, "gone") == nullptr);

    ecs::DestroyEntity(r);
}

TEST_CASE("SceneLoader.importConventions.lodCollisionSocket") {
    aiScene scene{};
    scene.mRootNode               = new aiNode("Root");
    scene.mRootNode->mNumChildren = 4;
    scene.mRootNode->mChildren    = new aiNode *[4];
    scene.mRootNode->mChildren[0] = new aiNode("Building_LOD0");
    scene.mRootNode->mChildren[1] = new aiNode("Building_LOD1");
    scene.mRootNode->mChildren[2] = new aiNode("UCX_Building");
    scene.mRootNode->mChildren[3] = new aiNode("SOCKET_Door");
    for (unsigned i = 0; i < 4; ++i) scene.mRootNode->mChildren[i]->mParent = scene.mRootNode;

    NodeDesc root = SceneLoader::buildNodeDesc(&scene);
    REQUIRE_EQ(root.children.size(), 4u);
    CHECK(root.children[0].visible);
    CHECK(!root.children[1].visible);
    CHECK(!root.children[2].visible);
    CHECK(std::find(root.children[0].tags.begin(), root.children[0].tags.end(), "lod:0") !=
          root.children[0].tags.end());
    CHECK(std::find(root.children[1].tags.begin(), root.children[1].tags.end(), "lod:1") !=
          root.children[1].tags.end());
    CHECK(std::find(root.children[2].tags.begin(), root.children[2].tags.end(), "collision") !=
          root.children[2].tags.end());
    CHECK(std::find(root.children[3].tags.begin(), root.children[3].tags.end(), "model-socket") !=
          root.children[3].tags.end());
}

// ---- integration: decode a real 3D scene into ECS GameObjects + hot reload ----

static const char kCubeObj[] =
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "f 1 2 3\n"
    "f 1 3 4\n"
    "f 5 6 7\n"
    "f 5 7 8\n"
    "f 1 2 6\n"
    "f 1 6 5\n"
    "f 2 3 7\n"
    "f 2 7 6\n"
    "f 3 4 8\n"
    "f 3 8 7\n"
    "f 4 1 5\n"
    "f 4 5 8\n";

TEST_CASE("SceneLoader.load.buildsGameObjectTreeWithRenderables") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader", true));
    REQUIRE(fs->setupWriteDirectory());
    const char *name = "sl_cube.obj";
    fs->write(name, kCubeObj, sizeof(kCubeObj) - 1);

    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto      *loader = SceneLoader::create();
    SceneHost *h      = loader->load(name);
    REQUIRE(h != nullptr);
    // Root + one mesh child = 2 GameObjects.
    CHECK_GE(loader->nodeCount(name), 2);
    CHECK(loader->loaded(name));
    CHECK(loader->host(name) == h);

    // Exactly one mesh GameObject with a linked Renderable3D.
    std::vector<SceneNode *> linked = h->findAllLinked();
    REQUIRE_EQ(linked.size(), 1u);
    REQUIRE_EQ(linked[0]->links.size(), 1u);
    CHECK(linked[0]->links[0].kind == eve::scene::findLinkKind("renderable3d"));
    auto *r = static_cast<eve::graphics::Renderable3D *>(linked[0]->links[0].target);
    REQUIRE(r != nullptr);
    CHECK(r->meshRenderer()->mesh != nullptr);

    // Hot-reload with an identical file is a fast no-op.
    SceneDiff out;
    auto      unchanged = loader->reload(name, &out);
    REQUIRE(unchanged.ok());
    CHECK(!unchanged.value());
    CHECK(out.empty());

    // Overwrite with a two-cube scene (two `o` objects) -> diff detects an add.
    const std::string twoCubes = std::string("o cubeA\n") + kCubeObj + "o cubeB\n" + kCubeObj;
    fs->write(name, twoCubes.data(), twoCubes.size());
    auto changed = loader->reload(name, &out);
    REQUIRE(changed.ok());
    REQUIRE(changed.value());
    CHECK_GT(out.added, 0);
    // A second (unchanged) mesh object still has a linked Renderable3D.
    std::vector<SceneNode *> linked2 = h->findAllLinked();
    REQUIRE_EQ(linked2.size(), 2u);

    loader->unload(name);
    CHECK(!loader->loaded(name));
    win->close();
}

TEST_CASE("SceneLoader.bounds.fillFromMeshAndUnion") {
    // Minimal Assimp scene: node "tank" with one cube mesh child.
    // Heap-allocate every node so the Assimp destructors (which delete[] mesh
    // vertex/face arrays and the scene/root node) stay well-defined.
    auto *scene    = new aiScene{};
    auto *rootNode = new aiNode{};
    auto *mesh     = new aiMesh{};
    auto *face     = new aiFace[1];
    auto *cube     = new aiVector3D[8]{
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
    };
    auto *meshIdx         = new unsigned[1]{0};
    auto *meshPtrs        = new aiMesh *[1];
    meshPtrs[0]           = mesh;
    mesh->mNumVertices    = 8;
    mesh->mVertices       = cube;
    mesh->mNumFaces       = 1;
    mesh->mFaces          = face;
    rootNode->mName       = "tank";
    rootNode->mNumMeshes  = 1;
    rootNode->mMeshes     = meshIdx;
    scene->mNumMeshes     = 1;
    scene->mMeshes        = meshPtrs;
    scene->mRootNode      = rootNode;
    scene->mNumMaterials  = 0;
    scene->mMaterials     = nullptr;
    scene->mNumAnimations = 0;

    MeshSlotMap slots;
    NodeDesc    root = SceneLoader::buildNodeDesc(scene, &slots);
    REQUIRE(slots.count("tank_mesh0") == 1u);

    SceneHost *h = sceneValue(SceneHost::createHost("boundstest"));
    h->setTree(std::move(root));
    SceneLoader::fillSceneBounds(h, slots);

    SceneNode *meshN = sceneNode(h, "tank_mesh0");
    REQUIRE(meshN != nullptr);
    CHECK(meshN->hasBounds);
    CHECK(approx(meshN->bminX, -1.f));
    CHECK(approx(meshN->bminY, -1.f));
    CHECK(approx(meshN->bmaxZ, 1.f));

    // ancestor union: mesh child has identity TRS → same local AABB
    SceneNode *tank = sceneNode(h, "tank");
    REQUIRE(tank != nullptr);
    CHECK(tank->hasBounds);
    CHECK(approx(tank->bminX, -1.f));
    CHECK(approx(tank->bmaxZ, 1.f));

    // moving a child merges (bounds are conservative and never shrink)
    meshN->x = 5.f;
    SceneLoader::fillSceneBounds(h, slots);
    CHECK(approx(tank->bminX, -1.f));
    CHECK(approx(tank->bmaxX, 6.f));

    // fresh host: transformed child union lands in parent local space
    SceneHost *h2 = sceneValue(SceneHost::createHost("boundstest2"));
    h2->setTree(node("tank", {node("tank_mesh0").withPosition(5.f, 0.f, 0.f)}));
    SceneLoader::fillSceneBounds(h2, slots);
    SceneNode *tank2 = sceneNode(h2, "tank");
    REQUIRE(tank2 != nullptr);
    CHECK(approx(tank2->bminX, 4.f));
    CHECK(approx(tank2->bmaxX, 6.f));

    // hot-reload path (null slots) keeps bounds on kept nodes
    NodeDesc  newRoot = node("tank", {node("tank_mesh0")});
    SceneDiff d       = SceneLoader::diffTree(h, newRoot);
    REQUIRE(SceneLoader::applyTreeDiff(h, newRoot, d, nullptr, nullptr));
    SceneNode *kept = sceneNode(h, "tank_mesh0");
    REQUIRE(kept != nullptr);
    CHECK(kept->hasBounds);
    CHECK(approx(kept->bminX, -1.f));

    delete scene;  // frees rootNode + mesh pointer arrays; mesh dtor frees verts/faces
}

// ---- PBR material + texture cache + lights/cameras via a self-contained glTF ----

TEST_CASE("SceneLoader.load.pbrMaterialAndLightsAndCamera") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_pbr", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string gltf = buildPbrGltf();
    fs->write("sl_pbr.gltf", gltf.data(), gltf.size());

    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto                         *loader = SceneLoader::create();
    eve::sceneloader::LoadOptions opts;
    opts.importCameras = true;
    SceneHost *h       = loader->load("sl_pbr.gltf", opts);
    REQUIRE(h != nullptr);

    // PBR factors landed on the linked Renderable3D.
    std::vector<SceneNode *> linked = h->findAllLinked();
    REQUIRE_EQ(linked.size(), 1u);
    auto *r = static_cast<eve::graphics::Renderable3D *>(linked[0]->links[0].target);
    REQUIRE(r != nullptr);
    auto mr = r->meshRenderer();
    CHECK(approx(mr->r, 0.2f, 1e-3f));
    CHECK(approx(mr->g, 0.4f, 1e-3f));
    CHECK(approx(mr->b, 0.6f, 1e-3f));
    CHECK(approx(mr->metallic, 0.9f, 1e-3f));
    CHECK(approx(mr->roughness, 0.25f, 1e-3f));

    // Lights + cameras imported.
    CHECK(loader->lightCount("sl_pbr.gltf") == 1);
    CHECK(loader->light("sl_pbr.gltf", 0) != nullptr);
    CHECK(loader->cameraCount("sl_pbr.gltf") == 1);
    eve::graphics::Camera3D *cam = loader->camera("sl_pbr.gltf", 0);
    REQUIRE(cam != nullptr);
    CHECK(!cam->data()->active);

    loader->unload("sl_pbr.gltf");
    CHECK(!loader->loaded("sl_pbr.gltf"));
    win->close();
}

TEST_CASE("SceneLoader.load.pbrTextureCacheReusesTexture") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_texcache", true));
    REQUIRE(fs->setupWriteDirectory());

    // Encode a small PNG through the image module (PNG is the only decoder).
    auto *img = eve::image::Image::create()->newImageData(4, 4, "RGBA8");
    REQUIRE(img != nullptr);
    REQUIRE(img->encode(medialoader::FormatHandler::ENCODED_PNG, "sl_cache.png", true) != nullptr);

    // Two objects share one material + one map_Kd -> the same GPU Texture must
    // be reused for both meshes (cache keyed by path + wrap mode).
    const char kObj[] =
        "mtllib sl_cache.mtl\n"
        "o a\n"
        "v -1 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "usemtl m\n"
        "f 1/1 2/2 3/3\n"
        "o b\n"
        "v -1 -1 0\n"
        "v 1 -1 0\n"
        "v 0 0 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "usemtl m\n"
        "f 4/4 5/5 6/6\n";
    const char kMtl[] = "newmtl m\nKd 1 1 1\nmap_Kd sl_cache.png\n";
    fs->write("sl_cache.obj", kObj, sizeof(kObj) - 1);
    fs->write("sl_cache.mtl", kMtl, sizeof(kMtl) - 1);

    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto      *loader = SceneLoader::create();
    SceneHost *h      = loader->load("sl_cache.obj");
    REQUIRE(h != nullptr);
    auto linked = h->findAllLinked();
    REQUIRE_EQ(linked.size(), 2u);
    auto *r0 = static_cast<eve::graphics::Renderable3D *>(linked[0]->links[0].target);
    auto *r1 = static_cast<eve::graphics::Renderable3D *>(linked[1]->links[0].target);
    REQUIRE(r0 != nullptr);
    REQUIRE(r1 != nullptr);
    REQUIRE(r0->meshRenderer()->texture != nullptr);
    // Shared texture instance across meshes that reference the same image.
    const bool sharedTexture = r0->meshRenderer()->texture == r1->meshRenderer()->texture;
    CHECK(sharedTexture);

    // Reload path stays healthy (diff-based) and reuses the same cached texture.
    SceneDiff out;
    auto      unchanged = loader->reload("sl_cache.obj", &out);
    REQUIRE(unchanged.ok());
    CHECK(!unchanged.value());
    CHECK(out.empty());

    loader->unload("sl_cache.obj");
    win->close();
}

TEST_CASE("SceneLoader.load.gltfCesiumManPbrAndAnimation") {
    const std::string dir = pathBesideThisSource("assets/skinned/cesium_man/glTF");
    auto             *fs  = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_cesium", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));

    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto                         *loader = SceneLoader::create();
    eve::sceneloader::LoadOptions opts;
    opts.importCameras = true;
    opts.mipmaps       = true;
    SceneHost *h       = loader->load("CesiumMan.gltf", opts);
    REQUIRE(h != nullptr);
    CHECK(loader->nodeCount("CesiumMan.gltf") > 0);

    std::vector<SceneNode *> linked = h->findAllLinked();
    REQUIRE(!linked.empty());
    auto *r = static_cast<eve::graphics::Renderable3D *>(linked[0]->links[0].target);
    REQUIRE(r != nullptr);
    // CesiumMan material: roughnessFactor = 1.0 (engine default is 0.45) and a
    // baseColorTexture — both must have been imported.
    CHECK(approx(r->meshRenderer()->roughness, 1.f, 1e-3f));
    CHECK(r->meshRenderer()->texture != nullptr);

    // Skinned character ships an animation; skeleton + clips must be imported.
    CHECK(loader->animationCount("CesiumMan.gltf") >= 1);
    REQUIRE(loader->skeleton("CesiumMan.gltf") != nullptr);
    CHECK(loader->clip("CesiumMan.gltf", 0) != nullptr);

    loader->unload("CesiumMan.gltf");
    win->close();
}

// ---- async loading ----

TEST_CASE("SceneLoader.loadAsync.mountsOnPoll") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_async", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->write("sl_async.obj", kCubeObj, sizeof(kCubeObj) - 1);

    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto      *loader = SceneLoader::create();
    bool       called = false;
    SceneHost *cbHost = nullptr;
    REQUIRE(loader->loadAsync("sl_async.obj", eve::sceneloader::LoadOptions{}, [&](SceneHost *h) {
        called = true;
        cbHost = h;
    }));

    int applied = 0;
    for (int i = 0; i < 100 && applied == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        applied = loader->pollAsync();
    }
    CHECK(applied == 1);
    CHECK(called);
    REQUIRE(cbHost != nullptr);
    CHECK(loader->host("sl_async.obj") == cbHost);
    CHECK(loader->nodeCount("sl_async.obj") >= 2);
    CHECK(loader->pendingAsyncCount() == 0);

    loader->unload("sl_async.obj");
    win->close();
}

TEST_CASE("SceneLoader.loadAsync.precodesTextures") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_asynctex", true));
    REQUIRE(fs->setupWriteDirectory());
    auto *img = eve::image::Image::create()->newImageData(4, 4, "RGBA8");
    REQUIRE(img != nullptr);
    REQUIRE(img->encode(medialoader::FormatHandler::ENCODED_PNG, "sl_at.png", true) != nullptr);
    const char kObj[] =
        "mtllib sl_at.mtl\n"
        "o a\n"
        "v -1 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "usemtl m\n"
        "f 1/1 2/2 3/3\n";
    const char kMtl[] = "newmtl m\nKd 1 1 1\nmap_Kd sl_at.png\n";
    fs->write("sl_at.obj", kObj, sizeof(kObj) - 1);
    fs->write("sl_at.mtl", kMtl, sizeof(kMtl) - 1);

    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto      *loader = SceneLoader::create();
    bool       called = false;
    SceneHost *cbHost = nullptr;
    REQUIRE(loader->loadAsync("sl_at.obj", eve::sceneloader::LoadOptions{}, [&](SceneHost *h) {
        called = true;
        cbHost = h;
    }));
    int applied = 0;
    for (int i = 0; i < 100 && applied == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        applied = loader->pollAsync();
    }
    CHECK(applied == 1);
    CHECK(called);
    REQUIRE(cbHost != nullptr);
    auto linked = cbHost->findAllLinked();
    REQUIRE_EQ(linked.size(), 1u);
    auto *r = static_cast<eve::graphics::Renderable3D *>(linked[0]->links[0].target);
    REQUIRE(r != nullptr);
    // Texture pre-decoded off-thread and uploaded on the main thread.
    CHECK(r->meshRenderer()->texture != nullptr);

    loader->unload("sl_at.obj");
    win->close();
}

TEST_CASE("SceneLoader.prewarmAsync.reusesDecodedScene") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_prewarm", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->write("sl_prewarm.obj", kCubeObj, sizeof(kCubeObj) - 1);

    auto *loader = SceneLoader::create();
    REQUIRE(loader->prewarmAsync("sl_prewarm.obj"));
    int applied = 0;
    for (int i = 0; i < 100 && applied == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        applied = loader->pollAsync();
    }
    CHECK(applied == 1);
    CHECK(loader->prewarmed("sl_prewarm.obj"));

    SceneHost *host = loader->load("sl_prewarm.obj", false);
    REQUIRE(host != nullptr);
    CHECK(!loader->prewarmed("sl_prewarm.obj"));
    CHECK(loader->nodeCount("sl_prewarm.obj") >= 2);
    loader->unload("sl_prewarm.obj");
}
}  // namespace test_sceneloader
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_spritestack)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "spritestack/SpriteStack.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// spritestack.cpp
namespace test_spritestack {
using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::image::ImageData;
using eve::spritestack::SpriteStack;
using eve::spritestack::SpriteStack2D;
using eve::spritestack::SpriteStackBatch;
using Colorf = ImageData::Colorf;

namespace {
int countAlpha(const ImageData *img) {
    int count = 0;
    for (int y = 0; y < img->getHeight(); ++y)
        for (int x = 0; x < img->getWidth(); ++x)
            if (img->getPixel(x, y).a > 0.5f) ++count;
    return count;
}

std::string testOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out"; }

void savePng(ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    [[maybe_unused]] auto *const               imageModule = eve::image::Image::create();
    std::unique_ptr<eve::filesystem::FileData> png(
        frame->encode(ImageData::FormatHandler::ENCODED_PNG, "spritestack2d.png", false));
    REQUIRE(png.get() != nullptr);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char *>(png->getData()), std::streamsize(png->getSize()));
    REQUIRE(out.good());
}

struct WindowFixture {
    eve::window::Window *win = eve::window::Window::create();
    Graphics            *gfx = Graphics::create();
    WindowFixture() {
        REQUIRE(win != nullptr);
        REQUIRE(gfx != nullptr);
        eve::window::WindowSettings settings;
        settings.width    = 320;
        settings.height   = 240;
        settings.centered = true;
        REQUIRE(win->setWindowSettings(settings));
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColor(Color(0.07f, 0.09f, 0.12f, 1.f));
    }
    ~WindowFixture() { win->close(); }
};

std::unique_ptr<ImageData> renderStack(Graphics *gfx, const SpriteStack2D &stack) {
    for (int i = 0; i < 3; ++i) {
        gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        stack.render(gfx);
        gfx->present();
    }
    return std::unique_ptr<ImageData>(gfx->newImageData());
}
}  // namespace

TEST_CASE("spritestack.slice.boxLayers") {
    auto layers = SpriteStack::create()->slicePrimitive("box", 6, 48, 48, "y", 0.f);
    REQUIRE_EQ(layers.size(), size_t(6));
    for (ImageData *layer : layers) {
        REQUIRE(layer != nullptr);
        CHECK_GT(countAlpha(layer), 100);
        delete layer;
    }
}

TEST_CASE("spritestack.slice.sphereVariesAcrossLayers") {
    auto layers = SpriteStack::create()->slicePrimitive("sphere", 9, 64, 64, "y", 0.f);
    REQUIRE_EQ(layers.size(), size_t(9));
    const int edge   = countAlpha(layers.front());
    const int middle = countAlpha(layers[layers.size() / 2]);
    CHECK_GT(middle, edge);
    for (ImageData *layer : layers) delete layer;
}

TEST_CASE("spritestack.render.pure2DStack") {
    WindowFixture fx;
    auto          layers = SpriteStack::create()->slicePrimitive("cone", 14, 64, 64, "y", 0.f);
    SpriteStack2D stack;
    stack.setLayerCount(int(layers.size()));
    for (int i = 0; i < int(layers.size()); ++i) stack.setLayerImage(fx.gfx, layers[size_t(i)], i);
    stack.setPosition(160.f, 155.f);
    stack.setSize(96.f, 96.f);
    stack.setThickness(3.f);
    stack.setRotation(25.f);
    stack.setTint(0.44f, 0.78f, 0.58f, 1.f);
    stack.setShadowEnabled(true);
    stack.setShadowOffset(8.f, 6.f);
    stack.setOutline(1.5f, 0.02f, 0.03f, 0.04f);

    auto frame = renderStack(fx.gfx, stack);
    REQUIRE(frame.get() != nullptr);
    savePng(frame.get(), testOutDir() + "/sprite_stack_2d.png");
    int changed = 0;
    for (int y = 30; y < 210; ++y) {
        for (int x = 60; x < 260; ++x) {
            const Colorf pixel = frame->getPixel(x, y);
            if (pixel.g > 0.20f && pixel.g > pixel.r * 1.15f) ++changed;
        }
    }
    CHECK_GT(changed, 500);
    for (ImageData *layer : layers) delete layer;
}

TEST_CASE("spritestack.render.rotationChangesPixels") {
    WindowFixture fx;
    auto          layers = SpriteStack::create()->slicePrimitive("box", 8, 48, 48, "y", 0.f);
    SpriteStack2D stack;
    stack.setLayerCount(int(layers.size()));
    for (int i = 0; i < int(layers.size()); ++i) stack.setLayerImage(fx.gfx, layers[size_t(i)], i);
    stack.setPosition(160.f, 140.f);
    stack.setSize(100.f, 68.f);
    stack.setThickness(3.f);
    auto before = renderStack(fx.gfx, stack);
    stack.setRotation(37.f);
    auto after = renderStack(fx.gfx, stack);
    int  diff  = 0;
    for (int y = 0; y < before->getHeight(); ++y)
        for (int x = 0; x < before->getWidth(); ++x) {
            const Colorf a = before->getPixel(x, y);
            const Colorf b = after->getPixel(x, y);
            if (std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b) > 0.08f) ++diff;
        }
    CHECK_GT(diff, 300);
    for (ImageData *layer : layers) delete layer;
}

TEST_CASE("spritestack.render.batchUses2DStacks") {
    WindowFixture fx;
    auto          layers = SpriteStack::create()->slicePrimitive("sphere", 8, 48, 48, "y", 0.f);
    SpriteStack2D left;
    SpriteStack2D right;
    for (SpriteStack2D *stack : {&left, &right}) {
        stack->setLayerCount(int(layers.size()));
        for (int i = 0; i < int(layers.size()); ++i) stack->setLayerImage(fx.gfx, layers[size_t(i)], i);
        stack->setSize(70.f, 70.f);
        stack->setThickness(2.f);
    }
    left.setPosition(105.f, 145.f);
    right.setPosition(215.f, 145.f);
    right.setRotation(30.f);
    SpriteStackBatch batch;
    batch.add(&left);
    batch.add(&right);
    CHECK_EQ(batch.getStackCount(), 2);
    for (int i = 0; i < 3; ++i) {
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        batch.render(fx.gfx);
        fx.gfx->present();
    }
    std::unique_ptr<ImageData> frame(fx.gfx->newImageData());
    savePng(frame.get(), testOutDir() + "/sprite_stack_2d_batch.png");
    for (ImageData *layer : layers) delete layer;
}
}  // namespace test_spritestack
#endif
