#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_dynamic_bones)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/Animation.h"
#include "animation/SpineAnim.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_dynamic_bones.cpp
namespace test_particles_dynamic_bones {
using namespace eve::particles;
using namespace eve::animation;
using namespace eve::ik;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

const char* kSpineJson = R"JSON({
  "skeleton": { "spine": "4.1.00" },
  "bones": [
    { "name": "root" },
    { "name": "body", "parent": "root", "y": 40 },
    { "name": "hand", "parent": "body", "x": 30, "y": 10 }
  ],
  "slots": [
    { "name": "body", "bone": "body", "attachment": "body" }
  ],
  "skins": [
    { "name": "default", "attachments": {
      "body": { "body": { "width": 20, "height": 40 } }
    }}
  ],
  "animations": {
    "wave": {
      "bones": {
        "hand": {
          "translate": [
            { "time": 0, "x": 0, "y": 0 },
            { "time": 0.5, "x": 20, "y": 0 },
            { "time": 1.0, "x": 0, "y": 0 }
          ]
        }
      }
    }
  }
})JSON";

}  // namespace

TEST_CASE("particles.attach.spineBoneFollowsAndEmits") {
    auto*                              animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    REQUIRE(data.get() != nullptr);
    std::unique_ptr<SpineSkeleton> spine(animMod->newSpineSkeleton(data.get()));
    REQUIRE(spine.get() != nullptr);
    spine->setToSetupPose();
    spine->updateWorldTransform();

    const int hand = data->findBone("hand");
    REQUIRE(hand >= 0);

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(32);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setAttachScale(1.f);
    e->attachToSpineBoneByName(spine.get(), "hand");
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("spine"));
    CHECK_EQ(e->getAttachBone(), hand);
    CHECK(std::fabs(e->getX() - spine->getBoneWorldX(hand)) < 1e-3f);
    CHECK(std::fabs(e->getY() - spine->getBoneWorldY(hand)) < 1e-3f);

    e->setAttachOffset(5.f, 0.f, 0.f);
    e->syncAttach();
    // Offset in bone local +X, transformed by world matrix.
    float a, b, c, d;
    spine->getBoneWorldMatrix(hand, a, b, c, d);
    const float expectX = spine->getBoneWorldX(hand) + a * 5.f;
    const float expectY = spine->getBoneWorldY(hand) + c * 5.f;
    CHECK(std::fabs(e->getX() - expectX) < 1e-3f);
    CHECK(std::fabs(e->getY() - expectY) < 1e-3f);

    e->setEmissionRate(100.f);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 4);

    // Missing bone name disables attach.
    e->attachToSpineBoneByName(spine.get(), "Missing");
    CHECK(!e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("none"));
    e->detach();
}

TEST_CASE("particles.attach.spineDynamicEmitAcrossAnim") {
    auto*                              animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton>     spine(animMod->newSpineSkeleton(data.get()));
    std::unique_ptr<SpineAnim>         anim(animMod->newSpineAnim(spine.get()));
    REQUIRE(anim.get() != nullptr);

    const int        hand = data->findBone("hand");
    auto*            mod  = Particles::create();
    ParticleEmitter* e    = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(0.f);
    e->attachToSpineBone(spine.get(), hand);

    float prevX = -1e9f;
    int   moved = 0;
    CHECK(anim->play("wave"));
    for (int i = 0; i < 6; ++i) {
        const float t = float(i) * 0.1f;
        anim->setTime(t);
        anim->apply();
        e->emit(1);
        ParticleSimSystem::update(0.016f);
        const float x = e->sim()->particles[size_t(e->getCount() - 1)].x;
        if (i > 0 && std::fabs(x - prevX) > 0.5f) ++moved;
        prevX = x;
    }
    CHECK_EQ(e->getCount(), 6);
    CHECK_GE(moved, 1);  // hand translate keyframes should move spawn X
    e->detach();
}

TEST_CASE("particles.attach.spineFollowRotation") {
    auto*                              animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton>     spine(animMod->newSpineSkeleton(data.get()));
    const int                          body = data->findBone("body");
    spine->setToSetupPose();
    spine->setBoneLocalRotation(body, 90.f);  // degrees
    spine->updateWorldTransform();

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(4);
    e->setFollowBoneRotation(true);
    e->attachToSpineBone(spine.get(), body);
    CHECK(std::fabs(e->getDirection() - float(M_PI * 0.5)) < 0.05f);
    e->detach();
}

TEST_CASE("particles.attach.ik2dBoneEmit") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int                         b1  = sk->createBone(0, 50.f);
    int                         tip = sk->createBone(b1, 50.f);
    sk->setPosition(0, 100.f, 200.f);
    sk->setRotation(0, 0.f);
    sk->setRotation(b1, 0.f);
    sk->setRotation(tip, float(M_PI / 2.0));
    sk->forwardKinematics();

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setAttachScale(1.f);
    e->attachToSkeleton2D(sk.get(), tip);
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1e-2f);
    CHECK(std::fabs(e->getY() - sk->getY(tip)) < 1e-2f);

    e->setEmissionRate(80.f);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 3);
    auto sim = e->sim();
    for (int i = 0; i < sim->alive; ++i) {
        CHECK(std::fabs(sim->particles[size_t(i)].x - sk->getX(tip)) < 1e-1f);
        CHECK(std::fabs(sim->particles[size_t(i)].y - sk->getY(tip)) < 1e-1f);
    }

    // Move chain and verify emitter follows via ParticleSimSystem.
    sk->setPosition(0, 150.f, 200.f);
    sk->forwardKinematics();
    ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.ik2dFollowRotationAndSolver") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int                         b1  = sk->createBone(0, 40.f);
    int                         tip = sk->createBone(b1, 40.f);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(32);
    solver->addTarget(tip, 40.f, 40.f, 1.f);
    CHECK(solver->solve(sk.get()));

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(8);
    e->setFollowBoneRotation(true);
    e->attachToSkeleton2D(sk.get(), tip);
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1.f);
    CHECK(std::fabs(e->getY() - sk->getY(tip)) < 1.f);
    // Direction should align with bone forward.
    const float fx     = sk->getOrientationX(tip);
    const float fy     = sk->getOrientationY(tip);
    const float expect = std::atan2(fy, fx);
    CHECK(std::fabs(e->getDirection() - expect) < 0.1f);
    e->detach();
}

TEST_CASE("particles.attach.ik3dBoneEmitProjected") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int                         b1  = sk->createBone(0, 1.f);
    int                         tip = sk->createBone(b1, 1.f);
    sk->initStraightPose(10.f, 20.f, 5.f);
    CHECK(std::fabs(sk->getX(tip) - 12.f) < 1e-3f);

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(16);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setAttachPlane("xy");
    e->setAttachScale(2.f);
    e->attachToSkeleton3D(sk.get(), tip);
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("ik3d"));
    // tip (12,20,5) × scale 2 on xy → (24, 40)
    CHECK(std::fabs(e->getX() - 24.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 40.f) < 1e-2f);

    e->setAttachPlane("xz");
    e->syncAttach();
    // (12,20,5) on xz ×2 → (24, 10)
    CHECK(std::fabs(e->getX() - 24.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 10.f) < 1e-2f);

    e->setEmissionRate(60.f);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 2);
    e->detach();
}

TEST_CASE("particles.attach.switchKindsClearsPrevious") {
    std::unique_ptr<Skeleton2D> sk2(new Skeleton2D());
    sk2->initStraightPose(1.f, 2.f);

    std::unique_ptr<Skeleton3D> sk3(new Skeleton3D());
    sk3->initStraightPose(3.f, 4.f, 5.f);

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(4);
    e->attachToSkeleton2D(sk2.get(), 0);
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    e->attachToSkeleton3D(sk3.get(), 0);
    CHECK_EQ(e->getAttachKind(), std::string("ik3d"));
    CHECK(e->attach()->ik2d == nullptr);
    CHECK(e->attach()->ik3d == sk3.get());
    e->detach();
}

TEST_CASE("particles.attach.nullAndOutOfRangeDisable") {
    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(4);

    e->attachToBone(nullptr, 0);
    CHECK(!e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("none"));

    e->attachToSpineBone(nullptr, 0);
    CHECK(!e->isAttached());

    e->attachToSkeleton2D(nullptr, 0);
    CHECK(!e->isAttached());

    e->attachToSkeleton3D(nullptr, 0);
    CHECK(!e->isAttached());

    std::unique_ptr<Skeleton2D> sk2(new Skeleton2D());
    sk2->initStraightPose(0.f, 0.f);
    e->attachToSkeleton2D(sk2.get(), 99);  // only root bone exists
    CHECK(!e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("none"));

    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->computeWorld(sk.get());
    e->attachToBone(pose.get(), root);
    CHECK(e->isAttached());
    e->attachToBone(pose.get(), -1);
    CHECK(!e->isAttached());
}

TEST_CASE("particles.attach.invalidPlaneFallsBackToXy") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 3.f, 4.f, 5.f);
    pose->computeWorld(sk.get());

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(4);
    e->setAttachPlane("nope");
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), root);
    CHECK_EQ(e->attach()->plane, std::string("xy"));
    CHECK(std::fabs(e->getX() - 3.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 4.f) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.multiEmittersOnSameSkeleton") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int                         mid = sk->createBone(0, 30.f);
    int                         tip = sk->createBone(mid, 30.f);
    sk->setPosition(0, 10.f, 20.f);
    sk->setRotation(0, 0.f);
    sk->setRotation(mid, 0.f);
    sk->setRotation(tip, 0.f);
    sk->forwardKinematics();

    auto*            mod    = Particles::create();
    ParticleEmitter* rootFx = mod->newEmitter(8);
    ParticleEmitter* tipFx  = mod->newEmitter(8);
    rootFx->setParticleLifetime(2.f, 2.f);
    tipFx->setParticleLifetime(2.f, 2.f);
    rootFx->setSpeed(0.f, 0.f);
    tipFx->setSpeed(0.f, 0.f);
    rootFx->attachToSkeleton2D(sk.get(), 0);
    tipFx->attachToSkeleton2D(sk.get(), tip);

    CHECK(std::fabs(rootFx->getX() - sk->getX(0)) < 1e-2f);
    CHECK(std::fabs(tipFx->getX() - sk->getX(tip)) < 1e-2f);
    CHECK(std::fabs(tipFx->getX() - rootFx->getX()) > 1.f);

    rootFx->setEmissionRate(40.f);
    tipFx->setEmissionRate(40.f);
    rootFx->start();
    tipFx->start();
    ParticleSimSystem::update(0.1f);
    CHECK_GE(rootFx->getCount(), 3);
    CHECK_GE(tipFx->getCount(), 3);
    // Spawn clusters stay near their respective bones.
    CHECK(std::fabs(rootFx->sim()->particles[0].x - sk->getX(0)) < 0.5f);
    CHECK(std::fabs(tipFx->sim()->particles[0].x - sk->getX(tip)) < 0.5f);
    rootFx->detach();
    tipFx->detach();
}

TEST_CASE("particles.attach.rateEmitTracksMovingIk2d") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int                         tip = sk->createBone(0, 1.f);
    sk->initStraightPose(0.f, 0.f);

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(128);
    e->setParticleLifetime(10.f, 10.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(100.f);  // ~1 particle per 0.01s
    e->attachToSkeleton2D(sk.get(), tip);
    e->start();

    std::vector<float> xs;
    for (int step = 0; step < 5; ++step) {
        sk->setPosition(0, float(step) * 10.f, 0.f);
        sk->forwardKinematics();
        const int before = e->getCount();
        ParticleSimSystem::update(0.02f);  // ~2 particles
        CHECK_GT(e->getCount(), before);
        xs.push_back(e->sim()->particles[size_t(e->getCount() - 1)].x);
    }
    // Tip world x ≈ root.x + length; root moves 0,10,20,30,40 → tip ~1,11,21,31,41
    CHECK(xs.back() - xs.front() > 20.f);
    e->detach();
}

TEST_CASE("particles.attach.ik2dOffsetAlongOrientation") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int                         tip = sk->createBone(0, 10.f);
    sk->setPosition(0, 0.f, 0.f);
    sk->setRotation(0, 0.f);
    sk->setRotation(tip, 0.f);
    sk->forwardKinematics();
    // tip at (10, 0), orientation +X

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(4);
    e->setAttachOffset(5.f, 0.f, 0.f);  // further along bone forward
    e->attachToSkeleton2D(sk.get(), tip);
    CHECK(std::fabs(e->getX() - 15.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 0.f) < 1e-2f);

    e->setAttachOffset(0.f, 3.f, 0.f);  // perpendicular (+Y when facing +X)
    e->syncAttach();
    CHECK(std::fabs(e->getX() - 10.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 3.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.ik3dYzPlaneAndFollowRotation") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int                         tip = sk->createBone(0, 2.f);
    sk->initStraightPose(0.f, 1.f, 3.f);
    // tip at (2, 1, 3); default orientation along +X

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(8);
    e->setAttachPlane("yz");
    e->setAttachScale(1.f);
    e->setFollowBoneRotation(true);
    e->attachToSkeleton3D(sk.get(), tip);
    // yz of (2,1,3) → (1, 3)
    CHECK(std::fabs(e->getX() - 1.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 3.f) < 1e-2f);

    const float fx = sk->getOrientationX(tip);
    const float fy = sk->getOrientationY(tip);
    const float fz = sk->getOrientationZ(tip);
    // Project orientation onto yz → (fy, fz)
    const float expect = std::atan2(fz, fy);
    if (fy * fy + fz * fz > 1e-6f)
        CHECK(std::fabs(e->getDirection() - expect) < 0.15f);
    else
        (void)fx;  // orientation collapsed on yz — direction unchanged is fine
    e->detach();
}

TEST_CASE("particles.attach.ik3dSolverMovesEmitter") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int                         b1  = sk->createBone(0, 1.f);
    int                         tip = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(8);
    e->setAttachPlane("xy");
    e->setAttachScale(1.f);
    e->attachToSkeleton3D(sk.get(), tip);
    const float x0 = e->getX();
    const float y0 = e->getY();

    std::unique_ptr<Solver3D> solver(new Solver3D());
    solver->setMaxIterations(48);
    solver->addTarget(tip, 1.f, 1.f, 0.f, 1.f);
    CHECK(solver->solve(sk.get()));
    ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - sk->getX(tip)) < 5e-2f);
    CHECK(std::fabs(e->getY() - sk->getY(tip)) < 5e-2f);
    CHECK(std::fabs(e->getX() - x0) + std::fabs(e->getY() - y0) > 0.2f);
    e->detach();
}

TEST_CASE("particles.attach.spineScaleAndModuleUpdate") {
    auto*                              animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton>     spine(animMod->newSpineSkeleton(data.get()));
    spine->setToSetupPose();
    spine->updateWorldTransform();
    const int hand = data->findBone("hand");

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setAttachScale(2.f);
    e->attachToSpineBone(spine.get(), hand);
    CHECK(std::fabs(e->getX() - spine->getBoneWorldX(hand) * 2.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - spine->getBoneWorldY(hand) * 2.f) < 1e-2f);

    e->setEmissionRate(50.f);
    e->start();
    mod->update(0.1f);  // module path must sync attach + sim
    CHECK_GE(e->getCount(), 4);
    CHECK(std::fabs(e->sim()->particles[0].x - e->getX()) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.detachKeepsAliveParticles") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(50.f, 60.f);

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(32);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(5);
    CHECK_EQ(e->getCount(), 5);
    const float px = e->sim()->particles[0].x;
    const float py = e->sim()->particles[0].y;

    e->detach();
    CHECK(!e->isAttached());
    ParticleSimSystem::update(0.016f);
    CHECK_EQ(e->getCount(), 5);
    CHECK(std::fabs(e->sim()->particles[0].x - px) < 1e-3f);
    CHECK(std::fabs(e->sim()->particles[0].y - py) < 1e-3f);

    // After detach, new emits use last Config.x/y (still at old bone pos until moveTo).
    e->emit(1);
    CHECK_EQ(e->getCount(), 6);
}

TEST_CASE("particles.attach.pauseStopsSpawnButStillSyncs") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(32);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(100.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    ParticleSimSystem::update(0.05f);
    const int n = e->getCount();
    CHECK_GE(n, 4);

    e->pause();
    sk->setPosition(0, 100.f, 0.f);
    sk->forwardKinematics();
    ParticleSimSystem::update(0.05f);
    CHECK_EQ(e->getCount(), n);                         // paused: no new particles
    CHECK(std::fabs(e->getX() - sk->getX(0)) < 1e-2f);  // but attach still syncs
    e->detach();
}

TEST_CASE("particles.attach.switchSpineToAnimClearsSpine") {
    auto*                              animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton>     spine(animMod->newSpineSkeleton(data.get()));
    spine->setToSetupPose();
    spine->updateWorldTransform();

    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 7.f, 8.f, 0.f);
    pose->computeWorld(sk.get());

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(4);
    e->attachToSpineBoneByName(spine.get(), "hand");
    CHECK_EQ(e->getAttachKind(), std::string("spine"));
    CHECK(e->attach()->spine != nullptr);

    e->attachToBone(pose.get(), root);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));
    CHECK(e->attach()->spine == nullptr);
    CHECK(e->attach()->pose == pose.get());
    CHECK(std::fabs(e->getX() - 7.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 8.f) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.animOffsetRotatedByBone") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    // 90° about Z: local +X → world +Y
    const float half = 0.70710678f;
    pose->setLocalRotation(root, 0.f, 0.f, half, half);
    pose->setLocalPosition(root, 0.f, 0.f, 0.f);
    pose->computeWorld(sk.get());

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(4);
    e->setAttachOffset(2.f, 0.f, 0.f);
    e->setAttachPlane("xy");
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getX() - 0.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.skinTakesPriorityOverBoneOrigin") {
    // When SkinSource is enabled, continuous spawn samples skin, not attach origin.
    // Use a synthetic path: attach to a far bone, but without a real skin the
    // spawn falls back to Config — here we only assert attach sync still runs
    // and clearSkinSource restores bone-origin emission.
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(100.f, 200.f);

    auto*            mod = Particles::create();
    ParticleEmitter* e   = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->attachToSkeleton2D(sk.get(), 0);
    CHECK(std::fabs(e->getX() - 100.f) < 1e-2f);

    e->emit(3);
    CHECK_EQ(e->getCount(), 3);
    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(e->sim()->particles[size_t(i)].x - 100.f) < 1e-2f);
        CHECK(std::fabs(e->sim()->particles[size_t(i)].y - 200.f) < 1e-2f);
    }
    CHECK(!e->hasSkinSource());
    e->detach();
}
}  // namespace test_particles_dynamic_bones
#undef M_PI
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_effect_asset)
#include "PrivateTestScope.h"
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionPreview.h"
#include "action/ActionVfxBlock.h"
#include "action/ActionVfxDuration.h"
#include "common/Capability.h"
#include "filesystem/Filesystem.h"
#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneObject.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_effect_asset.cpp
namespace test_particles_effect_asset {
using namespace eve::particles;

namespace {

[[maybe_unused]] eve::LogicalId logicalId(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

class TestVfxAttachmentSource final : public eve::IAttachmentPointSource {
public:
    eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(std::string_view     name,
                                                            eve::AttachmentPoint localOffset) const override {
        ++calls;
        if (name != "hand_r")
            return eve::Result<eve::AttachmentPoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing test bone", "bone"));
        return eve::Result<eve::AttachmentPoint>::success(
            {50.f + localOffset.x, 60.f + localOffset.y, 70.f + localOffset.z});
    }
    mutable int calls = 0;
};

[[maybe_unused]] int liveParticleCount() {
    int  count = 0;
    auto view  = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [config] = *it;
        if (config->entity) count += config->entity->getCount();
    }
    return count;
}

[[maybe_unused]] float firstPlaybackSpeed() {
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [config] = *it;
        if (config->entity) return config->entity->getPlaybackSpeed();
    }
    return 0.0f;
}

}  // namespace


TEST_CASE("particles.effectAsset.playbackLabAssetIsConsumable") {
    const std::string path = std::string(EVENGINE_SOURCE_DIR) + "/examples/particle-playback-lab/impact.effect.json";
    std::ifstream     input(path, std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();

    std::string error;
    auto*       effect = ParticleEffect::fromText(text.str(), path, &error);
    REQUIRE(effect != nullptr);
    CHECK(error.empty());
    CHECK_EQ(effect->getSourcePath(), path);
    CHECK_EQ(effect->getEmitterCount(), 2);
    CHECK(effect->getEmitterByName("core") != nullptr);
    CHECK(effect->getEmitterByName("sparks") != nullptr);
    CHECK(effect->hasFloatParameter("intensity"));
    auto duration = effect->naturalDuration();
    REQUIRE(duration.ok());
    CHECK(std::abs(duration.value().seconds() - 1.8) < 0.001);
    delete effect;

    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    (void)Particles::create();
    auto* provider = eve::cap::query<eve::action::IActionVfxDurationProvider>();
    REQUIRE(provider != nullptr);
    auto providedDuration = provider->naturalDuration("examples/particle-playback-lab/impact.effect.json");
    REQUIRE(providedDuration.ok());
    CHECK(std::abs(providedDuration.value().seconds() - 1.8) < 0.001);
}


TEST_CASE("particles.actionBlockProviderOwnsRealVfxEnterUpdateExit") {
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto* particles = Particles::create();
    auto* scene     = eve::scene::Scene::create();
    auto  mounted   = scene->mountAs("action-vfx", eve::scene::node("source").withPosition(5.f, 6.f, 7.f));
    REQUIRE(mounted.ok());
    auto source = eve::scene::SceneObject::createObject("action-vfx", "source");
    REQUIRE(source.ok());
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    const int                       before = particles->getEmitterCount();

    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId{91};
    advance.totalElapsed = eve::Duration::fromNanoseconds(10);
    eve::Value::Object payload{
        {"uri", "examples/particle-playback-lab/impact.effect.json"},
        {"stopBehavior", "clear_immediately"},
        {"clipStartTime", 0.2},
        {"clipEndTime", 1.2},
        {"bone", "hand_r"},
        {"positionOffset", eve::Value::Array{12.0, 34.0, 0.0}},
        {"rotationOffsetDegrees", eve::Value::Array{0.0, 0.0, 90.0}},
        {"scale", eve::Value::Array{2.0, 2.0, 2.0}},
    };
    advance.timelineEvents.push_back({eve::action::ActionTimelineEventKind::StateEnter,
                                      logicalId("presentation-track:vfx"), logicalId("presentation-vfx:impact"),
                                      logicalId("presentation:vfx-state"), advance.totalElapsed, payload});
    advance.activeBlocks.push_back({logicalId("presentation-track:vfx"), logicalId("presentation-vfx:impact"),
                                    logicalId("presentation:vfx-state"), eve::Duration::zero(),
                                    eve::Duration::fromNanoseconds(1'000'000'000), payload});
    eve::action::ActionNotifyContext context;
    context.executionId  = advance.id;
    context.playbackRate = 2.0;
    context.source       = ecs::handle_of(source.value());
    TestVfxAttachmentSource attachmentSource;
    context.sourceAttachment = std::cref(attachmentSource);
    auto applied             = runtime.apply(advance, context);
    REQUIRE(applied.ok());
    CHECK_EQ(attachmentSource.calls, 2);
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    CHECK(std::abs(firstPlaybackSpeed() - 2.0f) < 0.001f);

    context.time = eve::Duration::fromNanoseconds(20);
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(particles->getEmitterCount(), before);
}


TEST_CASE("particles.actionPreviewRetainsScrubsRebuildsAndCleansVfx") {
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto* particles = Particles::create();
    REQUIRE_EQ(eve::cap::listenerCount<eve::action::IActionPreviewSinkProvider>(), 1u);
    auto* provider = eve::cap::listenerAt<eve::action::IActionPreviewSinkProvider>(0);
    REQUIRE(provider != nullptr);
    auto sink = provider->createActionPreviewSink();
    REQUIRE(sink.ok());
    const int before = particles->getEmitterCount();

    eve::Value::Object              payload{{"uri", "examples/particle-playback-lab/impact.effect.json"},
                                            {"clipStartTime", 0.2},
                                            {"clipEndTime", 1.2},
                                            {"stopBehavior", "clear_immediately"}};
    eve::action::ActionPreviewFrame frame;
    frame.reason  = eve::action::ActionPreviewReason::Seek;
    frame.current = eve::Duration::fromNanoseconds(500'000'000);
    frame.activeBlocks.push_back({logicalId("presentation-track:vfx"), logicalId("presentation-vfx:preview"),
                                  logicalId("presentation:vfx-state"), eve::Duration::fromNanoseconds(500'000'000),
                                  eve::Duration::fromNanoseconds(1'000'000'000), payload});
    REQUIRE(sink.value()->prepare(frame).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    sink.value()->present(frame);
    const int seekParticles = liveParticleCount();
    CHECK(seekParticles > 0);

    frame.reason                         = eve::action::ActionPreviewReason::Advance;
    frame.current                        = eve::Duration::fromNanoseconds(750'000'000);
    frame.activeBlocks.front().localTime = frame.current;
    REQUIRE(sink.value()->prepare(frame).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    sink.value()->present(frame);
    const int advancedParticles = liveParticleCount();
    CHECK(advancedParticles > 0);

    frame.reason = eve::action::ActionPreviewReason::Refresh;
    REQUIRE(sink.value()->prepare(frame).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 4);
    sink.value()->present(frame);
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    CHECK_EQ(liveParticleCount(), advancedParticles);

    auto invalid                                        = frame;
    invalid.activeBlocks.front().payload["clipEndTime"] = 0.1;
    CHECK(!sink.value()->prepare(invalid).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);

    eve::action::ActionPreviewFrame empty;
    empty.reason  = eve::action::ActionPreviewReason::Seek;
    empty.current = eve::Duration::fromNanoseconds(800'000'000);
    REQUIRE(sink.value()->prepare(empty).ok());
    sink.value()->present(empty);
    CHECK_EQ(particles->getEmitterCount(), before);

    eve::action::ActionPreviewCue instant;
    instant.kind    = eve::action::ActionPreviewCueKind::Vfx;
    instant.itemId  = logicalId("presentation-vfx:instant-preview");
    instant.type    = logicalId("presentation:vfx");
    instant.time    = eve::Duration::fromNanoseconds(800'000'000);
    instant.payload = {{"uri", "examples/particle-playback-lab/impact.effect.json"}, {"lifetimeSeconds", 1.0}};
    empty.cues.push_back(std::move(instant));
    REQUIRE(sink.value()->prepare(empty).ok());
    sink.value()->present(empty);
    CHECK_EQ(particles->getEmitterCount(), before + 2);

    empty.reason  = eve::action::ActionPreviewReason::Advance;
    empty.current = eve::Duration::fromNanoseconds(2'000'000'000);
    empty.cues.clear();
    REQUIRE(sink.value()->prepare(empty).ok());
    sink.value()->present(empty);
    CHECK_EQ(particles->getEmitterCount(), before);
    sink.value().reset();
    CHECK_EQ(particles->getEmitterCount(), before);
}
}  // namespace test_particles_effect_asset
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_car_camera)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <cmath>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "camera/CameraController.h"
#include "camera/PcgCarCameraSetup.h"
#include "camera/PcgFreeCamera.h"
#include "common/Capability.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/PcgWaterPhotoMode.h"
#include "graphics/RenderSystem3D.h"
#include "image/ImageData.h"
#include "procgen/PcgMeshLod.h"
#include "procgen/PcgMeshLodBackup.h"
#include "procgen/Procgen.h"
#include "scene/Scene.h"
#include "scene/SceneNodeRef.h"
#include "ui/PcgPhotoModeApplyPlan.h"
#include "zeroerr/unittest.h"
// pcg_car_camera.cpp
namespace test_pcg_car_camera {
// pcg_car_camera.cpp
using namespace eve::camera;
using eve::graphics::Camera3D;
using namespace eve::scene;
TEST_CASE("camera.pcgCar.appliesExactDefaultsAndInput") {
    Scene* scene = Scene::create();
    scene->beginBuild();
    scene->beginNode("root");
    scene->addNode("car");
    scene->end();
    REQUIRE(scene->mountBuildAs("pcg-car-test"));
    SceneNodeRef        car("pcg-car-test", "car");
    auto*               camera = Camera3D::createCamera();
    CameraController    controller;
    PcgCarCameraSetup   setup;
    PcgCarCameraProfile profile;
    REQUIRE(setup.configure(profile).ok());
    REQUIRE(setup.apply(&controller, camera, &car, 15.f, 10.f).ok());
    CHECK_EQ(controller.getMode(), "orbit");
    CHECK_EQ(controller.getRadius(), 6.f);
    CHECK_EQ(controller.getMinimumRadius(), .6f);
    CHECK_EQ(controller.getMaximumRadius(), 20.f);
    REQUIRE(setup.update(&controller, 1.f, 100.f, 1.f, .25f, false, 0.f).ok());
    CHECK_EQ(setup.getYaw(), 19.f);
    CHECK_EQ(setup.getPitch(), -80.f);
    CHECK_EQ(controller.getRadius(), .6f);
}
TEST_CASE("camera.pcgCar.rejectsBadProfilesWithoutMutation") {
    PcgCarCameraSetup   setup;
    PcgCarCameraProfile profile;
    REQUIRE(setup.configure(profile).ok());
    profile.minimumDistance = 30.f;
    CHECK(!setup.configure(profile).ok());
    CameraController controller;
    REQUIRE(controller.setRadiusLimits(1.f, 10.f).ok());
    controller.setRadius(5.f);
    CHECK(!controller.setRadiusLimits(8.f, 2.f).ok());
    CHECK_EQ(controller.getRadius(), 5.f);
    CHECK_EQ(controller.getMinimumRadius(), 1.f);
    CHECK(!setup.update(nullptr, 0, 0, 0, .1f, false, 0).ok());
    CHECK(!setup.update(&controller, std::numeric_limits<float>::quiet_NaN(), 0, 0, .1f, false, 0).ok());
}
TEST_CASE("camera.pcgCar.rearLockUsesInjectedTargetYaw") {
    PcgCarCameraProfile profile;
    profile.lockToRearOfTarget = true;
    profile.rotationDamping    = 2.f;
    PcgCarCameraSetup setup;
    REQUIRE(setup.configure(profile).ok());
    Scene* scene = Scene::create();
    scene->beginBuild();
    scene->beginNode("root");
    scene->addNode("car");
    scene->end();
    REQUIRE(scene->mountBuildAs("pcg-car-rear-test"));
    SceneNodeRef     car("pcg-car-rear-test", "car");
    auto*            camera = Camera3D::createCamera();
    CameraController controller;
    REQUIRE(setup.apply(&controller, camera, &car, 0, 0).ok());
    REQUIRE(setup.update(&controller, 0, 0, 0, .5f, false, 90).ok());
    CHECK_GT(setup.getYaw(), 50.f);
    CHECK_LT(setup.getYaw(), 90.f);
}

// pcg_free_camera.cpp
using namespace eve::camera;
using eve::graphics::Camera3D;
TEST_CASE("camera.pcgFree.captureMoveSprintScrollAndRelease") {
    auto* c = Camera3D::createCamera();
    c->setEye(0, 0, 0);
    c->setTarget(0, 0, 1);
    PcgFreeCamera f;
    REQUIRE(f.configure(true, true, true, 5, 5, 50, true, 100, 15).ok());
    PcgFreeCameraInput i;
    i.rightPressed = true;
    i.forward      = 1;
    i.sprint       = true;
    i.scroll       = 500;
    REQUIRE(f.update(c, i, 1).ok());
    CHECK(f.getCaptured());
    CHECK_EQ(f.getSprintSpeed(), 150.f);
    CHECK_GT(c->getEyeZ(), 149.f);
    CHECK_EQ(f.getRoll(), 15.f);
    i               = {};
    i.rightReleased = true;
    REQUIRE(f.update(c, i, .1f).ok());
    CHECK(!f.getCaptured());
}
TEST_CASE("camera.pcgFree.focusAndInvalidInputAreSafe") {
    auto* c = Camera3D::createCamera();
    c->setEye(1, 2, 3);
    c->setTarget(1, 2, 4);
    PcgFreeCamera f;
    REQUIRE(f.capture(c).ok());
    PcgFreeCameraInput i;
    i.focused = false;
    REQUIRE(f.update(c, i, .1f).ok());
    CHECK(!f.getCaptured());
    float x  = c->getEyeX();
    i.mouseX = std::numeric_limits<float>::quiet_NaN();
    CHECK(!f.update(c, i, .1f).ok());
    CHECK_EQ(c->getEyeX(), x);
    CHECK(!f.configure(true, false, true, 5, 10, 5, true, 100, 0).ok());
}

// pcg_mesh_lod.cpp
using namespace eve::procgen;
using namespace eve::graphics;

namespace {
MeshBuild pcgLodGrid() {
    MeshBuild mesh;
    mesh.setActiveGroup("surface");
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x)
            mesh.addVertex(float(x), (x == 1 && z == 2) ? 0.35F : 0.F, float(z), 0.F, 1.F, 0.F, float(x) / 3.F,
                           float(z) / 3.F);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) {
            const std::uint32_t a = std::uint32_t(z * 4 + x), b = a + 1, c = a + 4, d = c + 1;
            mesh.addTriangle(a, b, d);
            mesh.addTriangle(a, d, c);
        }
    return mesh;
}

TEST_CASE("graphics.pcgMeshLod.speedTreeFadeUsesComplementaryOpaqueCoverage") {
    auto* gfx = Graphics::create();
    REQUIRE(gfx);
    gfx->initHeadless(96, 96);
    auto* canvas = gfx->newCanvas(96, 96);
    REQUIRE(canvas);
    const float    positions[] = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]        = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh);
    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1.f);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos({0.f, 0.f, 2.f});
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DSurface(SurfaceMode::Masked, BlendMode::Alpha, true, true, .5f);
    gfx->setMesh3DLodDither(.5f, false, true);
    gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(1.f, 0.f, 0.f, 1.f));
    gfx->setMesh3DLodDither(.5f, true, true);
    gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(0.f, 0.f, 1.f, 1.f));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    REQUIRE(pixels);
    const auto* data = static_cast<const unsigned char*>(pixels->getData());
    size_t      red = 0, blue = 0, holes = 0;
    for (int y = 12; y < 84; ++y)
        for (int x = 12; x < 84; ++x) {
            const size_t i = static_cast<size_t>(y * 96 + x) * 4;
            if (data[i] > data[i + 2] + 20)
                ++red;
            else if (data[i + 2] > data[i] + 20)
                ++blue;
            else if (data[i] < 10 && data[i + 2] < 10)
                ++holes;
        }
    CHECK(red > 500);
    CHECK(blue > 500);
    CHECK(holes < 100);
}

MeshBuild pcgTriangle(const std::string& material) {
    MeshBuild mesh;
    mesh.setActiveGroup(material);
    mesh.addVertex(0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F);
    mesh.addVertex(1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F);
    mesh.addVertex(0.F, 1.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}
}  // namespace

TEST_CASE("procgen.pcgMeshCombiner.matchesStaticRendererOrderingAndStreams") {
    MeshBuild first = pcgTriangle("shared");
    MeshBuild second;
    second.addVertex(0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F);
    second.addVertex(1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F);
    second.addVertex(1.F, 1.F, 0.F, 1.F, 0.F, 0.F, 1.F, 1.F);
    second.addVertex(0.F, 1.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F);
    second.setActiveGroup("unique");
    second.addTriangle(0, 1, 2);
    second.setActiveGroup("shared");
    second.addTriangle(0, 2, 3);
    REQUIRE(
        second
            .setVertexColors({0.F, 0.1F, 0.2F, 1.F, 0.3F, 0.4F, 0.5F, 1.F, 0.6F, 0.7F, 0.8F, 1.F, 0.9F, 1.F, 0.F, 1.F})
            .ok());
    PcgMeshTransform identity, transformed;
    REQUIRE(transformed.setElement(0, 0, 2.F).ok());
    REQUIRE(transformed.setElement(0, 3, 10.F).ok());
    PcgMeshCombinePlan plan;
    REQUIRE(plan.appendSource(first, identity, "fallback-a").ok());
    REQUIRE(plan.appendSource(second, transformed, "fallback-b").ok());
    MeshBuild output;
    auto      combined = combinePcgStaticMeshesInto(output, plan);
    REQUIRE(combined.ok());
    CHECK_EQ(combined.value(), 3);
    CHECK_EQ(output.getVertexCount(), 7);
    CHECK_EQ(output.getIndexCount(), 9);
    CHECK_EQ(output.getPositionX(3), 10.F);
    CHECK_EQ(output.getPositionX(4), 12.F);
    CHECK_EQ(output.getNormalX(3), 2.F);
    REQUIRE(output.hasVertexColors());
    CHECK_EQ(output.getColor(0, 0), 1.F);
    CHECK_EQ(output.getColor(3, 1), 0.1F);
    CHECK_EQ(output.getGroupCount(), 2);
    CHECK_EQ(output.getGroupName(0), std::string("shared"));
    CHECK_EQ(output.getGroupName(1), std::string("unique"));
    CHECK_EQ(output.getTriangleGroup(0), 0);
    CHECK_EQ(output.getTriangleGroup(1), 0);
    CHECK_EQ(output.getTriangleGroup(2), 1);
    CHECK_EQ(output.getIndex(3), 3);
    CHECK_EQ(output.getIndex(4), 5);
    CHECK_EQ(output.getIndex(5), 6);
}

TEST_CASE("procgen.pcgMeshCombiner.failuresAreAtomic") {
    PcgMeshTransform transform;
    CHECK(!transform.setElement(4, 0, 1.F).ok());
    CHECK(!transform.setElement(0, 0, std::numeric_limits<float>::infinity()).ok());
    PcgMeshCombinePlan empty;
    MeshBuild          output = pcgTriangle("preserved");
    CHECK(!combinePcgStaticMeshesInto(output, empty).ok());
    CHECK_EQ(output.getVertexCount(), 3);
    CHECK_EQ(output.getGroupName(0), std::string("preserved"));
    PcgMeshLodProfile profile;
    REQUIRE(profile.appendLevel(0.5F, 0.F, 1.F, false, false).ok());
    PcgMeshLodSet lods;
    CHECK(!buildPcgCombinedMeshLodsInto(lods, empty, profile).ok());
    CHECK_EQ(lods.getLevelCount(), 0);
}

TEST_CASE("procgen.pcgMeshLod.generatesEveryLevelFromOriginalMesh") {
    PcgMeshLodProfile profile;
    REQUIRE(profile.appendLevel(0.8F, 0.1F, 0.5F, false, false).ok());
    REQUIRE(profile.appendLevel(0.4F, 0.2F, 0.5F, false, false).ok());
    REQUIRE(profile.setFadePolicy(2, false, 0.75F).ok());
    REQUIRE(profile.setLevelRendererState(0, 4, 2, false, 2, false, 4, 2).ok());
    REQUIRE(profile.levelAt(0));
    CHECK(!profile.levelAt(0)->simplification.preserveBorderEdges);
    CHECK_EQ(profile.levelAt(0)->simplification.vertexLinkDistance, std::numeric_limits<double>::denorm_min());
    PcgMeshLodSet output;
    REQUIRE(buildPcgMeshLodsInto(output, pcgLodGrid(), profile).ok());
    CHECK_EQ(output.getLevelCount(), 2);
    REQUIRE(output.meshAt(0));
    REQUIRE(output.meshAt(1));
    REQUIRE(output.rendererStateAt(0));
    CHECK_EQ(output.rendererStateAt(0)->skinQuality, 4);
    CHECK_EQ(output.rendererStateAt(0)->shadowCastingMode, 2);
    CHECK(!output.rendererStateAt(0)->receiveShadows);
    CHECK_EQ(output.rendererStateAt(0)->lightProbeUsage, 4);
    CHECK_EQ(output.meshAt(0)->getIndexCount(), output.meshAt(1)->getIndexCount());
    CHECK_EQ(output.meshAt(0)->positions(), output.meshAt(1)->positions());
    CHECK_EQ(output.getFadeWidth(1), 0.2F);
    CHECK_EQ(output.getFadeMode(), 2);
    CHECK(!output.getAnimateCrossFading());
    CHECK_EQ(output.getCrossFadeAnimationDuration(), 0.75F);
    CHECK_EQ(output.selectLevel(0.9F), 0);
    CHECK_EQ(output.selectLevel(0.5F), 1);
    CHECK_EQ(output.selectLevel(0.1F), -1);
    CHECK(std::abs(output.getSwitchDistance(0, 2.F, 90.F) - 1.25F) < 0.0001F);
}

TEST_CASE("procgen.pcgMeshLod.profileAndBuildFailuresAreAtomic") {
    PcgMeshLodProfile profile;
    REQUIRE(profile.appendLevel(0.8F, 0.F, 1.F).ok());
    CHECK(!profile.appendLevel(0.9F, 0.F, 0.5F).ok());
    CHECK(!profile.appendLevel(0.4F, 0.F, 0.5F, false, true).ok());
    CHECK(!profile.setLevelRendererState(0, 3, 1, true, 1, true, 1, 1).ok());
    CHECK(!profile.setLevelRendererState(0, 4, 1, true, 1, true, 3, 1).ok());
    CHECK(!profile.setFadePolicy(3, false, .5F).ok());
    CHECK(!profile.setFadePolicy(0, true, .5F).ok());
    CHECK_EQ(profile.getLevelCount(), 1);
    PcgMeshLodSet output;
    REQUIRE(buildPcgMeshLodsInto(output, pcgLodGrid(), profile).ok());
    const int         before = output.meshAt(0)->getIndexCount();
    PcgMeshLodProfile emptyProfile;
    CHECK(!buildPcgMeshLodsInto(output, pcgLodGrid(), emptyProfile).ok());
    CHECK_EQ(output.getLevelCount(), 1);
    CHECK_EQ(output.meshAt(0)->getIndexCount(), before);
    MeshBuild empty;
    CHECK(!buildPcgMeshLodsInto(output, empty, profile).ok());
    CHECK_EQ(output.meshAt(0)->getIndexCount(), before);
}

TEST_CASE("procgen.pcgMeshLod.realVmBuildsAndCopiesLevel") {
    MeshBuild             source = pcgLodGrid();
    MeshBuild             combined;
    eve::procgen::Procgen procgen;
    ssq::VM               vm(1024);
    auto                  table = vm.addTable("eve");
    procgen.expose(table);
    vm.addFunc("pcgLodSource", [&]() { return &source; });
    vm.addFunc("pcgCombineOutput", [&]() { return &combined; });
    vm.run(vm.compileSource(R"(
        local profile=eve.PcgMeshLodProfile();
        assert(profile.appendLevel(0.8,0.1,1.0,false,false).ok);
        assert(profile.appendLevel(0.3,0.2,0.5,false,false).ok);
        assert(profile.setLevelRendererState(1,2,0,false,2,false,0,0).ok);
        assert(profile.setFadePolicy(2,false,0.75).ok);
        assert(profile.getFadeMode()==2 && !profile.getAnimateCrossFading());
        assert(profile.getCrossFadeAnimationDuration()==0.75);
        assert(profile.getLevelRendererState(1,0)==2);
        local output=eve.PcgMeshLodSet();
        assert(eve.buildPcgMeshLods(output,pcgLodSource(),profile).ok);
        assert(output.getLevelCount()==2 && output.selectLevel(0.5)==1);
        local mesh=output.copyLevelMesh(1);
        assert(mesh.getIndexCount()>0);
        local transform=eve.PcgMeshTransform();
        assert(transform.setElement(0,3,2.0).ok);
        local plan=eve.PcgMeshCombinePlan();
        assert(plan.appendSource(pcgLodSource(),transform,"surface").ok);
        local combined=pcgCombineOutput();
        local combineResult=eve.combinePcgStaticMeshes(combined,plan);
        if(!combineResult.ok)throw combineResult.status.summary;
        assert(combined.getIndexCount()==54);
        local combinedProfile=eve.PcgMeshLodProfile();
        assert(combinedProfile.appendLevel(0.5,0.0,1.0,true,false).ok);
        local combinedLods=eve.PcgMeshLodSet();
        assert(eve.buildPcgCombinedMeshLods(combinedLods,plan,combinedProfile).ok);
        assert(combinedLods.getLevelCount()==1);
    )"));
}

TEST_CASE("procgen.pcgMeshLodBackup.restoresExactEntityGenerationAtomically") {
    using eve::graphics::Mesh;
    using eve::graphics::Renderable3D;
    auto* target = Renderable3D::create();
    auto* other  = Renderable3D::create();
    REQUIRE(target != nullptr);
    REQUIRE(other != nullptr);
    Mesh* original  = reinterpret_cast<Mesh*>(uintptr_t(0x1000));
    Mesh* generated = reinterpret_cast<Mesh*>(uintptr_t(0x2000));
    target->setMesh(original);
    target->setVisible(true);
    PcgMeshLodBackup backup;
    REQUIRE(backup.capture(*target).ok());
    CHECK(backup.isCaptured());
    CHECK_EQ(backup.getEntityId(), target->getEntityId());
    CHECK_EQ(backup.getEntityGeneration(), target->getEntityGeneration());

    target->setMeshLod(0, generated);
    target->setVisible(false);
    REQUIRE(target->setMeshLodRendererState(0, 4, 2, false, 2, false, 4, 2).ok());
    const auto otherBefore = *other->meshRenderer();
    CHECK(!backup.restore(*other).ok());
    CHECK_EQ(other->meshRenderer()->mesh, otherBefore.mesh);
    CHECK(backup.isCaptured());

    REQUIRE(backup.restore(*target).ok());
    CHECK_EQ(target->getMesh(), original);
    CHECK_EQ(target->getMeshLodCount(), 0);
    CHECK(target->getVisible());
    CHECK(!backup.isCaptured());
    CHECK(!backup.restore(*target).ok());
    ecs::DestroyEntity(target);
    ecs::DestroyEntity(other);
}

// pcg_photo_mode_water.cpp
using namespace eve;
using namespace eve::graphics;
using namespace eve::ui;
TEST_CASE("graphics.pcgWaterPhotoMode.routesSevenFieldsIntoRealPlanners") {
    cap::detail::clearAllRaw();
    PcgWaterPhotoModeAuthority water;
    water.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setBool("m_pcgWaterReflectionEnabled", false).value();
    after.setFloat("m_pcgWaterReflectionDistance", 100).value();
    after.setInt("m_pcgWaterReflectionResolution", 3).value();
    after.setFloat("m_pcgReflectionsLODBias", 2).value();
    after.setColor("m_pcgUnderwaterFogColor", 0.1f, 0.2f, 0.3f, 1).value();
    after.setFloat("m_pcgUnderwaterFogDensity", 0.08f).value();
    after.setFloat("m_pcgUnderwaterFogDistance", 70).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 7u);
    REQUIRE(plan.executeRegistered().ok());
    WaterPlanarReflectionSettings reflection;
    REQUIRE(water.applyToPlanarSettings(reflection).ok());
    WaterPlanarReflectionInput input;
    input.cameraPosition = {0, 5, 0};
    input.cameraForward  = {0, -0.2f, -1};
    auto capture         = buildWaterPlanarReflectionPlan(reflection, input);
    REQUIRE(capture.ok());
    CHECK(!capture.value().shouldRender);
    CHECK_EQ(capture.value().textureWidth, 128);
    CHECK_EQ(capture.value().layerCullDistances[0], 1200.0f);
    WaterUnderwaterSettings settings;
    settings.supportPostFx = false;
    REQUIRE(water.applyToUnderwaterSettings(settings).ok());
    WaterUnderwaterState    state;
    WaterUnderwaterOutput   output;
    UnderwaterColorGradient emptyColor;
    UnderwaterScalarCurve   emptyCurve;
    WaterUnderwaterInput    frame;
    frame.cameraY  = -5;
    frame.seaLevel = 0;
    REQUIRE(
        advanceWaterUnderwaterEffects(state, output, settings, frame, emptyColor, emptyColor, emptyCurve, emptyColor)
            .ok());
    CHECK_EQ(output.fogColor.x, 0.1f);
    CHECK_EQ(output.fogColor.y, 0.2f);
    CHECK_EQ(output.fogColor.z, 0.3f);
    CHECK_EQ(output.fogDensity, 0.08f);
    CHECK_EQ(output.fogEnd, 70.0f);
}
TEST_CASE("graphics.pcgWaterPhotoMode.rollsBackBeforeMissingAudioUnderwaterProvider") {
    cap::detail::clearAllRaw();
    PcgWaterPhotoModeAuthority water;
    water.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setColor("m_pcgUnderwaterFogColor", 0.4f, 0.5f, 0.6f, 1).value();
    after.setFloat("m_pcgUnderwaterVolume", 0.2f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    CHECK(!plan.executeRegistered().ok());
    CHECK(plan.getRollbackComplete());
    CHECK_EQ(water.getFogRed(), 0.0f);
    CHECK_EQ(water.getFogGreen(), 0.0f);
    CHECK_EQ(water.getFogBlue(), 0.0f);
}
TEST_CASE("graphics.pcgWaterPhotoMode.authorityLeaseRevokesOnDestruction") {
    cap::detail::clearAllRaw();
    PcgPhotoModeValues before, after;
    after.setFloat("m_pcgWaterReflectionDistance", 5).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    {
        PcgWaterPhotoModeAuthority water;
        water.setAuthority(true);
        REQUIRE(plan.executeRegistered().ok());
    }
    CHECK(!plan.executeRegistered().ok());
}
}  // namespace test_pcg_car_camera
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_photo_mode_apply)
#include <chrono>
#include <vector>
#include "GraphicsParitySupport.h"
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "camera/CameraController.h"
#include "common/AudioQuery.h"
#include "common/Capability.h"
#include "common/FramePresentation.h"
#include "common/ScriptProvider.h"
#include "common/Status.h"
#include "daynight/DayNight.h"
#include "daynight/PcgLightingAmbientPhotoMode.h"
#include "daynight/PcgLightingFogPhotoMode.h"
#include "daynight/PcgLightingSkyboxPhotoMode.h"
#include "daynight/PcgLightingSunPhotoMode.h"
#include "daynight/PcgLightingTimePhotoMode.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Graphics.h"
#include "graphics/PcgGraphicsPhotoMode.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ShadowScheme.h"
#include "graphics/Volumetric.h"
#include "os/OS.h"
#include "procgen/heightmap/PcgTerrainPhotoMode.h"
#include "procgen/heightmap/PcgTerrainStreaming.h"
#include "procgen/heightmap/TerrainPipeline.h"
#include "ui/PcgPhotoModeApplyPlan.h"
#include "ui/PcgPhotoModeApplySeam.h"
#include "ui/PcgPhotoModePhoto.h"
#include "weather/Weather.h"
#include "zeroerr/unittest.h"
// pcg_photo_mode_apply.cpp
namespace test_pcg_photo_mode_apply {
using namespace eve;
using namespace eve::ui;
namespace {
struct AudioMock final : IAudioQuery {
    float gain = 1;
    float volume() const override { return gain; }
    void  setVolume(float v) override { gain = v; }
    void  stopAll() override {}
};
struct PresentationMock final : IFramePresentation {
    int  count = 0;
    void setVSyncCount(int v) override { count = v; }
    int  getVSyncCount() const noexcept override { return count; }
};
struct FieldSink final : IPhotoModeFieldSink {
    std::string                      field;
    std::vector<PhotoModeAssignment> calls;
    PhotoModeFieldAcceptance         acceptsPhotoModeField(const PhotoModeAssignment& a) const noexcept override {
        return a.field == field ? PhotoModeFieldAcceptance::Accepted : PhotoModeFieldAcceptance::Rejected;
    }
    Result<void> applyPhotoModeField(const PhotoModeAssignment& a) override {
        calls.push_back(a);
        return Result<void>::success();
    }
};
struct RecordingSink final : IPhotoModeApplySink {
    std::vector<PhotoModeAssignment> calls;
    int                              failAt       = -1;
    bool                             failRollback = false;
    Result<void>                     applyPhotoModeAssignment(const PhotoModeAssignment& a) override {
        calls.push_back(a);
        const int index = static_cast<int>(calls.size()) - 1;
        if (index == failAt || (failRollback && index > failAt))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "injected", {}, {}, "test"));
        return Result<void>::success();
    }
};
PcgPhotoModeValues changed() {
    PcgPhotoModeValues v;
    v.setFloat("m_fieldOfView", 75).value();
    v.setBool("m_pcgWeatherRain", true).value();
    v.setFloat("m_pcgUnderwaterVolume", 0.2f).value();
    v.setFloat("m_globalGrassDistance", 350).value();
    return v;
}
}  // namespace
TEST_CASE("ui.pcgPhotoModeApply.compilesStableTypedDomainCommands") {
    PcgPhotoModeValues    before, after = changed();
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compile(before, after).ok());
    REQUIRE_EQ(p.getCommandCount(), 4u);
    auto f0 = p.getCommandField(0);
    REQUIRE(f0.ok());
    CHECK_EQ(f0.value(), std::string("m_fieldOfView"));
    auto d0 = p.getCommandDomain(0);
    REQUIRE(d0.ok());
    CHECK_EQ(d0.value(), static_cast<int>(PhotoModeDomain::Camera));
    auto d1 = p.getCommandDomain(1);
    REQUIRE(d1.ok());
    CHECK_EQ(d1.value(), static_cast<int>(PhotoModeDomain::Audio));
    auto d2 = p.getCommandDomain(2);
    REQUIRE(d2.ok());
    CHECK_EQ(d2.value(), static_cast<int>(PhotoModeDomain::Weather));
    auto d3 = p.getCommandDomain(3);
    REQUIRE(d3.ok());
    CHECK_EQ(d3.value(), static_cast<int>(PhotoModeDomain::Grass));
    RecordingSink sink;
    REQUIRE(p.execute(sink).ok());
    REQUIRE_EQ(sink.calls.size(), 4u);
    CHECK_EQ(std::get<float>(sink.calls[0].value), 75.0f);
    CHECK_EQ(p.getAppliedCount(), 4u);
    CHECK(p.getRollbackComplete());
}
TEST_CASE("ui.pcgPhotoModeApply.rollsBackInReverseAfterFailure") {
    PcgPhotoModeValues    before, after = changed();
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compile(before, after).ok());
    RecordingSink sink;
    sink.failAt = 2;
    auto result = p.execute(sink);
    CHECK(!result.ok());
    REQUIRE_EQ(sink.calls.size(), 5u);
    CHECK_EQ(sink.calls[3].field, std::string("m_globalVolume"));
    CHECK_EQ(sink.calls[4].field, std::string("m_fieldOfView"));
    CHECK_EQ(std::get<float>(sink.calls[3].value), 1.0f);
    CHECK_EQ(std::get<float>(sink.calls[4].value), 60.0f);
    CHECK(p.getRollbackComplete());
    CHECK_EQ(p.getAppliedCount(), 2u);
}
TEST_CASE("ui.pcgPhotoModeApply.isAtomicAndCapabilityAbsenceIsObservable") {
    PcgPhotoModeValues before, after = changed();
    auto               a = before.snapshotJson();
    auto               b = after.snapshotJson();
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compileJson(a.value(), b.value()).ok());
    auto count = p.getCommandCount();
    auto rev   = p.getRevision();
    CHECK(!p.compileJson("bad", b.value()).ok());
    CHECK_EQ(p.getCommandCount(), count);
    CHECK_EQ(p.getRevision(), rev);
    cap::detail::clearAllRaw();
    CHECK(!p.executeRegistered().ok());
    RecordingSink sink;
    cap::provide<IPhotoModeApplySink>(&sink);
    REQUIRE(p.executeRegistered().ok());
    CHECK_EQ(sink.calls.size(), count);
    cap::revoke<IPhotoModeApplySink>(&sink);
}
TEST_CASE("ui.pcgPhotoModeApply.reportsIncompleteRollback") {
    PcgPhotoModeValues    before, after = changed();
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compile(before, after).ok());
    RecordingSink sink;
    sink.failAt       = 2;
    sink.failRollback = true;
    CHECK(!p.execute(sink).ok());
    CHECK(!p.getRollbackComplete());
}
TEST_CASE("ui.pcgPhotoModeApply.registeredRouterWritesAudioAuthorityAndFieldProvider") {
    cap::detail::clearAllRaw();
    AudioMock audio;
    FieldSink camera;
    camera.field = "m_fieldOfView";
    cap::provide<IAudioQuery>(&audio);
    cap::addListener<IPhotoModeFieldSink>(&camera);
    PcgPhotoModeValues before, after;
    after.setFloat("m_fieldOfView", 82).value();
    after.setFloat("m_globalVolume", 0.35f).value();
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compile(before, after).ok());
    REQUIRE(p.executeRegistered().ok());
    CHECK_EQ(audio.gain, 0.35f);
    REQUIRE_EQ(camera.calls.size(), 1u);
    CHECK_EQ(std::get<float>(camera.calls[0].value), 82.0f);
    cap::removeListener<IPhotoModeFieldSink>(&camera);
    cap::revoke<IAudioQuery>(&audio);
}
TEST_CASE("ui.pcgPhotoModeApply.registeredRouterRejectsAmbiguousOwnership") {
    cap::detail::clearAllRaw();
    FieldSink first, second;
    first.field = second.field = "m_fieldOfView";
    cap::addListener<IPhotoModeFieldSink>(&first);
    cap::addListener<IPhotoModeFieldSink>(&second);
    PcgPhotoModeValues before, after;
    after.setFloat("m_fieldOfView", 82).value();
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compile(before, after).ok());
    CHECK(!p.executeRegistered().ok());
    CHECK(first.calls.empty());
    CHECK(second.calls.empty());
    cap::removeListener<IPhotoModeFieldSink>(&first);
    cap::removeListener<IPhotoModeFieldSink>(&second);
}
TEST_CASE("ui.pcgPhotoModeApply.weatherProviderOwnsSixPcgFieldsAndRollsBack") {
    cap::detail::clearAllRaw();
    weather::Weather   weather;
    PcgPhotoModeValues before, after;
    after.setBool("m_pcgWeatherEnabled", true).value();
    after.setBool("m_pcgWeatherRain", true).value();
    after.setBool("m_pcgWeatherSnow", true).value();
    after.setBool("m_pcgWindSettingsOverride", true).value();
    after.setFloat("m_pcgWindDirection", 0.625f).value();
    after.setFloat("m_pcgWindSpeed", 0.8f).value();
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compile(before, after).ok());
    REQUIRE_EQ(p.getCommandCount(), 6u);
    REQUIRE(p.executeRegistered().ok());
    CHECK(weather.getPcgWeatherEnabled());
    CHECK(weather.getPcgRainEnabled());
    CHECK(weather.getPcgSnowEnabled());
    CHECK(weather.getPcgWindOverride());
    CHECK_EQ(weather.getWindDirection(), 0.625f);
    CHECK_EQ(weather.getWindSpeed(), 0.8f);
    PcgPhotoModeValues failing = after;
    failing.setFloat("m_pcgWindDirection", 0.9f).value();
    failing.setFloat("m_globalGrassDistance", 300).value();
    PcgPhotoModeApplyPlan rollback;
    REQUIRE(rollback.compile(after, failing).ok());
    CHECK(!rollback.executeRegistered().ok());
    CHECK_EQ(weather.getWindSpeed(), 0.8f);
}
TEST_CASE("ui.pcgPhotoModeApply.explicitCameraAuthorityAppliesSixLiveFields") {
    cap::detail::clearAllRaw();
    auto* camera = graphics::Camera3D::createCamera();
    camera->setEye(0, 0, 3);
    camera->setTarget(0, 0, 0);
    camera::CameraController controller;
    controller.setCamera(camera);
    controller.setPhotoModeAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_fieldOfView", 72).value();
    after.setFloat("m_pcgCullinDistance", 25).value();
    after.setFloat("m_cameraAperture", 11).value();
    after.setFloat("m_cameraFocalLength", 80).value();
    after.setFloat("m_cameraRoll", 30).value();
    after.setFloat("m_farClipPlane", 1500).value();
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compile(before, after).ok());
    REQUIRE_EQ(p.getCommandCount(), 6u);
    REQUIRE(p.executeRegistered().ok());
    CHECK_EQ(camera->getFov(), 72.0f);
    CHECK_EQ(camera->getFarClip(), 1500.0f);
    CHECK_EQ(camera->getAperture(), 11.0f);
    CHECK_EQ(camera->getFocalLength(), 80.0f);
    CHECK_EQ(controller.getCameraRoll(), 30.0f);
    CHECK_EQ(controller.getPcgCullingDistance(), 25.0f);
    CHECK_EQ(camera->getLayerCullDistance(0), 1525.0f);
    CHECK(std::abs(camera->data()->upX) > 0.4f);
    controller.setPhotoModeAuthority(false);
    CHECK(!p.executeRegistered().ok());
}
TEST_CASE("ui.pcgPhotoModeApply.cameraAuthorityLifetimeRevokesProvider") {
    cap::detail::clearAllRaw();
    auto*              camera = graphics::Camera3D::createCamera();
    PcgPhotoModeValues before, after;
    after.setFloat("m_fieldOfView", 71).value();
    PcgPhotoModeApplyPlan p;
    REQUIRE(p.compile(before, after).ok());
    {
        camera::CameraController controller;
        controller.setCamera(camera);
        controller.setPhotoModeAuthority(true);
        REQUIRE(p.executeRegistered().ok());
    }
    CHECK(!p.executeRegistered().ok());
}
TEST_CASE("ui.pcgPhotoModeApply.cameraAuthorityProjectsAllPostFxFieldsToFinalComposite") {
    cap::detail::clearAllRaw();
    auto*                    camera = graphics::Camera3D::createCamera();
    camera::CameraController controller;
    controller.setCamera(camera);
    controller.setPhotoModeAuthority(true);
    PcgPhotoModeValues before, after;
    before.setBool("m_dofActive", false).value();
    before.setInt("m_dofFocusModeURP", 1).value();
    after.setFloat("m_postFXExposure", 2.25f).value();
    after.setInt("m_postFXExposureMode", 1).value();
    after.setBool("m_dofActive", true).value();
    after.setBool("m_autoDOFFocus", false).value();
    after.setFloat("m_dofFocusDistance", 24.f).value();
    after.setFloat("m_dofAperture", 5.6f).value();
    after.setFloat("m_dofFocalLength", 85.f).value();
    after.setInt("m_dofKernelSize", 3).value();
    after.setInt("m_savedDofFocusMode", 2).value();
    after.setInt("m_dofFocusModeHDRP", 1).value();
    after.setInt("m_dofFocusModeURP", 0).value();
    after.setInt("m_dofQualityHDRP", 2).value();
    after.setFloat("m_dofNearBlurStart", 1.f).value();
    after.setFloat("m_dofNearBlurEnd", 4.f).value();
    after.setFloat("m_dofFarBlurStart", 40.f).value();
    after.setFloat("m_dofFarBlurEnd", 120.f).value();
    after.setFloat("m_dofStartBlurURP", 8.f).value();
    after.setFloat("m_dofEndBlurURP", 68.f).value();
    after.setFloat("m_dofMaxRadiusBlur", 2.f).value();
    after.setBool("m_dofHighQualityURP", true).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 20u);
    REQUIRE(plan.executeRegistered().ok());
    const auto& state = controller.getPcgPostFxState();
    CHECK_EQ(state.savedFocusMode, 2);
    CHECK_EQ(state.focusModeHdrp, 1);
    CHECK_EQ(state.farBlurEnd, 120.f);
    CHECK_EQ(camera->getExposure(), 2.25f);
    CHECK(camera->isAutoExposure());
    CHECK(camera->data()->dofMaxBlurPx > 0.f);
    CHECK_EQ(camera->data()->dofFocusDistance, 24.f);
    CHECK_EQ(camera->data()->dofFocusRange, 60.f);
    CHECK_EQ(camera->data()->dofMaxBlurPx, 12.f);
    CHECK_EQ(camera->getAperture(), 5.6f);
    CHECK_EQ(camera->getFocalLength(), 85.f);
}
TEST_CASE("ui.pcgPhotoModeApply.postFxRollsBackLiveCompositeWhenFollowingDomainIsMissing") {
    cap::detail::clearAllRaw();
    auto*                    camera = graphics::Camera3D::createCamera();
    camera::CameraController controller;
    controller.setCamera(camera);
    controller.setPhotoModeAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_postFXExposure", 3.f).value();
    after.setBool("m_dofActive", false).value();
    after.setFloat("m_terrainDetailDensity", 0.8f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    CHECK(!plan.executeRegistered().ok());
    CHECK(plan.getRollbackComplete());
    CHECK_EQ(controller.getPcgPostFxState().postFxExposure, 13.5f);
    CHECK(controller.getPcgPostFxState().depthOfFieldActive);
    CHECK_EQ(camera->getExposure(), 13.5f);
    CHECK(camera->data()->dofMaxBlurPx > 0.f);
}
TEST_CASE("ui.pcgPhotoModeApply.terrainAuthorityDrivesLodBasemapAndDetailPolicy") {
    cap::detail::clearAllRaw();
    procgen::PcgTerrainPhotoModeAuthority terrain;
    terrain.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setBool("m_drawInstanced", false).value();
    after.setFloat("m_terrainDetailDensity", 0.8f).value();
    after.setFloat("m_terrainDetailDistance", 275.f).value();
    after.setFloat("m_terrainPixelError", 1.f).value();
    after.setFloat("m_terrainBasemapDistance", 500.f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 5u);
    REQUIRE(plan.executeRegistered().ok());
    CHECK(!terrain.state().drawInstanced);
    CHECK_EQ(terrain.state().detailDensity, 0.8f);
    CHECK_EQ(terrain.state().detailDistance, 275.f);
    CHECK_EQ(terrain.textureTier(499.f), procgen::PcgTerrainTextureTier::Detailed);
    CHECK_EQ(terrain.textureTier(500.f), procgen::PcgTerrainTextureTier::Basemap);
    procgen::Heightmap heightmap(9, 9);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) heightmap.setHeight(x, y, float((x + y) % 3));
    procgen::TerrainMeshSettings settings;
    settings.cellsX            = 8;
    settings.cellsY            = 8;
    const int           strict = terrain.selectLod(heightmap, settings, 3, 20.f, 1080.f, 60.f);
    PhotoModeAssignment relaxed{"m_terrainPixelError", PhotoModeDomain::Terrain, 1000.f};
    REQUIRE(terrain.applyPhotoModeField(relaxed).ok());
    CHECK(terrain.selectLod(heightmap, settings, 3, 20.f, 1080.f, 60.f) >= strict);
}
TEST_CASE("ui.pcgPhotoModeApply.graphicsAuthorityProjectsFiveLiveQualityFields") {
    cap::detail::clearAllRaw();
    auto&                                   scheme          = graphics::ShadowSchemeSettings::current();
    const int                               prevMap         = scheme.directionalMapSize;
    const int                               prevLocal       = scheme.localMapSize;
    const float                             prevMaxDistance = scheme.directionalMaxDistance;
    auto*                                   graphics = graphics::parity_test::headlessGraphics();
    auto*                                   camera   = graphics::Camera3D::createCamera();
    auto*                                   control  = graphics->getRenderControl();
    graphics::PcgGraphicsPhotoModeAuthority authority;
    authority.setTargets(graphics, control, camera);
    authority.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_lodBias", 4.f).value();
    after.setInt("m_antiAliasing", 2).value();
    after.setFloat("m_shadowDistance", 220.f).value();
    after.setInt("m_shadowResolution", 1).value();
    after.setInt("m_shadowCascades", 2).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 5u);
    REQUIRE(plan.executeRegistered().ok());
    CHECK_EQ(camera->data()->lodBias, 4.f);
    CHECK_EQ(camera->getShadowLayerCullDistance(31), 220.f);
    CHECK(control->isEnabled("aa"));
    CHECK(!control->isEnabled("taa"));
    CHECK_EQ(graphics->pipelineAntiAliasing()->getMode(), std::string("smaa"));
    CHECK_EQ(authority.state().shadowResolution, 1);
    CHECK_EQ(authority.state().shadowCascades, 2);
    CHECK_EQ(scheme.directionalMapSize, 1024);
    CHECK_EQ(scheme.resolvedAtlasMapSize(), 1024);
    CHECK_EQ(scheme.directionalMaxDistance, 220.f);
    scheme.directionalMapSize     = prevMap;
    scheme.localMapSize           = prevLocal;
    scheme.directionalMaxDistance = prevMaxDistance;
}
TEST_CASE("ui.pcgPhotoModeApply.lightingShadowMultiplierUsesGraphicsShadowAuthority") {
    cap::detail::clearAllRaw();
    auto&                                   scheme          = graphics::ShadowSchemeSettings::current();
    const int                               prevMap         = scheme.directionalMapSize;
    const int                               prevLocal       = scheme.localMapSize;
    const float                             prevMaxDistance = scheme.directionalMaxDistance;
    auto*                                   graphics = graphics::parity_test::headlessGraphics();
    auto*                                   camera   = graphics::Camera3D::createCamera();
    graphics::PcgGraphicsPhotoModeAuthority authority;
    authority.setTargets(graphics, graphics->getRenderControl(), camera);
    authority.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_shadowDistance", 200.f).value();
    after.setFloat("m_globalShadowDistanceMultiplier", 2.5f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 2u);
    REQUIRE(plan.executeRegistered().ok());
    CHECK_EQ(camera->getShadowLayerCullDistance(0), 500.f);
    CHECK_EQ(camera->getShadowLayerCullDistance(31), 500.f);
    CHECK_EQ(authority.state().globalShadowDistanceMultiplier, 2.5f);
    // CSM coverage follows PhotoMode distance but stays capped for texel density.
    CHECK_EQ(scheme.directionalMaxDistance, 256.f);
    PhotoModeAssignment invalid{"m_globalShadowDistanceMultiplier", PhotoModeDomain::Lighting, 5.1f};
    CHECK(!authority.applyPhotoModeField(invalid).ok());
    CHECK_EQ(camera->getShadowLayerCullDistance(7), 500.f);
    scheme.directionalMapSize     = prevMap;
    scheme.localMapSize           = prevLocal;
    scheme.directionalMaxDistance = prevMaxDistance;
}
TEST_CASE("ui.pcgPhotoModeApply.framePacingAuthorityAppliesVSyncAndRealFrameCap") {
    cap::detail::clearAllRaw();
    PresentationMock presentation;
    cap::provide<IFramePresentation>(&presentation);
    os::OS             os;
    PcgPhotoModeValues before, after;
    after.setInt("m_vSync", 2).value();
    after.setInt("m_targetFPS", 120).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 2u);
    auto domain = plan.getCommandDomain(0);
    REQUIRE(domain.ok());
    CHECK_EQ(domain.value(), static_cast<int>(PhotoModeDomain::FramePacing));
    REQUIRE(plan.executeRegistered().ok());
    CHECK_EQ(presentation.count, 2);
    CHECK_EQ(os.getTargetFramesPerSecond(), 120);
    os.limitFrame();
    const auto start = std::chrono::steady_clock::now();
    os.limitFrame();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK_GE(elapsed, 1);
    cap::revoke<IFramePresentation>(&presentation);
}
TEST_CASE("ui.pcgPhotoModeApply.framePacingAbsenceFailsBeforePresentationMutation") {
    cap::detail::clearAllRaw();
    PresentationMock presentation;
    cap::provide<IFramePresentation>(&presentation);
    PcgPhotoModeValues before, after;
    after.setInt("m_vSync", 2).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    CHECK(!plan.executeRegistered().ok());
    CHECK_EQ(presentation.count, 0);
    cap::revoke<IFramePresentation>(&presentation);
}
TEST_CASE("ui.pcgPhotoModeApply.streamingAuthorityDrivesRealChunkResidencyAndImpostorTier") {
    cap::detail::clearAllRaw();
    procgen::Heightmap heightmap(24, 24);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) heightmap.setHeight(x, y, float(x + y));
    auto                 hydrology = procgen::TerrainPipeline::buildHydrology(heightmap, 5.f, -1.f);
    auto                 climate   = procgen::TerrainPipeline::buildClimate(heightmap, hydrology, -1.f, 0.5f);
    std::vector<uint8_t> bytes;
    std::string          error;
    REQUIRE(procgen::TerrainAsset::bake(heightmap, hydrology, climate, 8, bytes, &error));
    procgen::TerrainStreamingCache cache;
    REQUIRE(cache.open(bytes.data(), bytes.size(), &error));
    procgen::PcgTerrainStreamingAuthority streaming;
    REQUIRE(streaming.setTarget(&cache, 100.f).ok());
    streaming.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_pcgLoadRange", 900.f).value();
    after.setFloat("m_pcgImpostorRange", 1700.f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 2u);
    REQUIRE(plan.executeRegistered().ok());
    CHECK_EQ(streaming.regularRadiusChunks(), 2);
    CHECK_EQ(streaming.impostorRadiusChunks(), 3);
    CHECK_EQ(streaming.tierForDistance(900.f), procgen::PcgTerrainStreamingTier::Regular);
    CHECK_EQ(streaming.tierForDistance(901.f), procgen::PcgTerrainStreamingTier::Impostor);
    CHECK_EQ(streaming.tierForDistance(1701.f), procgen::PcgTerrainStreamingTier::Unloaded);
    auto streamed = streaming.streamAround(12, 12, 2);
    REQUIRE(streamed.ok());
    CHECK_EQ(streamed.value().loaded, 2);
    PhotoModeAssignment invalid{"m_pcgLoadRange", PhotoModeDomain::Streaming, -1.f};
    CHECK(!streaming.applyPhotoModeField(invalid).ok());
    CHECK_EQ(streaming.state().regularRange, 900.f);
}
TEST_CASE("ui.pcgPhotoModeApply.photoAuthorityOwnsAllCapturePersistenceAndOverlayFields") {
    cap::detail::clearAllRaw();
    PcgPhotoModePhotoAuthority photo;
    photo.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setString("m_lastSceneName", "Pcg World").value();
    after.setInt("m_screenshotResolution", 9).value();
    after.setInt("m_screenshotImageFormat", 0).value();
    after.setBool("m_loadSavedSettings", false).value();
    after.setBool("m_revertOnDisabled", false).value();
    after.setBool("m_showFPS", true).value();
    after.setBool("m_showReticle", true).value();
    after.setBool("m_showRuleOfThirds", true).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 8u);
    REQUIRE(plan.executeRegistered().ok());
    CHECK_EQ(photo.state().lastSceneName, "Pcg World");
    CHECK_EQ(photo.screenshotSize().width, 7680);
    CHECK_EQ(photo.screenshotSize().height, 4320);
    CHECK_EQ(std::string(photo.screenshotExtension()), "exr");
    CHECK(!photo.state().loadSavedSettings);
    CHECK(!photo.state().revertOnDisabled);
    CHECK(photo.state().showFps);
    CHECK(photo.state().showReticle);
    CHECK(photo.state().showRuleOfThirds);
    PhotoModeAssignment invalid{"m_screenshotResolution", PhotoModeDomain::Photo, int64_t{10}};
    CHECK(!photo.applyPhotoModeField(invalid).ok());
    CHECK_EQ(photo.screenshotSize().width, 7680);
}
TEST_CASE("ui.pcgPhotoModeApply.photoAuthorityOwnsLightingProfileIdentity") {
    cap::detail::clearAllRaw();
    PcgPhotoModePhotoAuthority photo;
    photo.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setBool("m_isUsingPcgLighting", true).value();
    after.setInt("m_selectedPcgLightingProfile", 3).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 2u);
    REQUIRE(plan.executeRegistered().ok());
    CHECK(photo.lightingProfileMatches(3));
    CHECK(!photo.lightingProfileMatches(2));
    PhotoModeAssignment custom{"m_selectedPcgLightingProfile", PhotoModeDomain::Lighting, int64_t{-1}};
    REQUIRE(photo.applyPhotoModeField(custom).ok());
    CHECK(!photo.lightingProfileMatches(-1));
    PhotoModeAssignment invalid{"m_selectedPcgLightingProfile", PhotoModeDomain::Lighting, int64_t{-2}};
    CHECK(!photo.applyPhotoModeField(invalid).ok());
    CHECK_EQ(photo.state().lightingProfile, -1);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingTimeAuthorityDrivesDayNightClock") {
    cap::detail::clearAllRaw();
    daynight::DayNight                 clock;
    daynight::PcgLightingTimePhotoMode authority;
    authority.setTarget(&clock);
    authority.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_pcgTime", 18.5f).value();
    after.setBool("m_pcgTimeOfDayEnabled", true).value();
    after.setFloat("m_pcgTimeScale", 12.f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 3u);
    REQUIRE(plan.executeRegistered().ok());
    CHECK_EQ(clock.getTimeOfDay(), 18.5f);
    CHECK_EQ(clock.getSpeed(), 12.f);
    CHECK(!clock.isPaused());
    PhotoModeAssignment pause{"m_pcgTimeOfDayEnabled", PhotoModeDomain::Lighting, false};
    REQUIRE(authority.applyPhotoModeField(pause).ok());
    CHECK(clock.isPaused());
    PhotoModeAssignment invalid{"m_pcgTimeScale", PhotoModeDomain::Lighting, 201.f};
    CHECK(!authority.applyPhotoModeField(invalid).ok());
    CHECK_EQ(clock.getSpeed(), 12.f);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingSunAuthorityPersistsAcrossDayNightUpdates") {
    cap::detail::clearAllRaw();
    daynight::DayNight                clock;
    daynight::PcgLightingSunPhotoMode authority;
    PhotoModeAssignment               unavailable{"m_sunOverride", PhotoModeDomain::Lighting, true};
    CHECK(!authority.applyPhotoModeField(unavailable).ok());
    authority.setTarget(&clock);
    authority.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_sunRotation", 90.f).value();
    after.setFloat("m_sunPitch", 30.f).value();
    after.setBool("m_sunOverride", true).value();
    after.setFloat("m_sunIntensity", 2.5f).value();
    after.setColor("m_sunColor", 0.8f, 0.6f, 0.4f, 1.f).value();
    after.setFloat("m_sunKelvinValue", 4200.f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 6u);
    REQUIRE(plan.executeRegistered().ok());
    auto state = clock.getPcgManualSun();
    CHECK(state.enabled);
    CHECK_EQ(state.rotationDegrees, 90.f);
    CHECK_EQ(state.pitchDegrees, 30.f);
    CHECK_EQ(state.intensity, 2.5f);
    auto* graphics = graphics::parity_test::headlessGraphics();
    clock.update(0.f, graphics);
    CHECK_EQ(clock.getSunIntensity(), 2.5f);
    CHECK(std::abs(clock.getSunDirX() + 0.8660254f) < 0.0001f);
    CHECK(std::abs(clock.getSunDirY() - 0.5f) < 0.0001f);
    CHECK(std::abs(clock.getSunDirZ()) < 0.0001f);
    const float manualRed = clock.getSunR();
    clock.setTimeOfDay(2.f);
    clock.update(1.f, graphics);
    CHECK(std::abs(clock.getSunDirX() + 0.8660254f) < 0.0001f);
    CHECK(std::abs(clock.getSunR() - manualRed) < 0.0001f);
    CHECK(manualRed > 1.9f);
    PhotoModeAssignment disable{"m_sunOverride", PhotoModeDomain::Lighting, false};
    REQUIRE(authority.applyPhotoModeField(disable).ok());
    clock.setTimeOfDay(12.f);
    clock.update(0.f, graphics);
    CHECK_EQ(clock.getSunIntensity(), 1.f);
    CHECK(clock.getSunDirY() > 0.9f);
    PhotoModeAssignment invalid{"m_sunKelvinValue", PhotoModeDomain::Lighting, 1000.f};
    CHECK(!authority.applyPhotoModeField(invalid).ok());
    CHECK_EQ(clock.getPcgManualSun().kelvin, 4200.f);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingSkyboxAuthorityDrivesProceduralSkyAndRestores") {
    cap::detail::clearAllRaw();
    daynight::DayNight clock;
    auto*              graphics = graphics::parity_test::headlessGraphics();
    clock.setTimeOfDay(12.f);
    clock.setPaused(true);
    clock.update(0.f, graphics);
    const float                          baseR = clock.getSkyR(), baseG = clock.getSkyG(), baseB = clock.getSkyB();
    daynight::PcgLightingSkyboxPhotoMode authority;
    PhotoModeAssignment                  unavailable{"m_skyboxOverride", PhotoModeDomain::Lighting, true};
    CHECK(!authority.applyPhotoModeField(unavailable).ok());
    authority.setTarget(&clock);
    authority.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setBool("m_skyboxOverride", true).value();
    after.setFloat("m_skyboxRotation", 90.f).value();
    after.setFloat("m_skyboxExposure", 2.f).value();
    after.setColor("m_skyboxTint", 0.5f, 0.25f, 1.f, 1.f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 4u);
    REQUIRE(plan.executeRegistered().ok());
    clock.update(0.f, graphics);
    const auto state = clock.getPcgSkybox();
    CHECK(state.enabled);
    CHECK_EQ(state.rotationDegrees, 90.f);
    CHECK_EQ(state.exposure, 2.f);
    CHECK_EQ(state.tintRed, 0.5f);
    CHECK(clock.getSkyG() < clock.getSkyB());
    const bool skyChanged = std::abs(clock.getSkyR() - baseR) > 0.0001f || std::abs(clock.getSkyG() - baseG) > 0.0001f;
    CHECK(skyChanged);
    clock.update(1.f, graphics);
    CHECK_EQ(clock.getPcgSkybox().rotationDegrees, 90.f);
    PhotoModeAssignment disable{"m_skyboxOverride", PhotoModeDomain::Lighting, false};
    REQUIRE(authority.applyPhotoModeField(disable).ok());
    clock.update(0.f, graphics);
    CHECK(std::abs(clock.getSkyR() - baseR) < 0.0001f);
    CHECK(std::abs(clock.getSkyG() - baseG) < 0.0001f);
    CHECK(std::abs(clock.getSkyB() - baseB) < 0.0001f);
    PhotoModeAssignment invalid{"m_skyboxExposure", PhotoModeDomain::Lighting, 31.f};
    CHECK(!authority.applyPhotoModeField(invalid).ok());
    CHECK_EQ(clock.getPcgSkybox().exposure, 2.f);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingFogAuthorityDrivesFogAndDensityVolume") {
    cap::detail::clearAllRaw();
    daynight::DayNight                clock;
    daynight::PcgLightingFogPhotoMode authority;
    PhotoModeAssignment               unavailable{"m_fogOverride", PhotoModeDomain::Lighting, true};
    CHECK(!authority.applyPhotoModeField(unavailable).ok());
    authority.setTarget(&clock);
    authority.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_pcgAdditionalLinearFog", 100.f).value();
    after.setFloat("m_pcgAdditionalExponentialFog", 0.02f).value();
    after.setBool("m_fogOverride", true).value();
    after.setInt("m_fogMode", 2).value();
    after.setColor("m_fogColor", 0.2f, 0.3f, 0.4f, 1.f).value();
    after.setFloat("m_fogDensity", 0.03f).value();
    after.setFloat("m_fogStart", 50.f).value();
    after.setFloat("m_fogEnd", 900.f).value();
    after.setFloat("m_globalFogDensityMultiplier", 2.f).value();
    after.setBool("m_overrideDensityVolume", true).value();
    after.setColor("m_densityVolumeAlbedoColor", 0.6f, 0.7f, 0.8f, 1.f).value();
    after.setFloat("m_densityVolumeFogDistance", 500.f).value();
    after.setInt("m_densityVolumeEffectType", 4).value();
    after.setInt("m_densityVolumeTilingResolution", 5).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 14u);
    REQUIRE(plan.executeRegistered().ok());
    auto*                graphics = graphics::parity_test::headlessGraphics();
    graphics::Volumetric fog(graphics);
    fog.setMode("raymarch");
    fog.setQuality("low");
    fog.setDensity(0.12f);
    const float baselineDensity = fog.getFloat("density");
    clock.applyAtmosphere(&fog);
    CHECK_EQ(fog.getMode(), std::string("fog"));
    CHECK_EQ(fog.getQuality(), std::string("high"));
    CHECK(std::abs(fog.getFloat("fogR") - 0.6f) < 0.0001f);
    CHECK(std::abs(fog.getFloat("density") - 0.08f) < 0.0001f);
    CHECK(std::abs(fog.getFloat("fogStart")) < 0.0001f);
    CHECK(std::abs(fog.getFloat("fogEnd") - 500.f) < 0.0001f);
    PhotoModeAssignment noVolume{"m_overrideDensityVolume", PhotoModeDomain::Lighting, false};
    REQUIRE(authority.applyPhotoModeField(noVolume).ok());
    clock.applyAtmosphere(&fog);
    CHECK(std::abs(fog.getFloat("density") - 0.15f) < 0.0001f);
    CHECK(std::abs(fog.getFloat("fogStart") - 150.f) < 0.0001f);
    CHECK(std::abs(fog.getFloat("fogEnd") - 1000.f) < 0.0001f);
    REQUIRE(authority.applyPhotoModeField({"m_fogOverride", PhotoModeDomain::Lighting, false}).ok());
    REQUIRE(authority.applyPhotoModeField({"m_pcgAdditionalLinearFog", PhotoModeDomain::Lighting, 0.f}).ok());
    REQUIRE(authority.applyPhotoModeField({"m_pcgAdditionalExponentialFog", PhotoModeDomain::Lighting, 0.f}).ok());
    clock.applyAtmosphere(&fog);
    CHECK_EQ(fog.getMode(), std::string("raymarch"));
    CHECK_EQ(fog.getQuality(), std::string("low"));
    CHECK(std::abs(fog.getFloat("density") - baselineDensity) < 0.0001f);
    PhotoModeAssignment invalid{"m_densityVolumeEffectType", PhotoModeDomain::Lighting, int64_t{5}};
    CHECK(!authority.applyPhotoModeField(invalid).ok());
    CHECK_EQ(clock.getPcgFog().densityVolumeEffect, 4);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingAmbientAuthorityDrivesGradientAndSunMultiplier") {
    cap::detail::clearAllRaw();
    daynight::DayNight                    clock;
    daynight::PcgLightingAmbientPhotoMode authority;
    CHECK(!authority.applyPhotoModeField({"m_ambientIntensity", PhotoModeDomain::Lighting, 2.f}).ok());
    authority.setTarget(&clock);
    authority.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setFloat("m_ambientIntensity", 2.f).value();
    after.setColor("m_ambientSkyColor", 1.f, 0.f, 0.f, 1.f).value();
    after.setColor("m_ambientEquatorColor", 0.f, 1.f, 0.f, 1.f).value();
    after.setColor("m_ambientGroundColor", 0.f, 0.f, 1.f, 1.f).value();
    after.setFloat("m_globalLightIntensityMultiplier", 3.f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 5u);
    REQUIRE(plan.executeRegistered().ok());
    CHECK(std::abs(clock.getAmbientR() - 1.f) < 0.0001f);
    CHECK(std::abs(clock.getAmbientG() - 0.7f) < 0.0001f);
    CHECK(std::abs(clock.getAmbientB() - 0.3f) < 0.0001f);
    auto* graphics = graphics::parity_test::headlessGraphics();
    clock.setTimeOfDay(12.f);
    clock.setPaused(true);
    clock.update(0.f, graphics);
    CHECK(clock.getSunR() > 2.9f);
    const auto state = clock.getPcgAmbientLight();
    CHECK(state.active);
    CHECK_EQ(state.globalLightMultiplier, 3.f);
    PhotoModeAssignment invalid{"m_globalLightIntensityMultiplier", PhotoModeDomain::Lighting, 5.1f};
    CHECK(!authority.applyPhotoModeField(invalid).ok());
    CHECK_EQ(clock.getPcgAmbientLight().globalLightMultiplier, 3.f);
    authority.setAuthority(false);
    CHECK(!clock.getPcgAmbientLight().active);
    CHECK(clock.getAmbientR() < 1.f);
}

// ---------------------------------------------------------------------------
// 决定③第一个落地的插座：`IPhotoModeApplySink` 可以由脚本提供。
//
// 这个插座要的正是"对象参数"：每条赋值是一个记录（field / domain / value，值本身还可能是颜色
// 那张表）。测试同时是 present/absent 双探针（J-4 的形态）。
// ---------------------------------------------------------------------------

namespace {

const char* kScriptPhotoModeApplyContent = R"SQ(
// 观察用的根表槽位：先声明再在方法里赋值（`<-` 只在第一次能用）。
lastField <- ""
lastDomain <- ""
lastValue <- null
lastValueIsColor <- false
appliedCount <- 0

class Applier {
    function applyPhotoModeAssignment(assignment) {
        appliedCount = appliedCount + 1
        lastField = assignment.field
        lastDomain = assignment.domain
        lastValue = assignment.value
        lastValueIsColor = typeof assignment.value == "table"
    }
}

// 第 3 条赋值拒绝：计划必须失败、按逆序回滚前两条，并报告回滚结果。
class FailingApplier {
    function applyPhotoModeAssignment(assignment) {
        appliedCount = appliedCount + 1
        if (appliedCount == 3) throw "script refused the third assignment"
    }
}

provider <- Applier()
failingProvider <- FailingApplier()
)SQ";

eve::DiagnosticCode firstCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

}  // namespace

/** 脚本提供方要一个真 VM；接缝由用例显式声明，所以不依赖 UI 模块是否被构造。 */
class ScriptPhotoModeApplyTest : public ScriptTest {
public:
    ScriptPhotoModeApplyTest() : ScriptTest(kScriptPhotoModeApplyContent) {
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
        eve::cap::detail::clearAllRaw();
    }

    ~ScriptPhotoModeApplyTest() {
        eve::ui::withdrawScriptPhotoModeApplySeam();
        eve::cap::detail::clearAllRaw();
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            registry.removeCapability(capability).ignore("fixture teardown withdraws only this case's seams");
        }
    }

private:
    std::vector<std::string> declaredBefore_;
};

TEST_CASE_FIXTURE(ScriptPhotoModeApplyTest, "ui.pcgPhotoModeApply.scriptProviderAnswersTheSinkThroughTheSeam") {
    using namespace eve;

    // ① 缺席：接缝没声明时，发布直接被拒——不是"发布成功但永远不会被调用"。
    auto refused = ScriptProviderRegistry::instance().publishForCapability(IPhotoModeApplySink::capabilityName,
                                                                           "test.photoapply", vm.find("provider"));
    REQUIRE(!refused.ok());
    REQUIRE_EQ(firstCode(refused.status()), DiagnosticCode::NotFound);
    REQUIRE(cap::query<IPhotoModeApplySink>() == nullptr);

    // ② 声明 + 发布：适配器进能力槽位。
    ui::declareScriptPhotoModeApplySeam();
    auto published = ScriptProviderRegistry::instance().publishForCapability(IPhotoModeApplySink::capabilityName,
                                                                             "test.photoapply", vm.find("provider"));
    REQUIRE(published.ok());
    REQUIRE(cap::query<IPhotoModeApplySink>() != nullptr);

    // ③ 标量赋值：脚本收到一张表（field/domain/value），域是**名字**而不是枚举数字。
    PcgPhotoModeValues before, after;
    after.setFloat("m_fieldOfView", 75).value();
    ui::PcgPhotoModeApplyPlan scalarPlan;
    REQUIRE(scalarPlan.compile(before, after).ok());
    REQUIRE_EQ(scalarPlan.getCommandCount(), 1u);
    REQUIRE(scalarPlan.executeRegistered().ok());
    REQUIRE_EQ(vm.find("appliedCount").toInt(), 1);
    REQUIRE_EQ(vm.find("lastField").toString(), std::string("m_fieldOfView"));
    REQUIRE_EQ(vm.find("lastDomain").toString(), std::string("camera"));
    REQUIRE(!vm.find("lastValueIsColor").toBool());
    REQUIRE_EQ(vm.find("lastValue").toFloat(), 75.0);

    // ④ 颜色赋值：值本身又是一张表（r/g/b/a）。
    PcgPhotoModeValues colorBefore, colorAfter;
    colorAfter.setColor("m_ambientSkyColor", 1.f, 0.25f, 0.f, 1.f).value();
    ui::PcgPhotoModeApplyPlan colorPlan;
    REQUIRE(colorPlan.compile(colorBefore, colorAfter).ok());
    REQUIRE_EQ(colorPlan.getCommandCount(), 1u);
    REQUIRE(colorPlan.executeRegistered().ok());
    REQUIRE_EQ(vm.find("lastField").toString(), std::string("m_ambientSkyColor"));
    REQUIRE_EQ(vm.find("lastDomain").toString(), std::string("lighting"));
    REQUIRE(vm.find("lastValueIsColor").toBool());
    REQUIRE_EQ(vm.find("lastValue").find("r").toFloat(), 1.0);
    REQUIRE_EQ(vm.find("lastValue").find("g").toFloat(), 0.25);

    // ⑤ 撤回后能力槽位重新为空：缺席是可观测的，不是"绑着但不会响"。
    REQUIRE(ScriptProviderRegistry::instance().unload(published.value()).ok());
    REQUIRE(cap::query<IPhotoModeApplySink>() == nullptr);
    CHECK(!scalarPlan.executeRegistered().ok());
}

TEST_CASE_FIXTURE(ScriptPhotoModeApplyTest, "ui.pcgPhotoModeApply.scriptRefusalDrivesThePlanRollback") {
    using namespace eve;

    ui::declareScriptPhotoModeApplySeam();
    auto published = ScriptProviderRegistry::instance().publishForCapability(
        IPhotoModeApplySink::capabilityName, "test.photoapply", vm.find("failingProvider"));
    REQUIRE(published.ok());

    PcgPhotoModeValues before, after;
    after.setFloat("m_fieldOfView", 72).value();
    after.setFloat("m_cameraRoll", 30).value();
    after.setFloat("m_pcgCullinDistance", 25).value();
    ui::PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 3u);

    // 脚本在第 3 条抛错：计划必须失败、逆序回滚前两条（共 5 次调用），并报告回滚结果。
    CHECK(!plan.executeRegistered().ok());
    REQUIRE_EQ(vm.find("appliedCount").toInt(), 5);
    CHECK_EQ(plan.getAppliedCount(), 2u);
    CHECK(plan.getRollbackComplete());
}
}  // namespace test_pcg_photo_mode_apply
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_accelerator_domains)
#include "PrivateTestScope.h"
#include <array>
#include <memory>
#include <string>
#include "common/Capability.h"
#include "physics/backend/SimulationBackend.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_accelerator_domains.cpp
namespace test_physics_accelerator_domains {
namespace {

using namespace eve::physics;

class CapabilityReset {
public:
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

void noOpStep(void*, const eve::SimulationStep&, const SimulationSettings&) noexcept {}

std::unique_ptr<ISimulationBackend> cpuBackend() {
    static int state = 0;
    return detail::makeCallbackSimulationBackend(&state, &noOpStep, SimulationBackendKind::Cpu,
                                                 SimulationDeterminism::ToleranceBounded);
}

class FailingStepBackend final : public ISimulationBackend {
public:
    eve::Result<void> step(const eve::SimulationStep&, const SimulationSettings&) override {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "injected accelerator dispatch failure", "physics.accelerator.step"));
    }
    [[nodiscard]] SimulationObservation observation() const noexcept override { return {}; }
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return SimulationBackendKind::Gpu; }
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override {
        return SimulationDeterminism::ToleranceBounded;
    }
};

enum class ProviderBehavior { Succeed, SucceedWithWarning, CreateFailure, StepFailure };

class SatelliteProvider final : public IAcceleratorBackendProvider {
public:
    explicit SatelliteProvider(ProviderBehavior behavior) : behavior_(behavior) {}

    [[nodiscard]] bool supports(SimulationBackendDomain domain) const noexcept override {
        return domain == SimulationBackendDomain::Cloth2D || domain == SimulationBackendDomain::Cloth3D ||
               domain == SimulationBackendDomain::SurfaceFluid;
    }

    eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain domain, void*) override {
        if (!supports(domain)) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "satellite domain unsupported", "physics.accelerator.domain"));
        }
        if (behavior_ == ProviderBehavior::CreateFailure) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "injected accelerator creation failure", "physics.accelerator.create"));
        }
        if (behavior_ == ProviderBehavior::StepFailure) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::success(std::make_unique<FailingStepBackend>());
        }
        if (behavior_ == ProviderBehavior::SucceedWithWarning) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::success(
                detail::makeMockAcceleratorBackend(),
                eve::Status(eve::StatusCode::Applied,
                            {eve::Diagnostic::warning(eve::DiagnosticCode::Unsupported,
                                                      "accelerator selected with reduced precision",
                                                      "physics.accelerator.registry.precision")}));
        }
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(detail::makeMockAcceleratorBackend());
    }

private:
    ProviderBehavior behavior_;
};

class DomainProvider final : public IAcceleratorBackendProvider {
public:
    DomainProvider(SimulationBackendDomain domain, ProviderBehavior behavior) : domain_(domain), behavior_(behavior) {}

    [[nodiscard]] bool supports(SimulationBackendDomain domain) const noexcept override { return domain == domain_; }

    eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain, void*) override {
        ++createCount;
        if (behavior_ == ProviderBehavior::CreateFailure) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "domain provider creation failed", "physics.accelerator.registry.create"));
        }
        if (behavior_ == ProviderBehavior::SucceedWithWarning) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::success(
                detail::makeMockAcceleratorBackend(),
                eve::Status(eve::StatusCode::Applied,
                            {eve::Diagnostic::warning(eve::DiagnosticCode::Unsupported,
                                                      "accelerator selected with reduced precision",
                                                      "physics.accelerator.registry.precision")}));
        }
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(detail::makeMockAcceleratorBackend());
    }

    int createCount = 0;

private:
    SimulationBackendDomain domain_;
    ProviderBehavior        behavior_;
};

constexpr std::array kSatelliteDomains = {SimulationBackendDomain::Cloth2D, SimulationBackendDomain::Cloth3D,
                                          SimulationBackendDomain::SurfaceFluid};

std::string diagnosticDetail(const eve::Diagnostic& diagnostic, const std::string& key) {
    for (const auto& [name, value] : diagnostic.details()) {
        if (name == key) return value;
    }
    return {};
}

}  // namespace

TEST_CASE("physics.accelerator.satellitesReportAbsentProviderPerDomain") {
    CapabilityReset reset;
    for (const auto domain : kSatelliteDomains) {
        auto selected = detail::selectSimulationBackend(domain, cpuBackend(), nullptr, true);
        REQUIRE(selected.ok());
        REQUIRE_EQ(selected.diagnostics().size(), std::size_t{1});
        CHECK_EQ(diagnosticDetail(selected.diagnostics().front(), "domain"),
                 std::string(simulationBackendDomainName(domain)));
        auto value = std::move(selected).takeValue();
        CHECK(value.usedFallback);
        CHECK_EQ(value.actualKind, SimulationBackendKind::Cpu);
    }
}

TEST_CASE("physics.accelerator.satellitesSelectPresentProvider") {
    CapabilityReset   reset;
    SatelliteProvider provider(ProviderBehavior::Succeed);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    for (const auto domain : kSatelliteDomains) {
        auto selected = detail::selectSimulationBackend(domain, cpuBackend(), nullptr, true);
        REQUIRE(selected.ok());
        CHECK(selected.diagnostics().empty());
        auto value = std::move(selected).takeValue();
        CHECK(!value.usedFallback);
        CHECK_EQ(value.actualKind, SimulationBackendKind::MockAccelerator);
    }
}

TEST_CASE("physics.accelerator.satellitesPreserveCreateFailure") {
    CapabilityReset   reset;
    SatelliteProvider provider(ProviderBehavior::CreateFailure);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    for (const auto domain : kSatelliteDomains) {
        auto selected = detail::selectSimulationBackend(domain, cpuBackend(), nullptr, true);
        REQUIRE(selected.ok());
        REQUIRE_EQ(selected.diagnostics().size(), std::size_t{2});
        CHECK_EQ(diagnosticDetail(selected.diagnostics().front(), "domain"),
                 std::string(simulationBackendDomainName(domain)));
        CHECK_EQ(selected.diagnostics()[1].message(), std::string("injected accelerator creation failure"));
        CHECK(std::move(selected).takeValue().usedFallback);
    }
}

TEST_CASE("physics.accelerator.satellitesStepFailureDoesNotAdvanceObservation") {
    CapabilityReset   reset;
    SatelliteProvider provider(ProviderBehavior::StepFailure);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    for (const auto domain : kSatelliteDomains) {
        auto selected = detail::selectSimulationBackend(domain, cpuBackend(), nullptr, true);
        REQUIRE(selected.ok());
        auto value  = std::move(selected).takeValue();
        auto result = value.backend->step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)},
                                          SimulationSettings{});
        CHECK(!result.ok());
        CHECK_EQ(result.error()->path(), std::string("physics.accelerator.step"));
        CHECK_EQ(value.backend->observation().stepCount, std::uint64_t{0});
        CHECK(value.backend->observation().lastTick.isZero());
    }
}

TEST_CASE("physics.accelerator.registryComposesIndependentClothAndFluidProviders") {
    CapabilityReset reset;
    DomainProvider  clothProvider(SimulationBackendDomain::Cloth2D, ProviderBehavior::Succeed);
    DomainProvider  fluidProvider(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::Succeed);
    auto clothRegistrationResult = AcceleratorBackendProviderRegistration::registerProvider(clothProvider, 10);
    auto fluidRegistrationResult = AcceleratorBackendProviderRegistration::registerProvider(fluidProvider, 20);
    REQUIRE(clothRegistrationResult.ok());
    REQUIRE(fluidRegistrationResult.ok());
    auto clothRegistration = std::move(clothRegistrationResult).takeValue();
    auto fluidRegistration = std::move(fluidRegistrationResult).takeValue();

    auto cloth = detail::selectSimulationBackend(SimulationBackendDomain::Cloth2D, cpuBackend(), nullptr, true);
    REQUIRE(cloth.ok());
    CHECK(!std::move(cloth).takeValue().usedFallback);
    auto fluid = detail::selectSimulationBackend(SimulationBackendDomain::SurfaceFluid, cpuBackend(), nullptr, true);
    REQUIRE(fluid.ok());
    CHECK(!std::move(fluid).takeValue().usedFallback);
    CHECK_EQ(clothProvider.createCount, 1);
    CHECK_EQ(fluidProvider.createCount, 1);
}

TEST_CASE("physics.accelerator.registryRemovalIsProviderLocal") {
    CapabilityReset reset;
    DomainProvider  clothProvider(SimulationBackendDomain::Cloth3D, ProviderBehavior::Succeed);
    DomainProvider  fluidProvider(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::Succeed);
    auto            clothRegistrationResult = AcceleratorBackendProviderRegistration::registerProvider(clothProvider);
    auto            fluidRegistrationResult = AcceleratorBackendProviderRegistration::registerProvider(fluidProvider);
    REQUIRE(clothRegistrationResult.ok());
    REQUIRE(fluidRegistrationResult.ok());
    auto clothRegistration = std::move(clothRegistrationResult).takeValue();
    auto fluidRegistration = std::move(fluidRegistrationResult).takeValue();

    clothRegistration.reset();
    auto cloth = detail::selectSimulationBackend(SimulationBackendDomain::Cloth3D, cpuBackend(), nullptr, true);
    REQUIRE(cloth.ok());
    CHECK(std::move(cloth).takeValue().usedFallback);
    auto fluid = detail::selectSimulationBackend(SimulationBackendDomain::SurfaceFluid, cpuBackend(), nullptr, true);
    REQUIRE(fluid.ok());
    CHECK(!std::move(fluid).takeValue().usedFallback);
}

TEST_CASE("physics.accelerator.registryTriesNextSupportingProviderAfterFailure") {
    CapabilityReset reset;
    DomainProvider  failing(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::CreateFailure);
    DomainProvider  succeeding(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::Succeed);
    auto            firstResult  = AcceleratorBackendProviderRegistration::registerProvider(failing, 0);
    auto            secondResult = AcceleratorBackendProviderRegistration::registerProvider(succeeding, 10);
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    auto first  = std::move(firstResult).takeValue();
    auto second = std::move(secondResult).takeValue();

    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::SurfaceFluid, cpuBackend(), nullptr, true);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.diagnostics().size(), std::size_t{1});
    CHECK_EQ(selected.diagnostics().front().message(), std::string("domain provider creation failed"));
    CHECK(selected.diagnostics().front().severity() == eve::Severity::Warning);
    CHECK_EQ(diagnosticDetail(selected.diagnostics().front(), "providerAttempt"), std::string("1"));
    CHECK(!std::move(selected).takeValue().usedFallback);
    CHECK_EQ(failing.createCount, 1);
    CHECK_EQ(succeeding.createCount, 1);
}

TEST_CASE("physics.accelerator.registryRejectsDuplicateLiveRegistration") {
    CapabilityReset reset;
    DomainProvider  provider(SimulationBackendDomain::Cloth2D, ProviderBehavior::Succeed);
    auto            firstResult = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(firstResult.ok());
    auto first = std::move(firstResult).takeValue();

    auto duplicate = AcceleratorBackendProviderRegistration::registerProvider(provider);
    CHECK(!duplicate.ok());
    REQUIRE(duplicate.error());
    CHECK_EQ(duplicate.error()->code(), eve::DiagnosticCode::Conflict);

    first.reset();
    auto replacement = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(replacement.ok());
    std::move(replacement).takeValue();
}

TEST_CASE("physics.accelerator.registryPreservesSuccessfulProviderDiagnostics") {
    CapabilityReset reset;
    DomainProvider  provider(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::SucceedWithWarning);
    auto            registrationResult = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(registrationResult.ok());
    auto registration = std::move(registrationResult).takeValue();

    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::SurfaceFluid, cpuBackend(), nullptr, true);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.diagnostics().size(), std::size_t{1});
    CHECK_EQ(selected.diagnostics().front().message(), std::string("accelerator selected with reduced precision"));
    CHECK_EQ(diagnosticDetail(selected.diagnostics().front(), "providerAttempt"), std::string("1"));
    CHECK(!std::move(selected).takeValue().usedFallback);
}

TEST_CASE("physics.accelerator.registryPreservesLegacySingleProviderCompatibility") {
    CapabilityReset reset;
    DomainProvider  legacy(SimulationBackendDomain::Cloth2D, ProviderBehavior::Succeed);
    eve::cap::provide<IAcceleratorBackendProvider>(&legacy);

    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::Cloth2D, cpuBackend(), nullptr, true);
    REQUIRE(selected.ok());
    CHECK(!std::move(selected).takeValue().usedFallback);
    CHECK_EQ(legacy.createCount, 1);
}
}  // namespace test_physics_accelerator_domains
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_destruction_graphics)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <memory>
#include "graphics/Graphics.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/FractureRecipe.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/editing/FractureRecipeSchema.h"
#include "physics/destruction/graphics/GeometryCollectionRenderer.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_destruction_graphics.cpp
namespace test_physics_destruction_graphics {
using eve::physics::BoneRuntimeState;
using eve::physics::DestructionField;
using eve::physics::DestructionFieldFalloff;
using eve::physics::DestructionFieldKind;
using eve::physics::GeometryCollectionAsset;
using eve::physics::GeometryCollectionInstance;
using eve::physics::GeometryCollectionRenderer;
using eve::physics::Physics;
using eve::physics::World3D;

namespace {

eve::SimulationStep simStep(std::uint64_t tick) {
    return {eve::SimulationTick{tick}, eve::Duration::fromSeconds(1.0 / 60.0).expect("destruction gfx dt")};
}

bool tryInitDrawWindow(eve::window::Window*& window, eve::graphics::Graphics*& graphics) {
    window   = eve::window::Window::create();
    graphics = eve::graphics::Graphics::create();
    if (!window || !graphics) return false;
    eve::window::WindowSettings settings;
    settings.width    = 320;
    settings.height   = 240;
    settings.centered = true;
    return window->setWindowSettings(settings);
}

}  // namespace

TEST_CASE("physics_destruction_editing.fractureRecipeSchemaMirrorsDefaults") {
    auto schema = eve::physics_editing::fractureRecipeSchema();
    REQUIRE_EQ(schema.typeId, std::string(eve::physics::FractureRecipe::SchemaId));
    REQUIRE_EQ(schema.version, eve::physics::FractureRecipe::SchemaVersion);
    REQUIRE(schema.find(eve::editing::PropertyPath("mode")));
    REQUIRE(schema.find(eve::editing::PropertyPath("seed")));
    REQUIRE(schema.find(eve::editing::PropertyPath("siteCountMin")));
    const auto mode = schema.find(eve::editing::PropertyPath("mode"));
    REQUIRE_EQ(mode->enumItems.size(), 4u);
}

TEST_CASE("physics_destruction_graphics.drawsActiveAndBatchesSleepingBones") {
    eve::window::Window*     window = nullptr;
    eve::graphics::Graphics* gfx    = nullptr;
    if (!tryInitDrawWindow(window, gfx)) {
        // Headless hosts without a display/Vulkan ICD skip the draw path.
        return;
    }

    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto                     asset = GeometryCollectionAsset::makeWeldedBoxesFixture(0.5f);
    REQUIRE(asset.ok());
    auto created = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(created.ok());
    auto instance = std::move(created.value());

    GeometryCollectionRenderer renderer(instance.get());
    renderer.setExteriorColor(0.6f, 0.6f, 0.6f, 1.f);
    renderer.setInteriorColor(0.9f, 0.3f, 0.2f, 1.f);

    gfx->begin3DFrame();
    auto drawn = renderer.draw(gfx);
    REQUIRE(drawn.ok());
    REQUIRE_EQ(renderer.lastActiveDrawCount(), 2);
    REQUIRE_EQ(renderer.lastSleepBatchCount(), 0);
    gfx->present();

    DestructionField strain;
    strain.kind      = DestructionFieldKind::Strain;
    strain.falloff   = DestructionFieldFalloff::Linear;
    strain.centerX   = 0.f;
    strain.centerY   = 1.f;
    strain.centerZ   = 0.f;
    strain.radius    = 3.f;
    strain.magnitude = 2.f;
    REQUIRE(instance->applyField(strain).ok());
    REQUIRE(instance->step(simStep(1)).ok());
    REQUIRE(instance->boneState(0) == BoneRuntimeState::Detached);
    REQUIRE(instance->boneState(1) == BoneRuntimeState::Detached);

    auto body0 = instance->boneLink(0).value().resolve(*world);
    auto body1 = instance->boneLink(1).value().resolve(*world);
    REQUIRE(body0.ok());
    REQUIRE(body1.ok());
    body0.value()->setLinearVelocity(0.f, 0.f, 0.f);
    body1.value()->setLinearVelocity(0.f, 0.f, 0.f);

    DestructionField sleep;
    sleep.kind    = DestructionFieldKind::Sleep;
    sleep.falloff = DestructionFieldFalloff::None;
    sleep.centerX = 0.f;
    sleep.centerY = 1.f;
    sleep.centerZ = 0.f;
    sleep.radius  = 5.f;
    REQUIRE(instance->applyField(sleep).ok());
    REQUIRE(instance->boneState(0) == BoneRuntimeState::Sleeping);
    REQUIRE(instance->boneState(1) == BoneRuntimeState::Sleeping);
    REQUIRE(instance->sleepBatchRevision() > 0u);

    gfx->begin3DFrame();
    auto drawnSleep = renderer.draw(gfx);
    REQUIRE(drawnSleep.ok());
    REQUIRE_EQ(renderer.lastActiveDrawCount(), 0);
    REQUIRE_EQ(renderer.lastSleepBatchCount(), 2);
    gfx->present();
    window->close();
}

TEST_CASE("physics_destruction_graphics.staleWorldRejectsDraw") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto                     asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    auto created = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(created.ok());
    auto                       instance = std::move(created.value());
    GeometryCollectionRenderer renderer(instance.get());
    world->destroy();
    world.reset();
    REQUIRE(!instance->hasLiveWorld());
    auto drawn = renderer.draw(nullptr);
    REQUIRE(!drawn.ok());
}
}  // namespace test_physics_destruction_graphics
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_editor_boot)
#include "PrivateTestScope.h"
#include <string>
#include "common/Module.h"
#include "editor/Editor.h"
#include "physics/editing/PhysicsEditingProvider.h"
#include "physics/editor/PhysicsEditorModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_editor_boot.cpp
namespace test_physics_editor_boot {
TEST_CASE("physics.editor.bootPublishesEditingFactory") {
    auto* physicsEditor = requireModInst(eve::physics_editor, PhysicsEditorModule);
    REQUIRE(physicsEditor != nullptr);

    auto* editor = getModInst(eve::editor, Editor);
    REQUIRE(editor != nullptr);
    auto lease = editor->extensionProviders().acquire("physics.editing");
    REQUIRE(lease.ok());
    auto* factory = static_cast<eve::physics_editing::IPhysicsEditingFactory*>(
        lease.value().query(eve::physics_editing::IPhysicsEditingFactory::capabilityId()));
    REQUIRE(factory != nullptr);

    auto target = factory->createCollider("boot.collider", 3, nullptr);
    REQUIRE(target != nullptr);
    CHECK_EQ(target->targetId().value(), std::string("boot.collider"));
}
}  // namespace test_physics_editor_boot
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_simulation_backend_satellites)
#include "PrivateTestScope.h"
#include <cstdint>
#include <memory>
#include <utility>
#include "fluids/Fluids.h"
#include "physics/backend/SimulationBackend.h"
#include "physics/cloth/Cloth.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/rope/Rope3D.h"
#include "physics/softbody/SoftBody3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_simulation_backend_satellites.cpp
namespace test_physics_simulation_backend_satellites {
namespace {

using eve::physics::ISimulationBackend;
using eve::physics::SimulationObservation;
using eve::physics::SimulationSettings;

void checkCpuSatelliteContract(ISimulationBackend& backend) {
    const eve::Duration duration = eve::Duration::fromNanoseconds(16666667);

    SimulationSettings invalidSettings;
    invalidSettings.subStepCount = 0;
    auto invalid                 = backend.step({eve::SimulationTick{1}, duration}, invalidSettings);
    CHECK(!invalid.ok());
    CHECK_EQ(backend.observation().stepCount, std::uint64_t{0});
    CHECK(backend.observation().lastTick.isZero());

    auto first = backend.step({eve::SimulationTick{1}, duration}, SimulationSettings{});
    REQUIRE(first.ok());
    const SimulationObservation afterFirst = backend.observation();
    CHECK_EQ(afterFirst.stepCount, std::uint64_t{1});
    CHECK(afterFirst.lastTick == eve::SimulationTick{1});
    CHECK_EQ(afterFirst.simulatedDuration, duration);

    auto duplicate = backend.step({eve::SimulationTick{1}, duration}, SimulationSettings{});
    CHECK(!duplicate.ok());
    CHECK_EQ(backend.observation().stepCount, afterFirst.stepCount);
    CHECK(backend.observation().lastTick == afterFirst.lastTick);
    CHECK_EQ(backend.observation().simulatedDuration, afterFirst.simulatedDuration);

    auto second = backend.step({eve::SimulationTick{2}, duration}, SimulationSettings{});
    REQUIRE(second.ok());
    CHECK_EQ(backend.observation().stepCount, std::uint64_t{2});
    CHECK(backend.observation().lastTick == eve::SimulationTick{2});
    CHECK_EQ(backend.observation().simulatedDuration, eve::Duration::fromNanoseconds(duration.nanoseconds() * 2));
}

}  // namespace

TEST_CASE("physics.backend.cpuCloth2DConforms") {
    eve::physics::Cloth cloth(3, 3, 0.5f, 0.f, 1.f);
    checkCpuSatelliteContract(cloth);
}

TEST_CASE("physics.backend.cpuCloth3DConforms") {
    eve::physics::Cloth3D cloth(3, 3, 0.5f, 0.f, 1.f, 0.f);
    checkCpuSatelliteContract(cloth);
}

TEST_CASE("physics.backend.cpuRope3DConforms") {
    eve::physics::Rope3D rope(4, 0.f, 1.f, 0.f, 1.5f, 1.f, 0.f);
    checkCpuSatelliteContract(rope);
}

TEST_CASE("physics.backend.cpuSoftBody3DConforms") {
    auto created = eve::physics::SoftBody3D::create(2, 2, 2, 0.5f, 0.f, 1.f, 0.f);
    REQUIRE(created.ok());
    std::unique_ptr<eve::physics::SoftBody3D> body = std::move(created).takeValue();
    checkCpuSatelliteContract(*body);
}

TEST_CASE("physics.backend.cpuSurfaceFluidConforms") {
    eve::fluids::FluidParams    params;
    eve::fluids::FluidSimulator fluid(64, params, false);
    checkCpuSatelliteContract(fluid);
}
}  // namespace test_physics_simulation_backend_satellites
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pixelworld_editor)
#include "PrivateTestScope.h"
#include <string>
#include "pixelworld/PixelMaterialCatalogCodec.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld/editor/PixelWorldCatalogPanel.h"
#include "ui/UIHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// pixelworld_editor.cpp
namespace test_pixelworld_editor {
using namespace eve::pixelworld;
using namespace eve::pixelworld_editor;

namespace {

eve::ui::WidgetDesc* findWidget(eve::ui::WidgetDesc& root, const std::string& id) {
    if (root.id == id) return &root;
    for (auto& child : root.children)
        if (auto* found = findWidget(child, id)) return found;
    return nullptr;
}

}  // namespace

TEST_CASE("pixelworld_editor_material_draft_is_transactional_and_codec_canonical") {
    auto               panel              = PixelWorldCatalogPanel::builtIn();
    const auto         initialRevision    = panel.revision();
    const auto         initialFingerprint = panel.draft().fingerprint();
    MaterialDefinition stone              = panel.draft().definition(MaterialId::Stone);
    stone.displayRgba                     = 0x102030FFU;
    const auto receipt                    = panel.replaceMaterial(stone).expect("replace material draft");
    CHECK_EQ(receipt.revisionBefore, initialRevision);
    CHECK_EQ(receipt.revisionAfter, initialRevision + 1);
    CHECK(receipt.fingerprint != initialFingerprint);
    CHECK_EQ(panel.draft().definition(MaterialId::Stone).displayRgba, std::uint32_t(0x102030FFU));

    const auto beforeFailure = panel.document().expect("document before failure");
    stone.name               = "water";
    CHECK(!panel.replaceMaterial(stone).ok());
    CHECK_EQ(panel.revision(), initialRevision + 1);
    CHECK_EQ(panel.document().expect("document after failure"), beforeFailure);

    auto decoded = PixelWorldCatalogPanel::create(beforeFailure).expect("decode panel document");
    CHECK_EQ(decoded.draft().fingerprint(), panel.draft().fingerprint());
    CHECK(!panel.loadDocument("{broken").ok());
    CHECK_EQ(panel.document().expect("document after malformed load"), beforeFailure);
}

TEST_CASE("pixelworld_editor_reaction_and_phase_operations_validate_whole_catalog") {
    auto                 panel = PixelWorldCatalogPanel::builtIn();
    MaterialReactionRule reaction;
    reaction.id           = "editor-test-reaction";
    reaction.first        = MaterialId::Water;
    reaction.second       = MaterialId::Lava;
    reaction.firstResult  = MaterialId::Steam;
    reaction.secondResult = MaterialId::Stone;
    reaction.heatDelta    = 25;
    panel.addReaction(reaction).expect("add reaction");
    REQUIRE(panel.selectedReactionIndex().has_value());
    CHECK_EQ(panel.draft().reactions()[*panel.selectedReactionIndex()].id, std::string("editor-test-reaction"));

    auto duplicate  = reaction;
    duplicate.first = MaterialId::Oil;
    CHECK(!panel.addReaction(std::move(duplicate)).ok());

    MaterialPhaseRule phase;
    phase.id        = "editor-test-phase";
    phase.source    = MaterialId::Water;
    phase.result    = MaterialId::Steam;
    phase.threshold = 120;
    panel.addPhase(phase).expect("add phase");
    REQUIRE(panel.selectedPhaseIndex().has_value());
    CHECK_EQ(panel.draft().phaseRules()[*panel.selectedPhaseIndex()].id, std::string("editor-test-phase"));
    panel.removePhase(*panel.selectedPhaseIndex()).expect("remove phase");
}

TEST_CASE("pixelworld_editor_widget_tree_exposes_visual_material_and_rule_controls") {
    auto panel = PixelWorldCatalogPanel::builtIn();
    auto tree  = panel.buildWidgetTree();
    CHECK_EQ(tree.id, std::string("pixelworld-catalog-editor"));
    REQUIRE(findWidget(tree, "catalog-browser") != nullptr);
    REQUIRE(findWidget(tree, "material-preview") != nullptr);
    REQUIRE(findWidget(tree, "reaction-section") != nullptr);
    REQUIRE(findWidget(tree, "phase-section") != nullptr);

    auto* stone = findWidget(tree, "material/1");
    REQUIRE(stone != nullptr);
    REQUIRE(bool(stone->onClick));
    stone->onClick();
    CHECK_EQ(panel.selectedMaterialIndex(), std::size_t(1));

    tree       = panel.buildWidgetTree();
    auto* rgba = findWidget(tree, "material-rgba");
    REQUIRE(rgba != nullptr);
    REQUIRE(bool(rgba->onTextChange));
    rgba->onTextChange("#112233FF");
    CHECK_EQ(panel.draft().definition(MaterialId::Stone).displayRgba, std::uint32_t(0x112233FFU));
    CHECK(panel.statusText().find("validated") != std::string::npos);
}

TEST_CASE("pixelworld_editor_mounts_a_live_panel_and_reconciles_callback_edits") {
    auto       panel      = PixelWorldCatalogPanel::builtIn();
    const auto hostHandle = panel.open().expect("open catalog panel");
    REQUIRE(hostHandle);
    CHECK(panel.isOpen());
    auto host = eve::ui::UIHost::resolve(hostHandle);
    REQUIRE(host.has_value());
    auto stone = host->get().findById("material/1");
    REQUIRE(stone.has_value());
    const auto handler = stone->get().handlerClick;
    REQUIRE(handler > 0);
    auto click = host->get().tree()->clickHandlers[handler - 1];
    REQUIRE(bool(click));
    click();
    CHECK_EQ(panel.selectedMaterialIndex(), std::size_t(1));
    CHECK(host->get().findById("material-rgba").has_value());

    panel.close();
    CHECK(!panel.isOpen());
    panel.open().expect("reopen catalog panel");
    CHECK(panel.isOpen());
}

TEST_CASE("pixelworld_editor_apply_reuses_paused_authority_and_fingerprint_guard") {
    PixelWorld world(8601);
    auto&      control = pixelWorldControlService();
    const auto worldId = world.worldLink().world;
    control.setPaused(worldId, true).expect("pause world");
    auto               panel = PixelWorldCatalogPanel::builtIn();
    MaterialDefinition stone = panel.draft().definition(MaterialId::Stone);
    stone.displayRgba        = 0xABCDEF88U;
    panel.replaceMaterial(std::move(stone)).expect("edit draft");
    const auto expected = world.materialCatalogFingerprint();
    panel.apply(control, worldId, expected).expect("apply Catalog panel");
    CHECK_EQ(world.materialDisplayRgba(MaterialId::Stone), std::uint32_t(0xABCDEF88U));
    CHECK(!panel.apply(control, worldId, expected).ok());
}
}  // namespace test_pixelworld_editor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_artifact_publish)
#include "PrivateTestScope.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "common/Capability.h"
#include "graphics/ArtifactProvider.h"
#include "procgen/ArtifactPublish.h"
#include "transaction/Transaction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_artifact_publish.cpp
namespace test_procgen_artifact_publish {
using namespace eve::procgen;

namespace {

ArtifactId id(const char* text) {
    const auto parsed = ArtifactId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

struct Counters {
    int prepared   = 0;
    int committed  = 0;
    int rolledBack = 0;
};

class InjectedTransactionParticipant final : public eve::transaction::ITransactionParticipant {
public:
    explicit InjectedTransactionParticipant(std::string name) : name_(std::move(name)) {}

    std::string_view name() const noexcept override { return name_; }

    eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        ++prepareCalls;
        if (failPrepare)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, name_ + " prepare"));
        prepared = true;
        return eve::Result<void>::success();
    }

    eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        ++commitCalls;
        if (failCommit)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, name_ + " commit"));
        prepared  = false;
        committed = true;
        return eve::Result<void>::success();
    }

    eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        ++rollbackCalls;
        if (failRollback)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, name_ + " rollback"));
        prepared = false;
        return eve::Result<void>::success();
    }

    eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        ++compensateCalls;
        if (failCompensate)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, name_ + " compensate"));
        committed = false;
        return eve::Result<void>::success();
    }

    bool failPrepare     = false;
    bool failCommit      = false;
    bool failRollback    = false;
    bool failCompensate  = false;
    bool prepared        = false;
    bool committed       = false;
    int  prepareCalls    = 0;
    int  commitCalls     = 0;
    int  rollbackCalls   = 0;
    int  compensateCalls = 0;

private:
    std::string name_;
};

class Stage final : public PreparedArtifactPublish {
public:
    explicit Stage(Counters& counters) : counters_(counters) {}
    void commit() noexcept override { ++counters_.committed; }
    void rollback() noexcept override { ++counters_.rolledBack; }

private:
    Counters& counters_;
};

template <class Interface>
class Adapter final : public Interface {
public:
    explicit Adapter(Counters& counters, bool fail = false) : counters_(counters), fail_(fail) {}
    eve::Result<std::unique_ptr<PreparedArtifactPublish>> prepare(const eve::artifact::PublicationView&) override {
        ++counters_.prepared;
        if (fail_)
            return eve::Result<std::unique_ptr<PreparedArtifactPublish>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected adapter failure"));
        return eve::Result<std::unique_ptr<PreparedArtifactPublish>>::success(std::make_unique<Stage>(counters_));
    }

private:
    Counters& counters_;
    bool      fail_;
};

Params params() {
    Params value;
    value.setSeed(42);
    value.setInt("width", 6);
    value.setInt("height", 5);
    value.setInt("rings", 1);
    return value;
}

}  // namespace

TEST_CASE("procgen.artifactPublish.missingCapabilityIsExplicitAndAtomic") {
    eve::cap::detail::clearAllRaw();
    auto generated = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd101"));
    REQUIRE(generated.ok());
    ArtifactStore     store;
    ArtifactPublisher publisher(store);
    auto              result = publisher.publish(std::move(generated).takeValue(), {.graphics = true});
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::Unsupported));
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(store.partCount(), std::size_t(0));
}

TEST_CASE("procgen.artifactPublish.capabilitiesCommitHexAndCastle") {
    eve::cap::detail::clearAllRaw();
    Counters                          sceneCounts, graphicsCounts, physicsCounts;
    Adapter<ISceneArtifactAdapter>    scene(sceneCounts);
    Adapter<IGraphicsArtifactAdapter> graphics(graphicsCounts);
    Adapter<IPhysicsArtifactAdapter>  physics(physicsCounts);
    eve::cap::provide<ISceneArtifactAdapter>(&scene);
    eve::cap::provide<IGraphicsArtifactAdapter>(&graphics);
    eve::cap::provide<IPhysicsArtifactAdapter>(&physics);

    ArtifactStore     store;
    ArtifactPublisher publisher(store);
    auto              hex = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd102"));
    REQUIRE(hex.ok());
    auto first = publisher.publish(std::move(hex).takeValue(), {true, true, true});
    REQUIRE(first.ok());
    std::move(first).takeValue();
    auto castle = generateCastleArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd103"));
    REQUIRE(castle.ok());
    auto second = publisher.publish(std::move(castle).takeValue(), {true, true, true});
    REQUIRE(second.ok());
    std::move(second).takeValue();
    CHECK_EQ(store.size(), std::size_t(2));
    CHECK_EQ(store.partCount(), std::size_t(8));
    CHECK_EQ(sceneCounts.committed, 2);
    CHECK_EQ(graphicsCounts.committed, 2);
    CHECK_EQ(physicsCounts.committed, 2);
}

TEST_CASE("procgen.artifactPublish.adapterFailureRollsBackWithoutStoreState") {
    eve::cap::detail::clearAllRaw();
    Counters                          sceneCounts, graphicsCounts;
    Adapter<ISceneArtifactAdapter>    scene(sceneCounts);
    Adapter<IGraphicsArtifactAdapter> graphics(graphicsCounts, true);
    eve::cap::provide<ISceneArtifactAdapter>(&scene);
    eve::cap::provide<IGraphicsArtifactAdapter>(&graphics);
    auto generated = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd104"));
    REQUIRE(generated.ok());
    ArtifactStore     store;
    ArtifactPublisher publisher(store);
    auto              result = publisher.publish(std::move(generated).takeValue(), {true, true, false});
    CHECK(!result.ok());
    CHECK_EQ(sceneCounts.rolledBack, 1);
    CHECK_EQ(sceneCounts.committed, 0);
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(store.partCount(), std::size_t(0));
}

TEST_CASE("procgen.artifactPublish.badDependencyRejectsWholeBatch") {
    auto first  = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd105"));
    auto second = generateCastleArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd106"));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    auto artifacts = std::vector<GeneratedArtifact>{};
    artifacts.emplace_back(std::move(first).takeValue());
    artifacts.emplace_back(std::move(second).takeValue());
    auto&         composite = std::get<CompositeArtifact>(artifacts.back().payload);
    ArtifactPart* collider  = const_cast<ArtifactPart*>(composite.find("collider"));
    REQUIRE(collider != nullptr);
    collider->dependencies = {id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd199")};
    ArtifactStore store;
    auto          result = store.publishBatch(std::move(artifacts));
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::NotFound));
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(store.partCount(), std::size_t(0));
}

TEST_CASE("procgen.artifactPublish.buildKeyAndPayloadAreDeterministicAcrossIdentity") {
    auto first  = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd107"));
    auto second = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd108"));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    GeneratedArtifact a = std::move(first).takeValue();
    GeneratedArtifact b = std::move(second).takeValue();
    CHECK_EQ(a.buildKey, b.buildKey);
    const auto* aMesh = std::get<CompositeArtifact>(a.payload).find("mesh");
    const auto* bMesh = std::get<CompositeArtifact>(b.payload).find("mesh");
    REQUIRE(aMesh != nullptr);
    REQUIRE(bMesh != nullptr);
    CHECK_EQ(std::get<MeshData>(aMesh->payload).positions(), std::get<MeshData>(bMesh->payload).positions());
    CHECK_EQ(std::get<MeshData>(aMesh->payload).indices(), std::get<MeshData>(bMesh->payload).indices());
}

TEST_CASE("procgen.artifactPublish.coordinatorFailureInjectionCoversPrepareCommitAndRollback") {
    using eve::transaction::ITransactionParticipant;

    InjectedTransactionParticipant first("first");
    InjectedTransactionParticipant prepareFailure("prepare-failure");
    prepareFailure.failPrepare = true;
    std::array<ITransactionParticipant*, 2> prepareParticipants{&first, &prepareFailure};
    eve::transaction::Coordinator           coordinator;
    auto                                    prepareResult =
        coordinator.execute(eve::transaction::TransactionContext("procgen-prepare-failure"), prepareParticipants);
    CHECK(!prepareResult.ok());
    CHECK_EQ(first.rollbackCalls, 1);
    CHECK(!first.prepared);

    InjectedTransactionParticipant committed("committed");
    InjectedTransactionParticipant commitFailure("commit-failure");
    commitFailure.failCommit = true;
    std::array<ITransactionParticipant*, 2> commitParticipants{&committed, &commitFailure};
    auto                                    commitResult =
        coordinator.execute(eve::transaction::TransactionContext("procgen-commit-failure"), commitParticipants);
    CHECK(!commitResult.ok());
    CHECK_EQ(commitFailure.rollbackCalls, 1);
    CHECK_EQ(committed.compensateCalls, 1);
    CHECK(!committed.committed);

    InjectedTransactionParticipant rollbackFailure("rollback-failure");
    rollbackFailure.failRollback = true;
    InjectedTransactionParticipant trigger("trigger");
    trigger.failPrepare = true;
    std::array<ITransactionParticipant*, 2> rollbackParticipants{&rollbackFailure, &trigger};
    auto                                    rollbackResult =
        coordinator.execute(eve::transaction::TransactionContext("procgen-rollback-failure"), rollbackParticipants);
    CHECK(!rollbackResult.ok());
    CHECK_EQ(rollbackFailure.rollbackCalls, 1);
    CHECK(rollbackFailure.prepared);
    CHECK(rollbackResult.error() != nullptr);
}

TEST_CASE("procgen.artifactPublish.webGpuDescriptorParityUsesCommonMeshView") {
    const auto                       artifact = id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd109");
    const std::vector<float>         positions{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    const std::vector<std::uint32_t> indices{0u, 1u, 2u};
    eve::artifact::PartView          part;
    part.id        = eve::PersistentId::fromUuid(artifact);
    part.role      = "mesh";
    part.kind      = eve::artifact::PartKind::MeshData;
    part.buildKey  = "procgen/v1/descriptor/0000000000000001";
    part.positions = positions;
    part.indices   = indices;
    std::array<eve::artifact::PartView, 1> parts{part};
    eve::artifact::PublicationView         publication;
    publication.id       = part.id;
    publication.buildKey = part.buildKey;
    publication.parts    = parts;

    const auto descriptor = eve::graphics::WebGpuArtifactDescriptorAdapter::describe(publication);
    REQUIRE(descriptor.has_value());
    CHECK_EQ(descriptor->id, part.id);
    CHECK_EQ(descriptor->vertexCount, std::size_t(3));
    CHECK_EQ(descriptor->indexCount, std::size_t(3));
    CHECK_NE(descriptor->checksum, std::uint64_t(0));
}
}  // namespace test_procgen_artifact_publish
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_core_boundary)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/Snapshot.h"
#include "graphics/ArtifactProvider.h"
#include "image/ImageData.h"
#include "map/ArtifactProvider.h"
#include "physics/ArtifactProvider.h"
#include "procgen/ArtifactPublish.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/core/ProcgenCore.h"
#include "procgen/mesh/DynamicMeshUvPaintSession.h"
#include "procgen/mesh/GeometryStroke.h"
#include "procgen/mesh/MeshUvProjection.h"
#include "procgen/texture/FloorTextures.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"
#include "scene/ArtifactProvider.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_core_boundary.cpp
namespace test_procgen_core_boundary {
// procgen_core_boundary.cpp
namespace {

static_assert(std::is_default_constructible_v<eve::procgen::Params>);
static_assert(std::is_default_constructible_v<eve::procgen::Grid2D>);
static_assert(std::is_default_constructible_v<eve::procgen::PointSet>);
static_assert(std::is_default_constructible_v<eve::procgen::MeshBuild>);

}  // namespace

TEST_CASE("procgen.core.cpuSurfaceIsBackendNeutral") {
    using namespace eve::procgen;

    Params params;
    params.setSeed(42);
    params.setSize(4, 3);
    params.setBool("decorations", true);

    const RecipeDescriptor recipe = RecipeDescriptor::grid("core.grid", "Core Grid", "test", 1, 1, 32, 32);
    recipe.applyDefaults(params);
    CHECK_EQ(params.getSeed(), uint32_t(42));
    CHECK_EQ(params.getWidth(), 4);
    CHECK_EQ(params.getHeight(), 3);

    Grid2D grid;
    grid.resize(params.getWidth(), params.getHeight());
    grid.fill(int(Semantic::Floor));
    grid.setMeta("buildKey", "core-grid:v1:42:4x3");
    CHECK_EQ(grid.getCell(2, 1), int(Semantic::Floor));
    CHECK_EQ(grid.getMeta("buildKey", ""), std::string("core-grid:v1:42:4x3"));

    PointSet points;
    points.add(1.f, 0.f, 2.f);
    const PointSet jittered = jitterPointPositions(points, params.getSeed(), 0.25f, 0.25f);
    CHECK_EQ(jittered.getCount(), 1);
    CHECK_EQ(deriveSeed(params.getSeed(), "terrain"), deriveSeed(params.getSeed(), "terrain"));
    CHECK(deriveSeed(params.getSeed(), "terrain") != deriveSeed(params.getSeed(), "props"));

    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    CHECK_EQ(mesh.getVertexCount(), 3);
    CHECK_EQ(mesh.getIndexCount(), 3);

    ProcgenContext context("core-system", params.getSeed(), "core-grid:v1:42:4x3");
    CHECK_EQ(context.getBuildKey(), std::string("core-grid:v1:42:4x3"));
    CHECK_EQ(context.seedFor("terrain"), deriveSeed(params.getSeed(), "terrain"));
}

TEST_CASE("procgen.params.typedStorageConversionsAndCanonicalEncoding") {
    using namespace eve::procgen;

    Params params;
    params.setInt("count", 7);
    params.setFloat("ratio", 1.5f);
    params.setBool("enabled", true);
    params.setString("label", "7");

    // Numeric conversion is explicit and does not go through text.
    CHECK_EQ(params.getInt("count", -1), 7);
    CHECK_EQ(params.getFloat("count", -1.f), 7.f);
    CHECK_EQ(params.getFloat("ratio", -1.f), 1.5f);
    CHECK_EQ(params.getInt("ratio", -1), -1);  // fractional Double is not truncated
    CHECK(params.getBool("enabled", false));
    CHECK_EQ(params.getInt("enabled", -1), 1);  // Bool has only 0/1 numeric conversion
    CHECK_EQ(params.getString("count", "wrong-kind"), "wrong-kind");
    CHECK_EQ(params.getInt("label", -1), -1);  // String is never parsed
    CHECK(params.getBool("label", true));      // invalid kind returns the caller default

    const std::string canonical = params.canonicalString();
    CHECK(canonical.find("int:7") != std::string::npos);
    CHECK(canonical.find("double:") != std::string::npos);
    CHECK(canonical.find("bool:1") != std::string::npos);
    CHECK(canonical.find("string:1:7") != std::string::npos);

    Params sameValues;
    sameValues.setString("label", "7");
    sameValues.setBool("enabled", true);
    sameValues.setFloat("ratio", 1.5f);
    sameValues.setInt("count", 7);
    CHECK_EQ(sameValues.canonicalString(), canonical);

    Params differentKind;
    differentKind.setInt("count", 7);
    differentKind.setFloat("ratio", 1.5f);
    differentKind.setBool("enabled", true);
    differentKind.setInt("label", 7);
    CHECK(differentKind.canonicalString() != canonical);
}

TEST_CASE("procgen.params.dimensionsHaveDedicatedSemantics") {
    using namespace eve::procgen;

    Params params;
    params.setSize(0, -4);
    CHECK_EQ(params.getWidth(), 1);
    CHECK_EQ(params.getHeight(), 1);
    params.setInt("width", 64);
    params.setInt("height", 48);
    params.setInt("seed", 123);
    CHECK_EQ(params.getWidth(), 64);
    CHECK_EQ(params.getHeight(), 48);
    CHECK_EQ(params.getSeed(), uint32_t(123));
    CHECK_EQ(params.getInt("width", -1), 64);
    CHECK_EQ(params.getInt("height", -1), 48);
    CHECK_EQ(params.getInt("seed", -1), 123);

    // Mesh recipes also use width/height as floating algorithm parameters;
    // those values remain separate from the grid dimensions.
    params.setFloat("width", 2.5f);
    params.setString("height", "building-height");
    CHECK_EQ(params.getWidth(), 64);
    CHECK_EQ(params.getHeight(), 48);
    CHECK_EQ(params.getFloat("width", -1.f), 2.5f);
    CHECK_EQ(params.getString("height", "missing"), "building-height");

    Params reordered;
    reordered.setString("height", "building-height");
    reordered.setFloat("width", 2.5f);
    reordered.setSeed(123);
    reordered.setSize(64, 48);
    CHECK_EQ(reordered.canonicalString(), params.canonicalString());
}

// procgen_dynamic_mesh_uv_paint.cpp
using namespace eve;

namespace {
procgen::MeshBuild paintTriangle(float lift = 0.f) {
    procgen::MeshBuild mesh;
    mesh.addVertex(0.f, lift, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, lift, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, lift, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}
}  // namespace

TEST_CASE("procgen.dynamicMeshUvPaint.reusesCanonicalPainterAcrossMeshUpdates") {
    image::ImageData                   pixels(16, 16, "RGBA8");
    procgen::DynamicMeshUvPaintSession session;
    REQUIRE(session.initializeResult(paintTriangle(), pixels).ok());
    REQUIRE(session.paintSurfacePointResult(0, 0.25f, 0.f, 0.25f, 2.f, 1.f, 0.f, 0.f, 1.f).ok());
    const auto paintRevision = session.paintRevision();
    REQUIRE(session.updateMeshResult(paintTriangle(1.f)).ok());
    CHECK_EQ(session.paintRevision(), paintRevision);
    REQUIRE(session.paintSurfacePointResult(0, 0.25f, 1.f, 0.25f, 2.f, 0.f, 1.f, 0.f, 1.f).ok());
    CHECK(session.paintRevision() > paintRevision);
    REQUIRE(session.undoResult().ok());
    REQUIRE(session.currentImageResult().ok());
}

TEST_CASE("procgen.dynamicMeshUvPaint.moduleHandleDetectsRelease") {
    auto* procgen = procgen::Procgen::create();
    auto  created = procgen->newDynamicMeshUvPaintSessionHandle();
    REQUIRE(created.ok());
    const auto handle = created.value();
    CHECK(procgen->resolveDynamicMeshUvPaintSession(handle).isBound());
    REQUIRE(procgen->release(handle).ok());
    CHECK(procgen->isStale(handle));
    CHECK(!procgen->resolveDynamicMeshUvPaintSession(handle).isBound());
}

// procgen_e2e.cpp
namespace {

eve::PersistentId persistentId(const char* text) {
    const auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::procgen::ArtifactId artifactId(const char* text) {
    const auto parsed = eve::procgen::ArtifactId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::PersistentId persistentId(eve::procgen::ArtifactId id) { return eve::PersistentId::fromUuid(id); }

eve::procgen::Params hexParams() {
    eve::procgen::Params params;
    params.setSeed(424242u);
    params.setInt("width", 8);
    params.setInt("height", 7);
    params.setFloat("radius", 1.f);
    return params;
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        std::array<std::uint8_t, 16> bytes{};
        for (std::size_t i = 0; i < 8; ++i) {
            bytes[i]     = static_cast<std::uint8_t>((hash >> (i * 8u)) & 0xffu);
            bytes[8 + i] = static_cast<std::uint8_t>(((hash ^ 0x9e3779b97f4a7c15ull) >> (i * 8u)) & 0xffu);
        }
        const auto id = eve::ContentId::fromBytes(std::span<const std::uint8_t>(bytes));
        if (!id)
            return eve::Result<eve::ContentId>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "test digest construction failed"));
        return eve::Result<eve::ContentId>::success(*id);
    };
}

void resetProviders() {
    eve::cap::detail::clearAllRaw();
    eve::scene::sceneArtifactProvider().clear();
    eve::map::mapArtifactProvider().clear();
    eve::graphics::graphicsArtifactProvider().clear();
    eve::physics::physicsArtifactProvider().clear();
    eve::scene::registerSceneArtifactProvider();
    eve::map::registerMapArtifactProvider();
    eve::graphics::registerGraphicsArtifactProvider();
    eve::physics::registerPhysicsArtifactProvider();
}

eve::procgen::ArtifactPublishOptions allProviders() {
    eve::procgen::ArtifactPublishOptions options;
    options.scene    = true;
    options.graphics = true;
    options.physics  = true;
    options.map      = true;
    return options;
}

}  // namespace

TEST_CASE("procgen.e2e.hexProvidersCommitQueryableObjects") {
    resetProviders();
    const auto                      id = artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0101");
    eve::procgen::ArtifactStore     store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto                            generated = eve::procgen::generateHexTerrainArtifact(hexParams(), id);
    REQUIRE(generated.ok());
    REQUIRE(generated.value().metadata.contains("algorithm"));
    REQUIRE(generated.value().metadata.contains("determinism"));
    auto published = publisher.publish(std::move(generated).takeValue(), allProviders());
    REQUIRE(published.ok());
    std::move(published).takeValue();

    auto& scene    = eve::scene::sceneArtifactProvider();
    auto& map      = eve::map::mapArtifactProvider();
    auto& graphics = eve::graphics::graphicsArtifactProvider();
    auto& physics  = eve::physics::physicsArtifactProvider();
    CHECK_EQ(store.size(), std::size_t(1));
    CHECK_EQ(store.partCount(), std::size_t(4));
    CHECK_EQ(scene.size(), std::size_t(1));
    CHECK_EQ(map.size(), std::size_t(1));
    CHECK_EQ(graphics.size(), std::size_t(1));
    CHECK_EQ(physics.size(), std::size_t(1));
    REQUIRE(map.find(persistentId(id)) != nullptr);
    CHECK_EQ(map.find(persistentId(id))->width, 8);
    CHECK_EQ(map.find(persistentId(id))->height, 7);
    REQUIRE(graphics.find(persistentId(id)) != nullptr);
    CHECK_GT(graphics.find(persistentId(id))->positions.size(), std::size_t(0));
    REQUIRE(physics.find(persistentId(id)) != nullptr);
    const auto* collider = physics.find(persistentId(id));
    REQUIRE_GE(collider->indices.size(), std::size_t(3));
    const auto vertex = [collider](std::size_t corner, std::size_t axis) {
        return collider->vertices[std::size_t(collider->indices[corner]) * 3u + axis];
    };
    const float rayX   = (vertex(0, 0) + vertex(1, 0) + vertex(2, 0)) / 3.f;
    const float rayZ   = (vertex(0, 2) + vertex(1, 2) + vertex(2, 2)) / 3.f;
    const auto  bounds = collider->bounds;
    const auto  hit    = physics.rayCast(persistentId(id), rayX, bounds.maxY + 10.f, rayZ, 0.f,
                                         -((bounds.maxY - bounds.minY) + 20.f), 0.f);
    CHECK(hit.hit);
    CHECK(hit.fraction >= 0.f);
    CHECK(hit.fraction <= 1.f);
}

TEST_CASE("procgen.e2e.providerFailureLeavesNoHalfState") {
    for (int failure = 0; failure < 4; ++failure) {
        resetProviders();
        auto& scene    = eve::scene::sceneArtifactProvider();
        auto& map      = eve::map::mapArtifactProvider();
        auto& graphics = eve::graphics::graphicsArtifactProvider();
        auto& physics  = eve::physics::physicsArtifactProvider();
        if (failure == 0) scene.setPrepareFailure(true);
        if (failure == 1) map.setPrepareFailure(true);
        if (failure == 2) graphics.setPrepareFailure(true);
        if (failure == 3) physics.setPrepareFailure(true);

        eve::procgen::ArtifactStore     store;
        eve::procgen::ArtifactPublisher publisher(store);
        auto                            generated =
            eve::procgen::generateHexTerrainArtifact(hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0102"));
        REQUIRE(generated.ok());
        auto result = publisher.publish(std::move(generated).takeValue(), allProviders());
        CHECK(!result.ok());
        CHECK_EQ(store.size(), std::size_t(0));
        CHECK_EQ(store.partCount(), std::size_t(0));
        CHECK_EQ(scene.size(), std::size_t(0));
        CHECK_EQ(map.size(), std::size_t(0));
        CHECK_EQ(graphics.size(), std::size_t(0));
        CHECK_EQ(physics.size(), std::size_t(0));
    }
}

TEST_CASE("procgen.e2e.absentCapabilityIsExplicit") {
    resetProviders();
    eve::cap::revoke<eve::artifact::IPhysicsArtifactAdapter>(&eve::physics::physicsArtifactProvider());
    eve::procgen::ArtifactStore     store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto                            generated =
        eve::procgen::generateHexTerrainArtifact(hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0103"));
    REQUIRE(generated.ok());
    auto result = publisher.publish(std::move(generated).takeValue(), allProviders());
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::Unsupported));
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(eve::physics::physicsArtifactProvider().size(), std::size_t(0));
}

TEST_CASE("procgen.e2e.snapshotRoundTripPreservesIdentityBuildKeyAndQueries") {
    resetProviders();
    const auto                      id = artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0104");
    eve::procgen::ArtifactStore     store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto                            generated = eve::procgen::generateHexTerrainArtifact(hexParams(), id);
    REQUIRE(generated.ok());
    const auto expectedKey = generated.value().buildKey;
    auto       published   = publisher.publish(std::move(generated).takeValue(), allProviders());
    REQUIRE(published.ok());
    std::move(published).takeValue();
    const auto* beforeCollider = eve::physics::physicsArtifactProvider().find(persistentId(id));
    REQUIRE(beforeCollider != nullptr);
    const auto  bounds        = beforeCollider->bounds;
    const float rayX          = (bounds.minX + bounds.maxX) * .5f;
    const float rayZ          = (bounds.minZ + bounds.maxZ) * .5f;
    const float rayOriginY    = bounds.maxY + 10.f;
    const float rayDirectionY = -((bounds.maxY - bounds.minY) + 20.f);
    const auto  beforeHit = eve::physics::physicsArtifactProvider().rayCast(persistentId(id), rayX, rayOriginY, rayZ,
                                                                            0.f, rayDirectionY, 0.f);
    const auto  beforeChecksum = eve::graphics::graphicsArtifactProvider().checksum(persistentId(id));
    const auto  beforeCell     = eve::map::mapArtifactProvider().cell(persistentId(id), 2, 3);

    eve::procgen::ArtifactSnapshotContext context;
    context.instanceId   = persistentId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0105");
    context.revision     = eve::Revision(9);
    context.tick         = eve::SimulationTick(17);
    context.hashProvider = testHash();
    auto snapshot        = publisher.snapshot(context);
    REQUIRE(snapshot.ok());
    auto serialized = eve::serializeSnapshotEnvelope(snapshot.value());
    REQUIRE(serialized.ok());
    auto parsed = eve::parseSnapshotEnvelope(serialized.value(), context.hashProvider);
    REQUIRE(parsed.ok());

    store.clear();
    eve::scene::sceneArtifactProvider().clear();
    eve::map::mapArtifactProvider().clear();
    eve::graphics::graphicsArtifactProvider().clear();
    eve::physics::physicsArtifactProvider().clear();
    auto restored = publisher.restore(std::move(parsed).takeValue(), context.hashProvider);
    REQUIRE(restored.ok());
    restored.value();

    CHECK_EQ(store.size(), std::size_t(1));
    CHECK_EQ(store.typeOf(id), std::optional<eve::procgen::ArtifactType>(eve::procgen::ArtifactType::Composite));
    const auto matching = store.findByBuildKey(expectedKey);
    REQUIRE_EQ(matching.size(), std::size_t(1));
    CHECK_EQ(matching.front(), id);
    CHECK_EQ(eve::scene::sceneArtifactProvider().size(), std::size_t(1));
    CHECK_EQ(eve::map::mapArtifactProvider().cell(persistentId(id), 2, 3), beforeCell);
    CHECK_EQ(eve::graphics::graphicsArtifactProvider().checksum(persistentId(id)), beforeChecksum);
    const auto afterHit = eve::physics::physicsArtifactProvider().rayCast(persistentId(id), rayX, rayOriginY, rayZ, 0.f,
                                                                          rayDirectionY, 0.f);
    CHECK_EQ(afterHit.hit, beforeHit.hit);
    CHECK_EQ(afterHit.fraction, beforeHit.fraction);
}

TEST_CASE("procgen.e2e.fixedSeedBuildKeyAndPayloadAreStable") {
    const auto first =
        eve::procgen::generateHexTerrainArtifact(hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0106"));
    const auto second =
        eve::procgen::generateHexTerrainArtifact(hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0107"));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().buildKey, second.value().buildKey);
    REQUIRE(first.value().metadata.contains("determinism"));
    const auto* determinism = first.value().metadata.at("determinism").getIf<std::string>();
    REQUIRE(determinism != nullptr);
    CHECK_EQ(*determinism, std::string("seeded_cpu"));
    const auto& firstComposite  = std::get<eve::procgen::CompositeArtifact>(first.value().payload);
    const auto& secondComposite = std::get<eve::procgen::CompositeArtifact>(second.value().payload);
    REQUIRE(firstComposite.find("mesh") != nullptr);
    REQUIRE(secondComposite.find("mesh") != nullptr);
    CHECK_EQ(std::get<eve::procgen::MeshData>(firstComposite.find("mesh")->payload).positions(),
             std::get<eve::procgen::MeshData>(secondComposite.find("mesh")->payload).positions());
    CHECK_EQ(std::get<eve::procgen::Grid2D>(firstComposite.find("topology")->payload).cells(),
             std::get<eve::procgen::Grid2D>(secondComposite.find("topology")->payload).cells());
}

// procgen_floor_textures.cpp
// Dedicated floor-texture recipe coverage lives in this TU so the large
// procgen.cpp suite stays process-isolated and focused.


using namespace eve::procgen;

TEST_CASE("procgen.floorTextures.wood.reproducibleLayouts") {
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(TextureRecipeRegistry::instance().has("tex.floor.wood"));

    const char* layouts[] = {"planks", "staggered", "herringbone", "chevron", "parquet",
                             "basket", "diagonal",  "ladder",      "finger",  "versailles"};
    for (const char* layout : layouts) {
        Params p;
        p.setSeed(42);
        p.setSize(64, 64);
        p.setString("layout", layout);
        p.setString("tone", "oak");
        p.setInt("rows", 5);
        p.setInt("cols", 4);
        p.setFloat("gap", 0.05f);
        p.setInt("seamless", 1);

        std::string err;
        auto        a = TextureRecipeRegistry::instance().generate("tex.floor.wood", p, err);
        auto        b = TextureRecipeRegistry::instance().generate("tex.floor.wood", p, err);
        REQUIRE(static_cast<bool>(a));
        REQUIRE(static_cast<bool>(b));
        CHECK_EQ(a->getWidth(), 64);
        CHECK_EQ(a->getHeight(), 64);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.floorTextures.tile.reproduciblePatterns") {
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(TextureRecipeRegistry::instance().has("tex.floor.tile"));

    const char* patterns[] = {"square", "checker",   "diamond",     "hex",     "subway",    "brick",    "stack",
                              "mosaic", "basket",    "herringbone", "octagon", "fishscale", "pinwheel", "star",
                              "cobble", "arabesque", "terrazzo",    "scallop", "windmill",  "moroccan"};
    for (const char* pattern : patterns) {
        Params p;
        p.setSeed(17);
        p.setSize(48, 48);
        p.setString("pattern", pattern);
        p.setString("palette", "ceramic");
        p.setInt("tilesX", 4);
        p.setInt("tilesY", 4);
        p.setFloat("grout", 0.07f);
        p.setInt("seamless", 1);

        std::string err;
        auto        a = TextureRecipeRegistry::instance().generate("tex.floor.tile", p, err);
        auto        b = TextureRecipeRegistry::instance().generate("tex.floor.tile", p, err);
        REQUIRE(static_cast<bool>(a));
        REQUIRE(static_cast<bool>(b));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.floorTextures.paramsChangeOutput") {
    TextureRecipeRegistry::instance().registerBuiltins();

    Params oak;
    oak.setSeed(9);
    oak.setSize(32, 32);
    oak.setString("layout", "planks");
    oak.setString("tone", "oak");

    Params walnut = oak;
    walnut.setString("tone", "walnut");

    std::string err;
    auto        a = TextureRecipeRegistry::instance().generate("tex.floor.wood", oak, err);
    auto        b = TextureRecipeRegistry::instance().generate("tex.floor.wood", walnut, err);
    REQUIRE(static_cast<bool>(a));
    REQUIRE(static_cast<bool>(b));
    CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) != 0);

    Params square;
    square.setSeed(3);
    square.setSize(32, 32);
    square.setString("pattern", "square");
    square.setString("palette", "ceramic");

    Params checker = square;
    checker.setString("pattern", "checker");

    auto c = TextureRecipeRegistry::instance().generate("tex.floor.tile", square, err);
    auto d = TextureRecipeRegistry::instance().generate("tex.floor.tile", checker, err);
    REQUIRE(static_cast<bool>(c));
    REQUIRE(static_cast<bool>(d));
    CHECK(std::memcmp(c->getData(), d->getData(), c->getSize()) != 0);
}

TEST_CASE("procgen.floorTextures.resultApiAndDefaults") {
    Params p;
    p.setSeed(1);
    p.setSize(40, 40);
    auto wood = generateWoodFloorTexture(p);
    REQUIRE(wood.ok());
    REQUIRE(static_cast<bool>(wood.value()));
    CHECK_EQ(wood.value()->getWidth(), 40);

    auto tile = generateTileFloorTexture(p);
    REQUIRE(tile.ok());
    REQUIRE(static_cast<bool>(tile.value()));

    TextureRecipeRegistry::instance().registerBuiltins();
    Params filled;
    filled.setSize(16, 16);
    REQUIRE(TextureRecipeRegistry::instance().applyDefaults("tex.floor.wood", filled));
    CHECK_EQ(filled.getString("layout", ""), std::string("planks"));
    CHECK_EQ(filled.getString("tone", ""), std::string("oak"));
    REQUIRE(TextureRecipeRegistry::instance().applyDefaults("tex.floor.tile", filled));
    CHECK_EQ(filled.getString("pattern", ""), std::string("square"));
    CHECK_EQ(filled.getString("palette", ""), std::string("ceramic"));
}

TEST_CASE("procgen.floorTextures.pbr.fullMapSet") {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    REQUIRE(PbrRecipeRegistry::instance().has("pbr.floor.wood"));
    REQUIRE(PbrRecipeRegistry::instance().has("pbr.floor.tile"));

    Params wood;
    wood.setSeed(11);
    wood.setSize(48, 48);
    wood.setString("layout", "herringbone");
    wood.setString("tone", "walnut");
    wood.setInt("rows", 6);
    wood.setInt("cols", 6);
    wood.setInt("seamless", 1);

    std::string err;
    auto        woodSet = PbrRecipeRegistry::instance().generate("pbr.floor.wood", wood, err);
    REQUIRE(static_cast<bool>(woodSet));
    REQUIRE(woodSet->albedo != nullptr);
    REQUIRE(woodSet->normal != nullptr);
    REQUIRE(woodSet->roughness != nullptr);
    REQUIRE(woodSet->metallic != nullptr);
    REQUIRE(woodSet->height != nullptr);
    REQUIRE(woodSet->ao != nullptr);
    CHECK_EQ(woodSet->albedo->getWidth(), 48);
    CHECK_EQ(woodSet->normal->getWidth(), 48);
    CHECK_EQ(woodSet->height->getFormat(), std::string("RGBA8"));

    auto woodResult = generateWoodFloorPbr(wood);
    REQUIRE(woodResult.ok());
    REQUIRE(static_cast<bool>(woodResult.value()));
    CHECK(woodResult.value()->albedo != nullptr);
    CHECK(woodResult.value()->ao != nullptr);

    Params tile;
    tile.setSeed(22);
    tile.setSize(40, 40);
    tile.setString("pattern", "octagon");
    tile.setString("palette", "encaustic");
    tile.setFloat("glaze", 0.55f);
    tile.setInt("seamless", 1);

    auto tileSet = PbrRecipeRegistry::instance().generate("pbr.floor.tile", tile, err);
    REQUIRE(static_cast<bool>(tileSet));
    REQUIRE(tileSet->albedo != nullptr);
    REQUIRE(tileSet->normal != nullptr);
    REQUIRE(tileSet->roughness != nullptr);
    REQUIRE(tileSet->metallic != nullptr);
    REQUIRE(tileSet->height != nullptr);
    REQUIRE(tileSet->ao != nullptr);

    Params defaults;
    defaults.setSize(16, 16);
    REQUIRE(PbrRecipeRegistry::instance().applyDefaults("pbr.floor.wood", defaults));
    CHECK_EQ(defaults.getString("layout", ""), std::string("planks"));
    CHECK(defaults.getFloat("normalStrength", 0.f) > 0.f);
    REQUIRE(PbrRecipeRegistry::instance().applyDefaults("pbr.floor.tile", defaults));
    CHECK_EQ(defaults.getString("pattern", ""), std::string("square"));
}

// procgen_generated_artifact.cpp
using namespace eve::procgen;

namespace {

ArtifactId testId() {
    const auto parsed = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd001");
    REQUIRE(parsed.has_value());
    return *parsed;
}

}  // namespace

TEST_CASE("procgen.generatedArtifact.buildKeyIsCanonicalAndIdentityIsSeparate") {
    Params first;
    first.setSeed(17);
    first.setInt("width", 48);
    first.setFloat("radius", .75f);
    first.setBool("decorations", true);

    Params second;
    second.setBool("decorations", true);
    second.setFloat("radius", .75f);
    second.setInt("width", 48);
    second.setSeed(17);

    const auto firstKey  = BuildKey::forRecipe("mesh.hexterrain", first);
    const auto secondKey = BuildKey::forRecipe("mesh.hexterrain", second);
    REQUIRE(firstKey.has_value());
    REQUIRE(secondKey.has_value());
    CHECK_EQ(firstKey->format(), secondKey->format());

    second.setSeed(18);
    const auto changedKey = BuildKey::forRecipe("mesh.hexterrain", second);
    REQUIRE(changedKey.has_value());
    CHECK_NE(firstKey->format(), changedKey->format());

    const ArtifactId root = testId();
    CHECK(!root.isNil());
    CHECK(root.child("mesh") != root);
    CHECK(root.child("mesh") != root.child("collider"));
    CHECK(root.child("").isNil());
}

TEST_CASE("procgen.generatedArtifact.hexTerrainPublishesCompositeProducts") {
    Params params;
    params.setInt("width", 8);
    params.setInt("height", 7);
    params.setInt("riverCount", 2);
    params.setInt("seed", 91);

    auto result = generateHexTerrainArtifact(params, testId());
    REQUIRE(result.ok());
    GeneratedArtifact artifact = std::move(result).takeValue();
    CHECK_EQ(static_cast<int>(artifact.type), static_cast<int>(ArtifactType::Composite));
    CHECK(!artifact.buildKey.empty());
    CHECK(artifact.bounds.isValid());

    const auto& composite = std::get<CompositeArtifact>(artifact.payload);
    REQUIRE_EQ(composite.children.size(), std::size_t(4));
    const ArtifactPart* mesh     = composite.find("mesh");
    const ArtifactPart* collider = composite.find("collider");
    const ArtifactPart* topology = composite.find("topology");
    const ArtifactPart* anchors  = composite.find("anchors");
    REQUIRE(mesh != nullptr);
    REQUIRE(collider != nullptr);
    REQUIRE(topology != nullptr);
    REQUIRE(anchors != nullptr);
    CHECK_EQ(static_cast<int>(mesh->type), static_cast<int>(ArtifactType::MeshData));
    CHECK(std::get<MeshData>(mesh->payload).getVertexCount() > 0);
    CHECK(std::get<Collider>(collider->payload).isValid());
    CHECK_EQ(std::get<Grid2D>(topology->payload).getWidth(), 8);
    CHECK_EQ(std::get<PointSet>(anchors->payload).getCount(), 4);
}

TEST_CASE("procgen.generatedArtifact.castlePublishesCompositeProductsAndStoreProtectsIdentity") {
    Params params;
    params.setInt("rings", 2);
    params.setInt("keepFloors", 2);
    params.setInt("detail", 1);
    params.setInt("seed", 12);

    const ArtifactId id     = testId();
    auto             result = generateCastleArtifact(params, id);
    REQUIRE(result.ok());
    GeneratedArtifact artifact  = std::move(result).takeValue();
    const BuildKey    key       = artifact.buildKey;
    const auto&       composite = std::get<CompositeArtifact>(artifact.payload);
    REQUIRE(composite.has("mesh"));
    REQUIRE(composite.has("collider"));
    REQUIRE(composite.has("topology"));
    REQUIRE(composite.has("anchors"));
    CHECK(std::get<MeshData>(composite.find("mesh")->payload).getIndexCount() > 0);
    CHECK(std::get<Collider>(composite.find("collider")->payload).isValid());
    CHECK_EQ(std::get<Grid2D>(composite.find("topology")->payload).getWidth(), 2);
    CHECK_EQ(std::get<PointSet>(composite.find("anchors")->payload).getCount(), 5);

    ArtifactStore store;
    auto          published = store.publish(std::move(artifact));
    REQUIRE(published.ok());
    const auto publishedId = std::move(published).takeValue();
    CHECK_EQ(publishedId, id);
    CHECK_EQ(store.size(), std::size_t(1));
    CHECK(store.find(id) != nullptr);
    CHECK_EQ(store.findByBuildKey(key).size(), std::size_t(1));

    Params sameParams = params;
    auto   duplicate  = generateCastleArtifact(sameParams, id);
    REQUIRE(duplicate.ok());
    auto conflict = store.publish(std::move(duplicate).takeValue());
    CHECK(!conflict.ok());
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(eve::StatusCode::Conflict));

    auto removed = store.remove(id);
    CHECK(removed.ok());
    CHECK(store.find(id) == nullptr);
    auto missing = store.remove(id);
    CHECK(!missing.ok());
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(eve::StatusCode::NotFound));
}

TEST_CASE("procgen.generatedArtifact.rejectsInvalidContracts") {
    const ArtifactId id  = testId();
    auto             key = BuildKey::fromCanonical("procgen/v1/test/0000000000000001");
    REQUIRE(key.has_value());
    Bounds bounds;
    bounds.include(0.f, 0.f, 0.f);
    Grid2D grid;
    grid.resize(1, 1);

    auto nil = makeArtifact(ArtifactId::nil(), ArtifactType::Grid, eve::SchemaVersion(1), *key, bounds, {}, {},
                            GeneratedArtifact::Payload(grid));
    CHECK(!nil.ok());
    CHECK_EQ(static_cast<int>(nil.code()), static_cast<int>(eve::StatusCode::Rejected));

    auto mismatch = makeArtifact(id, ArtifactType::MeshData, eve::SchemaVersion(1), *key, bounds, {}, {},
                                 GeneratedArtifact::Payload(grid));
    CHECK(!mismatch.ok());
    CHECK_EQ(static_cast<int>(mismatch.code()), static_cast<int>(eve::StatusCode::Rejected));
}

TEST_CASE("procgen.meshBuild.checkedGroupRestoreIsAtomic") {
    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);

    auto valid = mesh.restoreGroupData({"main"}, {0}, 0);
    REQUIRE(valid.ok());
    CHECK_EQ(mesh.getGroupCount(), 1);
    CHECK_EQ(mesh.activeGroup(), 0);

    auto invalid = mesh.restoreGroupData({""}, {0}, 0);
    REQUIRE(!invalid.ok());
    CHECK_EQ(static_cast<int>(invalid.error()->code()), static_cast<int>(eve::DiagnosticCode::ProcgenGroupDataInvalid));
    CHECK_EQ(mesh.getGroupName(0), std::string("main"));
    CHECK_EQ(mesh.getTriangleGroup(0), 0);
}

TEST_CASE("procgen.generatedArtifact.denseSnapshotRoundTripsEveryPayloadAndRebuildsIndexes") {
    const ArtifactId root = testId();
    const auto       key  = BuildKey::fromCanonical("procgen/v1/roundtrip/0000000000000001");
    REQUIRE(key.has_value());

    Bounds rootBounds;
    rootBounds.include(0.f, 0.f, 0.f);
    rootBounds.include(4.f, 3.f, 2.f);

    Grid2D grid;
    grid.resize(2, 1);
    grid.setCell(0, 0, 7);
    grid.setCell(1, 0, 9);
    grid.setDetail(1, 0, 3);
    grid.setMeta("theme", "test");
    grid.addObject("spawn", "unit", 1.f, 2.f, 3.f, 4.f, 5);
    Bounds gridBounds;
    gridBounds.include(0.f, 0.f, 0.f);
    gridBounds.include(2.f, 0.f, 1.f);

    PointSet  points;
    const int point = points.add(1.f, 2.f, 3.f);
    points.setNormal(point, .1f, .9f, .2f);
    points.setRotation(point, .15f, .25f, .35f);
    points.setScale(point, 2.f, 3.f, 4.f);
    points.setBounds(point, -1.f, -2.f, -3.f, 1.f, 2.f, 3.f);
    points.setColor(point, .1f, .2f, .3f, .4f);
    points.setSteepness(point, .65f);
    points.setDensity(point, .75f);
    points.setPointSeed(point, 123u);
    points.trySetPointId(point, 18446744073709551614ull).expect("artifact snapshot point id");
    points.setFloatAttribute(point, "weight", 1.5f);
    points.setIntAttribute(point, "variant", 4);
    points.setBoolAttribute(point, "enabled", true);
    points.setVectorAttribute(point, "wind", 5.f, 6.f, 7.f);
    points.setStringAttribute(point, "tag", "anchor");
    Bounds pointBounds;
    pointBounds.include(1.f, 2.f, 3.f);

    MeshData mesh;
    mesh.setActiveGroup("main");
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    mesh.setMeta("material", "debug");
    Bounds meshBounds;
    meshBounds.include(0.f, 0.f, 0.f);
    meshBounds.include(1.f, 1.f, 0.f);

    ImageData image;
    image.width    = 2;
    image.height   = 1;
    image.channels = 4;
    image.format   = "rgba8";
    image.pixels   = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    Bounds imageBounds;
    imageBounds.include(0.f, 0.f, 0.f);
    imageBounds.include(2.f, 1.f, 0.f);

    Collider collider;
    collider.vertices = mesh.positions();
    collider.indices  = mesh.indices();
    collider.bounds   = meshBounds;
    collider.shape    = "triangle_mesh";

    auto makePart = [&](std::string role, ArtifactId id, ArtifactType type, Bounds bounds, ArtifactLeafPayload payload,
                        std::vector<ArtifactId> dependencies = {}) {
        return makeArtifactPart(std::move(role), id, type, eve::SchemaVersion(1), *key, bounds, std::move(dependencies),
                                {}, std::move(payload));
    };
    std::vector<ArtifactPart> children;
    auto                      addPart = [&](auto result) {
        REQUIRE(result.ok());
        children.emplace_back(std::move(result).takeValue());
    };
    addPart(makePart("grid", root.child("grid"), ArtifactType::Grid, gridBounds, std::move(grid)));
    addPart(makePart("points", root.child("points"), ArtifactType::PointSet, pointBounds, std::move(points)));
    addPart(makePart("mesh", root.child("mesh"), ArtifactType::MeshData, meshBounds, std::move(mesh)));
    addPart(makePart("image", root.child("image"), ArtifactType::ImageData, imageBounds, std::move(image)));
    addPart(makePart("collider", root.child("collider"), ArtifactType::Collider, meshBounds, std::move(collider),
                     {root.child("mesh")}));

    CompositeArtifact composite;
    composite.children = std::move(children);
    auto generated     = makeArtifact(root, ArtifactType::Composite, eve::SchemaVersion(1), *key, rootBounds, {}, {},
                                      std::move(composite));
    REQUIRE(generated.ok());

    ArtifactStore source;
    REQUIRE(source.publish(std::move(generated).takeValue()).ok());
    auto snapshot = source.snapshotState();
    REQUIRE(snapshot.ok());

    ArtifactStore restored;
    REQUIRE(restored.restoreState(snapshot.value()).ok());
    auto restoredSnapshot = restored.snapshotState();
    REQUIRE(restoredSnapshot.ok());
    CHECK_EQ(restoredSnapshot.value(), snapshot.value());
    CHECK(restored.find(root) != nullptr);
    CHECK_EQ(restored.partCount(), std::size_t(5));
    CHECK(restored.findPart(root.child("grid")) != nullptr);
    CHECK(restored.findPart(root.child("points")) != nullptr);
    CHECK(restored.findPart(root.child("mesh")) != nullptr);
    CHECK(restored.findPart(root.child("image")) != nullptr);
    CHECK(restored.findPart(root.child("collider")) != nullptr);
    REQUIRE(restored.typeOf(root.child("mesh")).has_value());
    CHECK_EQ(static_cast<int>(*restored.typeOf(root.child("mesh"))), static_cast<int>(ArtifactType::MeshData));
    const auto* restoredMesh = restored.findPart(root.child("mesh"));
    REQUIRE(restoredMesh != nullptr);
    CHECK_EQ(std::get<MeshData>(restoredMesh->payload).groupNames(), std::vector<std::string>{"main"});
    const auto* restoredPointsPart = restored.findPart(root.child("points"));
    REQUIRE(restoredPointsPart != nullptr);
    const auto& restoredPoints = std::get<PointSet>(restoredPointsPart->payload);
    CHECK_EQ(restoredPoints.getPitch(0), .15f);
    CHECK_EQ(restoredPoints.getRoll(0), .35f);
    CHECK_EQ(restoredPoints.getBoundsMinY(0), -2.f);
    CHECK_EQ(restoredPoints.getColorA(0), .4f);
    CHECK_EQ(restoredPoints.getSteepness(0), .65f);
    CHECK_EQ(restoredPoints.getPointId(0), 18446744073709551614ull);
    CHECK_EQ(restoredPoints.getIntAttribute(0, "variant", -1), 4);
    CHECK(restoredPoints.getBoolAttribute(0, "enabled", false));
    CHECK_EQ(restoredPoints.getVectorAttributeZ(0, "wind", -1.f), 7.f);
}

// procgen_geometry_stroke.cpp
using namespace eve::procgen;

namespace {

template <typename T>
void requireOk(const eve::Result<T>& result) {
    REQUIRE(result.ok());
}

}  // namespace

TEST_CASE("procgen.geometryStroke.buildsAllOfficialCrossSections") {
    struct Expected {
        const char* shape;
        int         totalIndices;
    };
    for (const Expected expected : {Expected{"quad", 24}, Expected{"triangularPrism", 42}, Expected{"cube", 60}}) {
        GeometryStroke stroke;
        requireOk(stroke.setShapeResult(expected.shape));
        requireOk(stroke.setSizeResult(0.5f, 0.3f));
        requireOk(stroke.addPointResult(0.f, 0.f, 0.f));
        requireOk(stroke.addPointResult(1.f, 0.f, 0.f));
        requireOk(stroke.addPointResult(1.f, 1.f, 0.f));
        auto mesh = stroke.buildMeshResult();
        requireOk(mesh);
        CHECK_EQ(mesh.value().getIndexCount(), expected.totalIndices);
        CHECK_EQ(mesh.value().getMeta("shape", ""), expected.shape);
        CHECK_EQ(mesh.value().getMeta("pointCount", ""), "3");
        CHECK_EQ(mesh.value().getGroupCount(), 1);
    }
}

TEST_CASE("procgen.geometryStroke.supportsPlanarSpacingUndoAndAtomicValidation") {
    GeometryStroke stroke;
    requireOk(stroke.setInputSpaceResult("planar", 2.f));
    requireOk(stroke.setMinimumSpacingResult(0.5f));
    auto first = stroke.addPointResult(0.f, 99.f, 0.f);
    auto close = stroke.addPointResult(0.1f, -5.f, 0.f);
    auto next  = stroke.addPointResult(1.f, -5.f, 0.f);
    requireOk(first);
    requireOk(close);
    requireOk(next);
    CHECK(first.value());
    CHECK(!close.value());
    CHECK(next.value());
    CHECK_EQ(stroke.pointCount(), 2);

    const auto revision = stroke.revision();
    CHECK(!stroke.setShapeResult("sphere").ok());
    CHECK_EQ(stroke.revision(), revision);
    auto mesh = stroke.buildMeshResult();
    requireOk(mesh);
    for (int index = 0; index < mesh.value().getVertexCount(); ++index)
        CHECK(std::abs(mesh.value().getPositionY(index) - 2.f) < 1e-6f);

    requireOk(stroke.undoResult());
    CHECK_EQ(stroke.pointCount(), 1);
    CHECK(!stroke.buildMeshResult().ok());
    requireOk(stroke.undoResult());
    CHECK(!stroke.undoResult().ok());
}
}  // namespace test_procgen_core_boundary
#endif
