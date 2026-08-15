#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "particles/Particles.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"

#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace eve::particles;
using namespace eve::animation;

namespace {

#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

bool fileExists(const std::string &path) { return std::filesystem::is_regular_file(path); }

std::string cesiumManDir() {
    return pathBesideThisSource("assets/skinned/cesium_man/glTF");
}

std::string cesiumManGltfPath() { return cesiumManDir() + "/CesiumMan.gltf"; }

bool ensureSkinnedAssets() {
    const std::string gltf = cesiumManGltfPath();
    if (fileExists(gltf)) return true;
    std::printf(
        "particles.attach_skin: missing %s — run scripts/download_skinned_character.sh\n",
        gltf.c_str());
    return false;
}

eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = cesiumManDir();
    REQUIRE(fileExists(dir + "/CesiumMan.gltf"));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
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

}  // namespace

TEST_CASE("particles.attach.boneFollowsPose") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    const int hand = sk->addBone("hand", root);
    sk->setBindPosition(hand, 0.f, 1.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 10.f, 20.f, 5.f);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    e->setAttachScale(1.f);
    e->setAttachPlane("xy");
    e->attachToBone(pose.get(), hand);

    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachBone(), hand);
    // hand world = root(10,20,5) + local(0,1,0) → (10, 21, 5); xy → (10, 21)
    CHECK(std::fabs(e->getX() - 10.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 21.f) < 1e-3f);

    // Move bone and sync via ParticleSimSystem.
    pose->setLocalPosition(root, 30.f, 40.f, 0.f);
    pose->computeWorld(sk.get());
    ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - 30.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 41.f) < 1e-3f);

    e->setAttachPlane("xz");
    e->syncAttach();
    // world (30, 41, 0) on xz → (30, 0)
    CHECK(std::fabs(e->getX() - 30.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 0.f) < 1e-3f);

    e->setAttachScale(2.f);
    e->setAttachPlane("xy");
    e->syncAttach();
    CHECK(std::fabs(e->getX() - 60.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 82.f) < 1e-3f);

    e->detach();
    CHECK(!e->isAttached());
}

TEST_CASE("particles.attach.byNameAndOffset") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("Hips", -1);
    const int tip  = sk->addBone("Head", root);
    sk->setBindPosition(tip, 0.f, 2.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->attachToBoneByName(pose.get(), sk.get(), "Head");
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachBone(), tip);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-3f);

    e->setAttachOffset(1.f, 0.f, 0.f);
    e->syncAttach();
    CHECK(std::fabs(e->getX() - 1.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-3f);

    e->attachToBoneByName(pose.get(), sk.get(), "MissingBone");
    CHECK(!e->isAttached());
}

TEST_CASE("particles.attach.followRotation") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    // 90° around Z: +X maps to +Y in XY plane → direction ≈ π/2
    const float half = 0.70710678f;
    pose->setLocalRotation(root, 0.f, 0.f, half, half);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setDirection(0.f);
    e->setFollowBoneRotation(true);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getDirection() - (3.14159265f * 0.5f)) < 0.05f);
    e->detach();
}

TEST_CASE("particles.attach.emitsAtBone") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 50.f, 60.f, 0.f);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(100.f);
    e->attachToBone(pose.get(), root);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 4);
    auto sim = e->sim();
    for (int i = 0; i < sim->alive; ++i) {
        CHECK(std::fabs(sim->particles[size_t(i)].x - 50.f) < 1e-2f);
        CHECK(std::fabs(sim->particles[size_t(i)].y - 60.f) < 1e-2f);
    }
    e->detach();
}

TEST_CASE("particles.skin.surfaceEmitCesiumMan") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_skin_surface"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimClip> clip(
        AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    REQUIRE(skin.get() != nullptr);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    clip->sample(0.f, pose.get(), skeleton.get());
    pose->computeWorld(skeleton.get());
    REQUIRE(skin->updateSkinnedPositions(pose.get()));
    CHECK(skin->hasSkinnedPositions());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(256);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionArea("none", 0.f, 0.f);
    e->setSkinScale(100.f);  // meters → pixels-ish
    e->setSkinPlane("xy");
    e->setSkinSource(skin.get(), pose.get());
    CHECK(e->hasSkinSource());

    e->emitFromSkin(64);
    CHECK_EQ(e->getCount(), 64);

    // Spawn positions should not all collapse to a single point.
    auto sim = e->sim();
    float minX = sim->particles[0].x, maxX = minX;
    float minY = sim->particles[0].y, maxY = minY;
    for (int i = 0; i < sim->alive; ++i) {
        const auto &p = sim->particles[size_t(i)];
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    CHECK_GT(maxX - minX + maxY - minY, 1.f);

    // Continuous emission via rate also samples the skin.
    e->reset();
    e->setEmissionRate(200.f);
    e->start();
    ParticleSimSystem::update(0.1f);
    CHECK_GE(e->getCount(), 15);

    // Bone filter should narrow candidate set (or keep emitting if bone unused).
    const std::string boneName = skin->getSkinBoneName(0);
    e->setSkinBoneFilterByName(skeleton.get(), boneName, 0.1f);
    e->reset();
    e->emitFromSkin(32);
    CHECK_GE(e->getCount(), 1);

    e->clearSkinSource();
    CHECK(!e->hasSkinSource());
}

TEST_CASE("particles.skin.skinnedCacheReadable") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_skin_cache"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose> pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    CHECK(!skin->hasSkinnedPositions());
    REQUIRE(skin->updateSkinnedPositions(pose.get()));
    CHECK(skin->hasSkinnedPositions());
    CHECK(std::isfinite(skin->getSkinnedPositionX(0)));
    CHECK(std::isfinite(skin->getSkinnedPositionY(0)));
    CHECK(std::isfinite(skin->getSkinnedPositionZ(0)));
}

TEST_CASE("particles.attach.animPoseDynamicEmitAcrossFrames") {
    // Emit while the bone is moving — particles should form a trail of spawn positions.
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    const int tip  = sk->addBone("tip", root);
    sk->setBindPosition(tip, 1.f, 0.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(0.f);
    e->setAttachScale(10.f);
    e->setAttachPlane("xy");
    e->attachToBone(pose.get(), tip);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));

    for (int frame = 0; frame < 5; ++frame) {
        pose->setLocalPosition(root, float(frame) * 2.f, 0.f, 0.f);
        pose->computeWorld(sk.get());
        e->emit(1);
        ParticleSimSystem::update(0.016f);
    }
    CHECK_EQ(e->getCount(), 5);
    auto sim = e->sim();
    // tip world x = root.x + 1 → scaled ×10; frames 0..4 → x = 10,30,50,70,90
    CHECK(std::fabs(sim->particles[0].x - 10.f) < 1e-2f);
    CHECK(std::fabs(sim->particles[4].x - 90.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.yzPlaneAndDetachClearsKind") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 1.f, 2.f, 3.f);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setAttachPlane("yz");
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getX() - 2.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 3.f) < 1e-3f);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));
    e->detach();
    CHECK(!e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("none"));
}

TEST_CASE("particles.attach.clipSampleMovesEmitter") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_attach_clip"));
    REQUIRE(model.get() != nullptr);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimClip> clip(
        AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    REQUIRE(clip->getDuration() > 0.1f);

    // Prefer a named limb bone; fall back to last bone.
    int bone = skeleton->findBone("Left_arm");
    if (bone < 0) bone = skeleton->findBone("LeftArm");
    if (bone < 0) bone = skeleton->getBoneCount() - 1;
    REQUIRE(bone >= 0);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setAttachScale(50.f);
    e->setAttachPlane("xy");
    e->attachToBone(pose.get(), bone);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));

    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < 8; ++i) {
        const float t = clip->getDuration() * (float(i) / 7.f);
        clip->sample(t, pose.get(), skeleton.get());
        pose->computeWorld(skeleton.get());
        e->emit(1);
        ParticleSimSystem::update(0.016f);
        const auto &p = e->sim()->particles[size_t(e->getCount() - 1)];
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    CHECK_EQ(e->getCount(), 8);
    // Animated bone should move enough that spawn positions spread out.
    CHECK_GT(maxX - minX + maxY - minY, 1.f);
    e->detach();
}

TEST_CASE("particles.skin.emptyBoneFilterFallsBack") {
    if (!ensureSkinnedAssets()) return;

    std::unique_ptr<eve::model3d::ModelData> model(
        loadCesiumMan("ev_ut_particles_skin_empty_filter"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose> pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setPosition(12.f, 34.f);
    e->setSkinScale(1.f);
    e->setSkinSource(skin.get(), pose.get());
    // Impossible weight threshold → no candidates → spawn falls back to Config.x/y.
    e->setSkinBoneFilter(0, 2.f);
    e->emitFromSkin(5);
    // emitFromSkin breaks when sample fails, so count may be 0.
    CHECK_LE(e->getCount(), 5);

    e->reset();
    e->setEmissionRate(100.f);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 4);
    // Fallback spawn at emitter position.
    CHECK(std::fabs(e->sim()->particles[0].x - 12.f) < 1e-2f);
    CHECK(std::fabs(e->sim()->particles[0].y - 34.f) < 1e-2f);
    e->clearSkinSource();
    e->detach();
}

TEST_CASE("particles.attach.followRotationOffKeepsDirection") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    const float half = 0.70710678f;
    pose->setLocalRotation(root, 0.f, 0.f, half, half);
    pose->computeWorld(sk.get());

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setDirection(0.25f);
    e->setFollowBoneRotation(false);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getDirection() - 0.25f) < 1e-4f);
    e->detach();
}
