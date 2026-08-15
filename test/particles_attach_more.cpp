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

#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

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
    std::printf("particles.attach_more: missing %s\n", gltf.c_str());
    return false;
}

eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = cesiumManDir();
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile("CesiumMan.gltf");
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
    { "name": "arm", "parent": "root", "x": 20 },
    { "name": "hand", "parent": "arm", "x": 20 }
  ],
  "slots": [ { "name": "arm", "bone": "arm", "attachment": "arm" } ],
  "skins": [ { "name": "default", "attachments": {
    "arm": { "arm": { "width": 10, "height": 10 } }
  }} ],
  "animations": {
    "spin": {
      "bones": {
        "arm": {
          "rotate": [
            { "time": 0, "value": 0 },
            { "time": 0.5, "value": 90 }
          ]
        }
      }
    }
  }
})JSON";

}  // namespace

TEST_CASE("particles.attach.emitterLifeExpiresWhileAttached") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(10.f, 20.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(200.f);
    e->setEmitterLifetime(0.05f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    CHECK(e->isActive());

    ParticleSimSystem::update(0.06f);
    CHECK(!e->isActive());
    const int n = e->getCount();
    CHECK_GE(n, 1);

    // Further updates while attached must not spawn more after life ends.
    sk->setPosition(0, 99.f, 99.f);
    sk->forwardKinematics();
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(e->getCount(), n);
    // Attach sync still updates origin even when inactive.
    CHECK(std::fabs(e->getX() - sk->getX(0)) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.bufferCapUnderRateOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setParticleLifetime(10.f, 10.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(1000.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    ParticleSimSystem::update(1.f);
    CHECK_EQ(e->getCount(), 8);
    CHECK_EQ(e->getBufferSize(), 8);
    e->detach();
}

TEST_CASE("particles.attach.rebindDifferentBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int mid = sk->createBone(0, 25.f);
    int tip = sk->createBone(mid, 25.f);
    sk->initStraightPose(0.f, 0.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->attachToSkeleton2D(sk.get(), mid);
    CHECK_EQ(e->getAttachBone(), mid);
    CHECK(std::fabs(e->getX() - sk->getX(mid)) < 1e-2f);

    e->attachToSkeleton2D(sk.get(), tip);
    CHECK_EQ(e->getAttachBone(), tip);
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1e-2f);
    CHECK(std::fabs(e->getX() - sk->getX(mid)) > 1.f);
    e->detach();
}

TEST_CASE("particles.attach.zeroAndNegativeScale") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 2.f, 3.f, 0.f);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->attachToBone(pose.get(), root);
    e->setAttachScale(0.f);
    CHECK(std::fabs(e->getX()) < 1e-4f);
    CHECK(std::fabs(e->getY()) < 1e-4f);

    e->setAttachScale(-2.f);
    CHECK(std::fabs(e->getX() - (-4.f)) < 1e-3f);
    CHECK(std::fabs(e->getY() - (-6.f)) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.stopStartPreservesAttach") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    sk->initStraightPose(1.f, 2.f, 3.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(80.f);
    e->setAttachPlane("xy");
    e->setAttachScale(1.f);
    e->attachToSkeleton3D(sk.get(), 0);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 3);

    e->stop();
    CHECK(e->isStopped());
    CHECK(e->isAttached());
    const int n = e->getCount();
    ParticleSimSystem::update(0.05f);
    CHECK_EQ(e->getCount(), n);

    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GT(e->getCount(), n);
    CHECK_EQ(e->getAttachKind(), std::string("ik3d"));
    e->detach();
}

TEST_CASE("particles.attach.emissionAreaAroundBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(100.f, 100.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionArea("rect", 10.f, 5.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(40);

    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < e->getCount(); ++i) {
        const auto &p = e->sim()->particles[size_t(i)];
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
        CHECK(p.x >= 90.f - 1e-2f);
        CHECK(p.x <= 110.f + 1e-2f);
        CHECK(p.y >= 95.f - 1e-2f);
        CHECK(p.y <= 105.f + 1e-2f);
    }
    CHECK_GT(maxX - minX, 5.f);
    CHECK_GT(maxY - minY, 2.f);
    e->detach();
}

TEST_CASE("particles.attach.followRotationAffectsSpawnVelocity") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    // 90° Z: +X → +Y
    const float half = 0.70710678f;
    pose->setLocalRotation(root, 0.f, 0.f, half, half);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(10.f, 10.f);
    e->setSpread(0.f);
    e->setFollowBoneRotation(true);
    e->attachToBone(pose.get(), root);
    e->emit(1);
    CHECK_EQ(e->getCount(), 1);
    const auto &p = e->sim()->particles[0];
    // Direction ≈ π/2 → velocity mostly +Y
    CHECK(std::fabs(p.vx) < 1.f);
    CHECK(p.vy > 8.f);
    e->detach();
}

TEST_CASE("particles.attach.radialForceUsesMovingOrigin") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setRadialAcceleration(20.f, 20.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(1);
    // Place particle away from origin manually.
    e->sim()->particles[0].x = 10.f;
    e->sim()->particles[0].y = 0.f;

    ParticleSimSystem::update(0.1f);
    // Radial push away from Config origin (0,0) → +vx
    CHECK(e->sim()->particles[0].vx > 0.f);

    // Move bone far to the right; radial origin follows → pull/push relative to new origin.
    sk->setPosition(0, 100.f, 0.f);
    sk->forwardKinematics();
    e->sim()->particles[0].vx = 0.f;
    e->sim()->particles[0].vy = 0.f;
    e->sim()->particles[0].x = 10.f;  // left of new origin 100
    e->sim()->particles[0].y = 0.f;
    ParticleSimSystem::update(0.1f);
    // Away from origin at 100: particle at 10 → direction -X → negative vx
    CHECK(e->sim()->particles[0].vx < 0.f);
    e->detach();
}

TEST_CASE("particles.attach.getAttachBoneAfterDetach") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(2);
    e->attachToSkeleton2D(sk.get(), 0);
    CHECK_EQ(e->getAttachBone(), 0);
    e->detach();
    CHECK_EQ(e->getAttachBone(), -1);
    CHECK_EQ(e->getAttachKind(), std::string("none"));
}

TEST_CASE("particles.attach.stableWhenPoseUnchanged") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 5.f, 6.f, 7.f);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setAttachPlane("xz");
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), root);
    const float x0 = e->getX();
    const float y0 = e->getY();
    for (int i = 0; i < 10; ++i) ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - x0) < 1e-5f);
    CHECK(std::fabs(e->getY() - y0) < 1e-5f);
    // xz of (5,6,7) → (5,7)
    CHECK(std::fabs(x0 - 5.f) < 1e-3f);
    CHECK(std::fabs(y0 - 7.f) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.spineRotatedParentOffset") {
    auto *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton> spine(animMod->newSpineSkeleton(data.get()));
    std::unique_ptr<SpineAnim> anim(animMod->newSpineAnim(spine.get()));
    REQUIRE(anim->play("spin"));
    anim->setTime(0.5f);
    anim->apply();

    const int hand = data->findBone("hand");
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setAttachOffset(0.f, 5.f, 0.f);
    e->attachToSpineBone(spine.get(), hand);

    float a, b, c, d;
    spine->getBoneWorldMatrix(hand, a, b, c, d);
    const float ex = spine->getBoneWorldX(hand) + b * 5.f;
    const float ey = spine->getBoneWorldY(hand) + d * 5.f;
    CHECK(std::fabs(e->getX() - ex) < 1e-2f);
    CHECK(std::fabs(e->getY() - ey) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.ik2dSolverStepTracksTip") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 40.f);
    int tip = sk->createBone(b1, 40.f);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(4);
    solver->setForce(0.5f);
    solver->addTarget(tip, 50.f, 30.f, 1.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->attachToSkeleton2D(sk.get(), tip);

    float prev = e->getX() + e->getY();
    int moved = 0;
    for (int i = 0; i < 8; ++i) {
        solver->step(sk.get(), 1.f);
        ParticleSimSystem::update(0.016f);
        const float cur = e->getX() + e->getY();
        if (std::fabs(cur - prev) > 0.1f) ++moved;
        prev = cur;
        CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1e-2f);
        CHECK(std::fabs(e->getY() - sk->getY(tip)) < 1e-2f);
    }
    CHECK_GE(moved, 1);
    e->detach();
}

TEST_CASE("particles.attach.ik3dOffsetAlongBoneProjected") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int tip = sk->createBone(0, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);
    // tip at (1,0,0), orientation +X

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setAttachPlane("xy");
    e->setAttachScale(1.f);
    e->setAttachOffset(2.f, 0.f, 0.f);  // +2 along bone → world (3,0,0)
    e->attachToSkeleton3D(sk.get(), tip);
    CHECK(std::fabs(e->getX() - 3.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 0.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.animByNameAfterDetach") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    sk->addBone("Hips", -1);
    const int head = sk->addBone("Head", 0);
    sk->setBindPosition(head, 0.f, 2.f, 0.f);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->attachToBoneByName(pose.get(), sk.get(), "Head");
    CHECK(e->isAttached());
    e->detach();
    e->attachToBoneByName(pose.get(), sk.get(), "Head");
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachBone(), head);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.presetStillWorksWhenAttached") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(40.f, 50.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    e->attachToSkeleton2D(sk.get(), 0);
    e->applyPreset("spark");
    // Preset must not clear attach.
    CHECK(e->isAttached());
    CHECK(std::fabs(e->getX() - 40.f) < 1e-2f);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 1);
    e->detach();
}

TEST_CASE("particles.attach.emitZeroAndNegativeNoOp") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(0);
    e->emit(-5);
    CHECK_EQ(e->getCount(), 0);
    e->detach();
}

TEST_CASE("particles.skin.planeXzYzCesiumMan") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_skin_planes"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose> pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());
    REQUIRE(skin->updateSkinnedPositions(pose.get()));

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setSkinScale(10.f);
    e->setSkinSource(skin.get(), pose.get());

    e->setSkinPlane("xz");
    e->emitFromSkin(32);
    CHECK_EQ(e->getCount(), 32);
    float spreadXZ = 0.f;
    {
        float minX = e->sim()->particles[0].x, maxX = minX;
        float minY = e->sim()->particles[0].y, maxY = minY;
        for (int i = 0; i < e->getCount(); ++i) {
            const auto &p = e->sim()->particles[size_t(i)];
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        spreadXZ = (maxX - minX) + (maxY - minY);
    }
    CHECK_GT(spreadXZ, 0.5f);

    e->reset();
    e->setSkinPlane("yz");
    e->emitFromSkin(32);
    CHECK_EQ(e->getCount(), 32);
    float spreadYZ = 0.f;
    {
        float minX = e->sim()->particles[0].x, maxX = minX;
        float minY = e->sim()->particles[0].y, maxY = minY;
        for (int i = 0; i < e->getCount(); ++i) {
            const auto &p = e->sim()->particles[size_t(i)];
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        spreadYZ = (maxX - minX) + (maxY - minY);
    }
    CHECK_GT(spreadYZ, 0.5f);
    e->clearSkinSource();
    e->detach();
}

TEST_CASE("particles.skin.clearRestoresBoneOriginSpawn") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_skin_clear"));
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose> pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->setLocalPosition(0, 1.f, 2.f, 0.f);
    pose->computeWorld(skeleton.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), 0);
    e->setSkinScale(1.f);
    e->setSkinSource(skin.get(), pose.get());
    CHECK(e->hasSkinSource());
    CHECK(e->isAttached());

    e->clearSkinSource();
    CHECK(!e->hasSkinSource());
    CHECK(e->isAttached());
    e->emit(4);
    CHECK_EQ(e->getCount(), 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::fabs(e->sim()->particles[size_t(i)].x - e->getX()) < 1e-2f);
        CHECK(std::fabs(e->sim()->particles[size_t(i)].y - e->getY()) < 1e-2f);
    }
    e->detach();
}

TEST_CASE("particles.attach.manualSyncAttachWithoutSim") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(1.f, 2.f);
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(2);
    e->attachToSkeleton2D(sk.get(), 0);
    sk->setPosition(0, 8.f, 9.f);
    sk->forwardKinematics();
    // Without ParticleSimSystem — only manual sync.
    e->syncAttach();
    CHECK(std::fabs(e->getX() - 8.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 9.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.followRotationToggleResyncs") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    const float half = 0.70710678f;
    pose->setLocalRotation(root, 0.f, 0.f, half, half);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(2);
    e->setDirection(0.1f);
    e->setFollowBoneRotation(false);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getDirection() - 0.1f) < 1e-4f);

    e->setFollowBoneRotation(true);  // triggers syncAttach
    CHECK(std::fabs(e->getDirection() - float(M_PI * 0.5)) < 0.05f);

    e->setFollowBoneRotation(false);
    e->setDirection(0.3f);
    e->syncAttach();
    CHECK(std::fabs(e->getDirection() - 0.3f) < 1e-4f);
    e->detach();
}
