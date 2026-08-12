#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "particles/Particles.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"

#include "animation/Animation.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "animation/SpineAnim.h"

#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"

#include <cmath>
#include <memory>
#include <string>

using namespace eve::particles;
using namespace eve::animation;
using namespace eve::ik;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

const char *kSpineJson = R"JSON({
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
    auto *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    REQUIRE(data.get() != nullptr);
    std::unique_ptr<SpineSkeleton> spine(animMod->newSpineSkeleton(data.get()));
    REQUIRE(spine.get() != nullptr);
    spine->setToSetupPose();
    spine->updateWorldTransform();

    const int hand = data->findBone("hand");
    REQUIRE(hand >= 0);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
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
}

TEST_CASE("particles.attach.spineDynamicEmitAcrossAnim") {
    auto *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton> spine(animMod->newSpineSkeleton(data.get()));
    std::unique_ptr<SpineAnim> anim(animMod->newSpineAnim(spine.get()));
    REQUIRE(anim.get() != nullptr);

    const int hand = data->findBone("hand");
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(0.f);
    e->attachToSpineBone(spine.get(), hand);

    float prevX = -1e9f;
    int moved = 0;
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
}

TEST_CASE("particles.attach.spineFollowRotation") {
    auto *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton> spine(animMod->newSpineSkeleton(data.get()));
    const int body = data->findBone("body");
    spine->setToSetupPose();
    spine->setBoneLocalRotation(body, 90.f);  // degrees
    spine->updateWorldTransform();

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setFollowBoneRotation(true);
    e->attachToSpineBone(spine.get(), body);
    CHECK(std::fabs(e->getDirection() - float(M_PI * 0.5)) < 0.05f);
}

TEST_CASE("particles.attach.ik2dBoneEmit") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 50.f);
    int tip = sk->createBone(b1, 50.f);
    sk->setPosition(0, 100.f, 200.f);
    sk->setRotation(0, 0.f);
    sk->setRotation(b1, 0.f);
    sk->setRotation(tip, float(M_PI / 2.0));
    sk->forwardKinematics();

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
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
}

TEST_CASE("particles.attach.ik2dFollowRotationAndSolver") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 40.f);
    int tip = sk->createBone(b1, 40.f);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(32);
    solver->addTarget(tip, 40.f, 40.f, 1.f);
    CHECK(solver->solve(sk.get()));

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setFollowBoneRotation(true);
    e->attachToSkeleton2D(sk.get(), tip);
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1.f);
    CHECK(std::fabs(e->getY() - sk->getY(tip)) < 1.f);
    // Direction should align with bone forward.
    const float fx = sk->getOrientationX(tip);
    const float fy = sk->getOrientationY(tip);
    const float expect = std::atan2(fy, fx);
    CHECK(std::fabs(e->getDirection() - expect) < 0.1f);
}

TEST_CASE("particles.attach.ik3dBoneEmitProjected") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int b1 = sk->createBone(0, 1.f);
    int tip = sk->createBone(b1, 1.f);
    sk->initStraightPose(10.f, 20.f, 5.f);
    CHECK(std::fabs(sk->getX(tip) - 12.f) < 1e-3f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
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
}

TEST_CASE("particles.attach.switchKindsClearsPrevious") {
    std::unique_ptr<Skeleton2D> sk2(new Skeleton2D());
    sk2->initStraightPose(1.f, 2.f);

    std::unique_ptr<Skeleton3D> sk3(new Skeleton3D());
    sk3->initStraightPose(3.f, 4.f, 5.f);

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->attachToSkeleton2D(sk2.get(), 0);
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    e->attachToSkeleton3D(sk3.get(), 0);
    CHECK_EQ(e->getAttachKind(), std::string("ik3d"));
    CHECK(e->attach()->ik2d == nullptr);
    CHECK(e->attach()->ik3d == sk3.get());
}
