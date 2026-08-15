#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "particles/Particles.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"

#include "animation/Animation.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/SpineAnim.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"

#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"

#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace eve::particles;
using namespace eve::animation;
using namespace eve::ik;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

bool fileExists(const std::string &path) { return std::filesystem::is_regular_file(path); }

std::string cesiumManDir() {
    return pathBesideThisSource("assets/skinned/cesium_man/glTF");
}

bool ensureSkinnedAssets() {
    const std::string gltf = cesiumManDir() + "/CesiumMan.gltf";
    if (fileExists(gltf)) return true;
    std::printf("particles.attach_extra: missing %s\n", gltf.c_str());
    return false;
}

eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = cesiumManDir();
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    return eve::model3d::Model3D::create()->newModelDataFromFile("CesiumMan.gltf");
}

int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

const char *kSpineJson = R"JSON({
  "skeleton": { "spine": "4.1.00" },
  "bones": [
    { "name": "root" },
    { "name": "hip", "parent": "root", "y": 10 },
    { "name": "chest", "parent": "hip", "y": 20 },
    { "name": "head", "parent": "chest", "y": 15 }
  ],
  "slots": [ { "name": "chest", "bone": "chest", "attachment": "c" } ],
  "skins": [ { "name": "default", "attachments": {
    "chest": { "c": { "width": 8, "height": 8 } }
  }} ],
  "animations": {
    "bob": {
      "bones": {
        "chest": {
          "translate": [
            { "time": 0, "x": 0, "y": 0 },
            { "time": 0.5, "x": 0, "y": 12 },
            { "time": 1.0, "x": 0, "y": 0 }
          ]
        }
      }
    }
  }
})JSON";

}  // namespace

TEST_CASE("particles.attach.resetClearsParticlesKeepsAttach") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(11.f, 22.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(8);
    CHECK_EQ(e->getCount(), 8);
    CHECK(e->isAttached());

    e->reset();
    CHECK_EQ(e->getCount(), 0);
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - 11.f) < 1e-2f);

    // The emitter lives in a process-wide ECS; detach before the local
    // skeleton is destroyed so later ParticleSimSystem::update() calls in
    // other tests don't dereference the freed skeleton.
    e->detach();
}

TEST_CASE("particles.attach.moveToOverwrittenBySync") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(5.f, 6.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->attachToSkeleton2D(sk.get(), 0);
    e->moveTo(999.f, 888.f);
    CHECK(std::fabs(e->getX() - 999.f) < 1e-3f);

    ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - 5.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 6.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.applyConfigPreservesAttach") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(30.f, 40.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    e->attachToSkeleton2D(sk.get(), 0);
    CHECK(e->applyConfig(R"({"emissionRate":40,"particleLifetime":[1,1],"speed":[0,0]})"));
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - 30.f) < 1e-2f);
    CHECK(std::fabs(e->getEmissionRate() - 40.f) < 1e-3f);

    e->start();
    ParticleSimSystem::update(0.1f);
    CHECK_GE(e->getCount(), 3);
    e->detach();
}

TEST_CASE("particles.attach.layerAndVisibleIndependent") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    sk->initStraightPose(1.f, 2.f, 3.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setAttachPlane("xy");
    e->attachToSkeleton3D(sk.get(), 0);
    e->setLayer(7);
    e->setVisible(false);
    CHECK_EQ(e->getLayer(), 7);
    CHECK(!e->isVisible());
    CHECK(e->isAttached());
    ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - 1.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.ellipseAreaOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(50.f, 50.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(80);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionArea("ellipse", 8.f, 4.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(60);

    int inside = 0;
    for (int i = 0; i < e->getCount(); ++i) {
        const auto &p = e->sim()->particles[size_t(i)];
        const float dx = (p.x - 50.f) / 8.f;
        const float dy = (p.y - 50.f) / 4.f;
        if (dx * dx + dy * dy <= 1.01f) ++inside;
    }
    CHECK_EQ(inside, e->getCount());
    e->detach();
}

TEST_CASE("particles.attach.tangentialForceUsesBoneOrigin") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setTangentialAcceleration(30.f, 30.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(1);
    e->sim()->particles[0].x = 5.f;
    e->sim()->particles[0].y = 0.f;
    e->sim()->particles[0].vx = 0.f;
    e->sim()->particles[0].vy = 0.f;

    ParticleSimSystem::update(0.1f);
    // Tangential to radial (+X) is +Y (rdx,rdy)=(1,0) → (-rdy, rdx)=(0,1)
    CHECK(e->sim()->particles[0].vy > 0.f);
    e->detach();
}

TEST_CASE("particles.attach.particleLifetimeExpiresOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(0.05f, 0.05f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(0.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(5);
    CHECK_EQ(e->getCount(), 5);
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(e->getCount(), 0);
    CHECK(e->isAttached());
    e->detach();
}

TEST_CASE("particles.attach.largeDtBurstOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(7.f, 8.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(100);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(50.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    ParticleSimSystem::update(0.5f);  // ~25 particles
    CHECK_GE(e->getCount(), 20);
    CHECK_LE(e->getCount(), 30);
    for (int i = 0; i < e->getCount(); ++i) {
        CHECK(std::fabs(e->sim()->particles[size_t(i)].x - 7.f) < 1e-2f);
        CHECK(std::fabs(e->sim()->particles[size_t(i)].y - 8.f) < 1e-2f);
    }
    e->detach();
}

TEST_CASE("particles.attach.zeroDtStillSyncs") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(1.f, 1.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(100.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    e->emit(2);
    CHECK_EQ(e->getCount(), 2);

    sk->setPosition(0, 15.f, 16.f);
    sk->forwardKinematics();
    ParticleSimSystem::update(0.f);  // sync runs; sim early-outs
    CHECK(std::fabs(e->getX() - 15.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 16.f) < 1e-2f);
    CHECK_EQ(e->getCount(), 2);  // no new spawns at dt=0
    e->detach();
}

TEST_CASE("particles.attach.cycleThroughAllKinds") {
    std::unique_ptr<AnimSkeleton> ask(new AnimSkeleton());
    const int root = ask->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    ask->applyBindPose(pose.get());
    pose->setLocalPosition(root, 1.f, 0.f, 0.f);
    pose->computeWorld(ask.get());

    auto *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton> spine(animMod->newSpineSkeleton(data.get()));
    spine->setToSetupPose();
    spine->updateWorldTransform();

    std::unique_ptr<Skeleton2D> sk2(new Skeleton2D());
    sk2->initStraightPose(3.f, 4.f);
    std::unique_ptr<Skeleton3D> sk3(new Skeleton3D());
    sk3->initStraightPose(5.f, 6.f, 7.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);

    e->attachToBone(pose.get(), root);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));
    e->attachToSpineBoneByName(spine.get(), "head");
    CHECK_EQ(e->getAttachKind(), std::string("spine"));
    CHECK(e->attach()->pose == nullptr);
    e->attachToSkeleton2D(sk2.get(), 0);
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(e->attach()->spine == nullptr);
    e->attachToSkeleton3D(sk3.get(), 0);
    CHECK_EQ(e->getAttachKind(), std::string("ik3d"));
    CHECK(e->attach()->ik2d == nullptr);
    e->detach();
    CHECK_EQ(e->getAttachKind(), std::string("none"));
}

TEST_CASE("particles.attach.scaleChangeMidLifetimeResyncs") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 2.f, 4.f, 0.f);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getX() - 2.f) < 1e-3f);

    e->setAttachScale(3.f);
    CHECK(std::fabs(e->getX() - 6.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 12.f) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.spinAndSizeVariationOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setSpin(1.f, 3.f);
    e->setSizeVariation(0.5f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(20);

    bool spinOk = false;
    bool sizeVaried = false;
    float firstSize = e->sim()->particles[0].size;
    for (int i = 0; i < e->getCount(); ++i) {
        const auto &p = e->sim()->particles[size_t(i)];
        if (p.spin >= 1.f && p.spin <= 3.f) spinOk = true;
        if (std::fabs(p.size - firstSize) > 1e-4f) sizeVaried = true;
    }
    CHECK(spinOk);
    CHECK(sizeVaried);
    e->detach();
}

TEST_CASE("particles.attach.ik2dConstraintsStillEmits") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 40.f);
    int tip = sk->createBone(b1, 40.f);
    sk->initStraightPose(0.f, 0.f);
    sk->setConstraints(b1, -0.2f, 0.2f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(32);
    solver->addTarget(tip, 0.f, 80.f, 1.f);
    solver->solve(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->attachToSkeleton2D(sk.get(), tip);
    e->emit(5);
    CHECK_EQ(e->getCount(), 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(std::fabs(e->sim()->particles[size_t(i)].x - sk->getX(tip)) < 1e-1f);
        CHECK(std::fabs(e->sim()->particles[size_t(i)].y - sk->getY(tip)) < 1e-1f);
    }
    e->detach();
}

TEST_CASE("particles.attach.twoEmittersDifferentPlanes") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    sk->initStraightPose(2.f, 4.f, 6.f);

    auto *mod = Particles::create();
    ParticleEmitter *xy = mod->newEmitter(4);
    ParticleEmitter *xz = mod->newEmitter(4);
    xy->setAttachPlane("xy");
    xz->setAttachPlane("xz");
    xy->setAttachScale(1.f);
    xz->setAttachScale(1.f);
    xy->attachToSkeleton3D(sk.get(), 0);
    xz->attachToSkeleton3D(sk.get(), 0);
    CHECK(std::fabs(xy->getX() - 2.f) < 1e-3f);
    CHECK(std::fabs(xy->getY() - 4.f) < 1e-3f);
    CHECK(std::fabs(xz->getX() - 2.f) < 1e-3f);
    CHECK(std::fabs(xz->getY() - 6.f) < 1e-3f);
    xy->detach();
    xz->detach();
}

TEST_CASE("particles.attach.animChainOffsetThreeBones") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int a = sk->addBone("a", -1);
    const int b = sk->addBone("b", a);
    const int c = sk->addBone("c", b);
    sk->setBindPosition(b, 1.f, 0.f, 0.f);
    sk->setBindPosition(c, 1.f, 0.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(a, 10.f, 0.f, 0.f);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setAttachOffset(0.5f, 0.f, 0.f);
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), c);
    // world c = 10+1+1=12, + local offset 0.5 → 12.5
    CHECK(std::fabs(e->getX() - 12.5f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 0.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.spineBobAnimContinuousRate") {
    auto *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton> spine(animMod->newSpineSkeleton(data.get()));
    std::unique_ptr<SpineAnim> anim(animMod->newSpineAnim(spine.get()));
    REQUIRE(anim->play("bob"));

    const int chest = data->findBone("chest");
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(128);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(100.f);
    e->attachToSpineBone(spine.get(), chest);
    e->start();

    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < 10; ++i) {
        anim->setTime(float(i) * 0.1f);
        anim->apply();
        ParticleSimSystem::update(0.05f);
        minY = std::min(minY, e->getY());
        maxY = std::max(maxY, e->getY());
    }
    CHECK_GE(e->getCount(), 20);
    CHECK_GT(maxY - minY, 5.f);
    e->detach();
}

TEST_CASE("particles.attach.ik3dSolverStepContinuous") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int b1 = sk->createBone(0, 1.f);
    int tip = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);

    std::unique_ptr<Solver3D> solver(new Solver3D());
    solver->setMaxIterations(2);
    solver->setForce(0.4f);
    solver->addTarget(tip, 1.f, 0.5f, 0.f, 1.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setAttachPlane("xy");
    e->attachToSkeleton3D(sk.get(), tip);

    float prev = e->getX();
    int moved = 0;
    for (int i = 0; i < 10; ++i) {
        solver->step(sk.get(), 1.f);
        e->syncAttach();
        if (std::fabs(e->getX() - prev) > 1e-3f) ++moved;
        prev = e->getX();
        CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1e-2f);
    }
    CHECK_GE(moved, 1);
    e->detach();
}

TEST_CASE("particles.skin.filterByIndexAndClearFilter") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_skin_filter_idx"));
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose> pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setSkinScale(50.f);
    e->setSkinSource(skin.get(), pose.get());

    const int skinBone = 0;
    e->setSkinBoneFilter(skinBone, 0.05f);
    e->emitFromSkin(16);
    CHECK_GE(e->getCount(), 1);

    e->reset();
    e->setSkinBoneFilter(-1, 0.f);  // clear filter → all verts
    e->emitFromSkin(32);
    CHECK_EQ(e->getCount(), 32);
    e->clearSkinSource();
    e->detach();
}

TEST_CASE("particles.skin.emitFromSkinRespectsBuffer") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_skin_buffer"));
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose> pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(10);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setSkinScale(1.f);
    e->setSkinSource(skin.get(), pose.get());
    e->emitFromSkin(100);
    CHECK_EQ(e->getCount(), 10);
    CHECK_EQ(e->getBufferSize(), 10);
    e->clearSkinSource();
    e->detach();
}

TEST_CASE("particles.attach.cesiumManBoneByNameIfPresent") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_attach_byname"));
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    REQUIRE(skeleton->getBoneCount() > 1);

    // Collect any bone name and attach by name.
    const std::string name = skeleton->getBoneName(1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setAttachScale(10.f);
    e->attachToBoneByName(pose.get(), skeleton.get(), name);
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachBone(), 1);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));

    e->attachToBoneByName(pose.get(), skeleton.get(), "__no_such_bone__");
    CHECK(!e->isAttached());
}

TEST_CASE("particles.attach.smokeAndFirePresetsOnIk3d") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    sk->initStraightPose(0.f, 1.f, 0.f);

    auto *mod = Particles::create();
    for (const char *preset : {"smoke", "fire"}) {
        ParticleEmitter *e = mod->newEmitter(64);
        e->setAttachPlane("xy");
        e->setAttachScale(1.f);
        e->attachToSkeleton3D(sk.get(), 0);
        e->applyPreset(preset);
        CHECK(e->isAttached());
        CHECK(e->getEmissionRate() > 0.f);
        e->start();
        ParticleSimSystem::update(0.05f);
        CHECK_GE(e->getCount(), 1);
        e->detach();
    }
}
