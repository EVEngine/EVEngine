#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_avatar)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/Animation.h"
#include "animation/Tween.h"
#include "avatar/Avatar.h"
#include "avatar/AvatarInstance.h"
#include "avatar/Live2DNullBackend.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "inventory/Equipment.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// avatar.cpp
namespace test_avatar_unit {
// avatar.cpp
using namespace eve::avatar;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;

namespace {

class FakeLive2D : public ILive2DBackend {
public:
    std::string getName() const override { return "fake"; }
    bool        loadModel(const std::string& path) override {
        loadedPath = path;
        return !path.empty();
    }
    void  update(float) override { ++updates; }
    void  setParameter(const std::string& name, float value) override { params[name] = value; }
    float getParameter(const std::string& name) const override {
        auto it = params.find(name);
        return it == params.end() ? 0.f : it->second;
    }
    bool setExpression(const std::string& name) override {
        expression = name;
        return true;
    }
    bool setMotion(const std::string& name) override {
        motion = name;
        return true;
    }

    std::string                            loadedPath;
    std::string                            expression;
    std::string                            motion;
    int                                    updates = 0;
    std::unordered_map<std::string, float> params;
};

[[maybe_unused]] ILive2DBackend* makeFakeLive2D() { return new FakeLive2D(); }

}  // namespace

TEST_CASE("avatar.image.layersAndExpression") {
    Avatar* mod = Avatar::create();
    REQUIRE(mod != nullptr);

    AvatarInstance* av = mod->newImageAvatar();
    CHECK_EQ(av->getKind(), std::string("image"));
    CHECK(av->addLayer("body", nullptr, 0));
    CHECK(av->addLayer("face", nullptr, 1));
    CHECK(av->addLayer("blush", nullptr, 2));
    CHECK(!av->addLayer("body", nullptr, 0));  // duplicate
    CHECK_EQ(av->getLayerCount(), 3);
    CHECK(av->hasLayer("face"));

    Renderable2D* face = av->getLayerRenderable("face");
    REQUIRE(face != nullptr);
    face->setRotation(0.25f);
    face->setReceiveLight(false);
    face->setBlend("additive");
    CHECK_EQ(face->getBlend(), std::string("additive"));

    CHECK(av->setLayerVisible("blush", false));
    CHECK(av->setLayerSize("body", 200.f, 400.f));
    CHECK(av->setLayerOffset("face", 10.f, -20.f));

    CHECK(av->defineExpression("neutral", "blush=0;face=1"));
    CHECK(av->defineExpression("shy", "blush=1;face=1"));
    CHECK(av->applyExpression("shy"));
    CHECK_EQ(av->getExpression(), std::string("shy"));

    av->setPosition(100.f, 200.f);
    av->setScale(1.5f, 1.5f);
    av->setVisible(true);
    av->setLayer(50);
    av->sync();
    CHECK(std::fabs(face->getRotation() - 0.25f) < 1e-5f);
    CHECK(!face->getReceiveLight());
    CHECK_EQ(face->getBlend(), std::string("additive"));

    int visible = 0;
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [xf, sp] = *it;
            if (sp->visible) ++visible;
            (void)xf;
        }
    }
    CHECK(visible >= 2);

    av->release();
    delete av;
    CHECK_EQ(mod->getAvatarCount(), 0);
}

TEST_CASE("avatar.live2d.nullBackendDefault") {
    Avatar::registerLive2DBackend(nullptr);  // restore NullLive2DBackend
    Avatar*         mod = Avatar::create();
    AvatarInstance* av  = mod->newLive2DAvatar();
    CHECK_EQ(av->getKind(), std::string("live2d"));
    CHECK(!av->hasLive2DBackend());
    CHECK(!Avatar::hasLive2DBackend());
    CHECK_EQ(Avatar::getLive2DBackendName(), std::string("null"));
    CHECK(av->loadLive2DModel("models/hiyori"));
    CHECK_EQ(av->getLive2DBackendName(), std::string("null"));
    av->setParameter("ParamMouthOpenY", 0.4f);
    CHECK_EQ(av->getParameter("ParamMouthOpenY"), 0.4f);
    av->release();
    delete av;
}

TEST_CASE("avatar.authoringMetadataIsComposableAndStable") {
    Avatar*         mod = Avatar::create();
    AvatarInstance* av  = mod->newImageAvatar();
    REQUIRE(av->defineParameter("mouth", 0.25f, -1.f, 1.f));
    CHECK_EQ(av->getParameter("mouth"), 0.25f);
    CHECK_EQ(av->getParameterDefault("mouth"), 0.25f);
    CHECK_EQ(av->getParameterMinimum("mouth"), -1.f);
    CHECK_EQ(av->getParameterMaximum("mouth"), 1.f);
    CHECK(!av->defineParameter("invalid", 0.f, 2.f, 1.f));
    REQUIRE(av->defineExpression("smile", "mouth=1"));
    REQUIRE(av->defineExpression("neutral", "mouth=0"));
    CHECK_EQ(av->getExpressionCount(), 2);
    CHECK_EQ(av->getExpressionName(0), std::string("neutral"));
    CHECK_EQ(av->getExpressionName(1), std::string("smile"));
    REQUIRE(av->removeExpression("neutral"));
    CHECK_EQ(av->getExpressionCount(), 1);
    av->release();
    delete av;
}


TEST_CASE("avatar.live2d.backendPlugIn") {
    Avatar::registerLive2DBackend(&makeFakeLive2D);
    Avatar*         mod = Avatar::create();
    AvatarInstance* av  = mod->newLive2DAvatar();
    CHECK(av->hasLive2DBackend());
    CHECK(Avatar::hasLive2DBackend());
    CHECK(av->loadLive2DModel("models/hiyori"));
    CHECK_EQ(av->getLive2DBackendName(), std::string("fake"));
    av->setExpression("smile");
    av->setMotion("idle");
    av->setParameter("ParamMouthOpenY", 0.5f);
    CHECK_EQ(av->getParameter("ParamMouthOpenY"), 0.5f);
    av->update(0.016f);

    Avatar::registerLive2DBackend(nullptr);
    av->release();
    delete av;
}

TEST_CASE("avatar.vroid.pathAndTransform") {
    Avatar*         mod = Avatar::create();
    AvatarInstance* av  = mod->newVroidAvatar();
    CHECK_EQ(av->getKind(), std::string("vroid"));
    CHECK(!av->loadVroidModelPath("chars/hero.vrm"));
    CHECK(av->getVroidModelPath().empty());
    av->setPosition3D(1.f, 0.f, -2.f);
    av->setRotation3D(0.1f, 0.f, 0.f);
    av->setScale3D(1.f, 1.f, 1.f);
    av->setExpression("Joy");
    CHECK(av->hasParameter("Joy"));
    CHECK_EQ(av->getParameter("Joy"), 1.f);
    av->setVisible(true);
    av->sync();
    CHECK(av->getRenderable3D() != nullptr);

    av->release();
    delete av;
}

TEST_CASE("avatar.equipment.2dProjectionTracksAuthoritativeSlots") {
    Avatar*                      mod = Avatar::create();
    AvatarInstance*              av  = mod->newImageAvatar();
    eve::inventory::EquipmentSet equipment;
    equipment.defineSlot("body");

    CHECK_EQ(static_cast<int>(av->defineEquipmentVisual2D("armor.red", "body", "armor", nullptr, 5)),
             static_cast<int>(EquipmentVisualChange::Applied));
    CHECK_EQ(static_cast<int>(av->bindEquipment(&equipment)), static_cast<int>(EquipmentVisualChange::Applied));
    CHECK_EQ(av->getEquipmentVisualItem("body"), std::string{});

    auto* stack = equipment.stackAt("body");
    REQUIRE(stack != nullptr);
    stack->itemId   = "armor.red";
    stack->quantity = 1;
    av->update(0.f);
    CHECK_EQ(av->getEquipmentVisualItem("body"), std::string("armor.red"));
    REQUIRE(av->getLayerRenderable("armor") != nullptr);
    CHECK(av->getLayerRenderable("armor")->getVisible());

    stack->clear();
    av->sync();
    CHECK_EQ(av->getEquipmentVisualItem("body"), std::string{});
    CHECK(!av->getLayerRenderable("armor")->getVisible());
    av->release();
    delete av;
}

TEST_CASE("avatar.equipment.3dProjectionSwapsRootAndBoneParts") {
    Avatar*                      mod = Avatar::create();
    AvatarInstance*              av  = mod->newVroidAvatar();
    eve::inventory::EquipmentSet equipment;
    equipment.defineSlot("weapon");
    auto* sword = Renderable3D::create();
    REQUIRE(sword != nullptr);

    CHECK_EQ(static_cast<int>(av->defineEquipmentVisual3D("sword.iron", "weapon", sword, "", 1.f, 2.f, 3.f)),
             static_cast<int>(EquipmentVisualChange::Applied));
    av->bindEquipment(&equipment);
    auto* stack = equipment.stackAt("weapon");
    REQUIRE(stack != nullptr);
    stack->itemId   = "sword.iron";
    stack->quantity = 1;
    av->setPosition3D(10.f, 20.f, 30.f);
    av->sync();
    CHECK_EQ(av->getEquipmentVisualItem("weapon"), std::string("sword.iron"));

    stack->clear();
    av->update(0.f);
    CHECK_EQ(av->getEquipmentVisualItem("weapon"), std::string{});
    av->release();
    delete av;
    ecs::DestroyEntity(sword);
}

TEST_CASE("avatar.equipment.skinnedPartsSharePoseAndRespectWearOcclusion") {
    Avatar*                  mod = Avatar::create();
    AvatarInstance*          av  = mod->newVroidAvatar();
    eve::animation::AnimSkin bodySkin;
    eve::animation::AnimSkin shirtSkin;
    eve::animation::AnimSkin coatSkin;
    Mesh                     bodyMesh;
    Mesh                     shirtMesh;
    Mesh                     coatMesh;
    eve::graphics::Material  bodyMaterial;
    eve::graphics::Material  shirtMaterial;
    eve::graphics::Material  coatMaterial;

    CHECK_EQ(static_cast<int>(av->bindSkinnedPart(0, "body", &bodyMesh, &bodyMaterial, &bodySkin)),
             static_cast<int>(EquipmentVisualChange::Applied));
    REQUIRE(av->getRenderable3D() != nullptr);
    CHECK_EQ(av->getRenderable3D()->getPartName(0), std::string("body"));
    CHECK(av->getRenderable3D()->getPartMaterial(0) == &bodyMaterial);

    CHECK_EQ(static_cast<int>(av->defineEquipmentSkinnedVisual3D("linen.shirt", "shirt", 1, "shirt", &shirtMesh,
                                                                 &shirtMaterial, &shirtSkin)),
             static_cast<int>(EquipmentVisualChange::Applied));
    CHECK_EQ(static_cast<int>(av->defineEquipmentSkinnedVisual3D("plate.coat", "outerwear", 2, "coat", &coatMesh,
                                                                 &coatMaterial, &coatSkin)),
             static_cast<int>(EquipmentVisualChange::Applied));
    av->setEquipmentVisualLayer("linen.shirt", "shirt", "shirt", 0);
    av->setEquipmentVisualLayer("plate.coat", "outerwear", "outerwear", 0);

    eve::inventory::EquipmentSet equipment;
    equipment.defineSlot("shirt");
    equipment.defineSlot("outerwear");
    auto* shirt = equipment.stackAt("shirt");
    auto* coat  = equipment.stackAt("outerwear");
    REQUIRE(shirt != nullptr);
    REQUIRE(coat != nullptr);
    shirt->itemId   = "linen.shirt";
    shirt->quantity = 1;
    coat->itemId    = "plate.coat";
    coat->quantity  = 1;
    av->bindEquipment(&equipment);
    CHECK_EQ(av->getRenderable3D()->getPartName(1), std::string("shirt"));
    CHECK_EQ(av->getRenderable3D()->getPartName(2), std::string("coat"));
    CHECK(av->getRenderable3D()->getPartMaterial(1) == &shirtMaterial);
    CHECK(av->getRenderable3D()->getPartMaterial(2) == &coatMaterial);
    CHECK_EQ(av->getRenderable3D()->getPartSortPriority(1), 200000);
    CHECK_EQ(av->getRenderable3D()->getPartSortPriority(2), 300000);
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root", -1);
    eve::animation::AnimPlayer player(&skeleton);
    CHECK(av->bindAnimPlayer(&player));
    av->update(0.f);
    CHECK_EQ(static_cast<int>(av->getSkinnedPartUpdateMode(0)), static_cast<int>(SkinnedPartUpdateMode::Unavailable));

    CHECK_EQ(static_cast<int>(av->addEquipmentLayerOcclusion("outerwear", "shirt")),
             static_cast<int>(EquipmentVisualChange::Applied));
    CHECK(av->getRenderable3D()->getPartMesh(1) == nullptr);
    CHECK(av->getRenderable3D()->getPartMesh(2) == &coatMesh);

    coat->clear();
    av->sync();
    CHECK(av->getRenderable3D()->getPartMesh(1) == &shirtMesh);
    CHECK(av->getRenderable3D()->getPartMesh(2) == nullptr);
    CHECK_EQ(static_cast<int>(av->unbindSkinnedPart(0)), static_cast<int>(EquipmentVisualChange::Removed));
    CHECK(av->getRenderable3D()->getPartMesh(0) == nullptr);
    av->release();
    delete av;
}

TEST_CASE("avatar.equipment.layerHierarchyOrdersAndOccludesClothing") {
    Avatar*                      mod = Avatar::create();
    AvatarInstance*              av  = mod->newImageAvatar();
    eve::inventory::EquipmentSet equipment;
    equipment.defineSlot("underwear");
    equipment.defineSlot("shirt");
    equipment.defineSlot("outerwear");

    av->defineEquipmentVisual2D("briefs", "underwear", "briefs", nullptr, 0);
    av->defineEquipmentVisual2D("linen.shirt", "shirt", "shirt", nullptr, 0);
    av->defineEquipmentVisual2D("plate.coat", "outerwear", "coat", nullptr, 0);
    av->setEquipmentVisualLayer("briefs", "underwear", "underwear", 0);
    av->setEquipmentVisualLayer("linen.shirt", "shirt", "shirt", 0);
    av->setEquipmentVisualLayer("plate.coat", "outerwear", "outerwear", 0);

    auto equip = [&](const std::string& slot, const std::string& item) {
        auto* stack = equipment.stackAt(slot);
        REQUIRE(stack != nullptr);
        stack->itemId   = item;
        stack->quantity = 1;
    };
    equip("underwear", "briefs");
    equip("shirt", "linen.shirt");
    equip("outerwear", "plate.coat");
    av->bindEquipment(&equipment);
    CHECK_EQ(av->getEquipmentRenderStackCount(), 3);
    CHECK_EQ(av->getEquipmentRenderStackLayer(0), std::string("underwear"));
    CHECK_EQ(av->getEquipmentRenderStackLayer(1), std::string("shirt"));
    CHECK_EQ(av->getEquipmentRenderStackLayer(2), std::string("outerwear"));

    CHECK_EQ(static_cast<int>(av->addEquipmentLayerOcclusion("outerwear", "shirt")),
             static_cast<int>(EquipmentVisualChange::Applied));
    CHECK_EQ(av->getEquipmentRenderStackCount(), 2);
    CHECK(!av->getLayerRenderable("shirt")->getVisible());
    CHECK_EQ(av->getEquipmentRenderStackItem(1), std::string("plate.coat"));
    av->release();
    delete av;
}

TEST_CASE("avatar.vroid.meshMorphWeights") {
    Avatar*         mod = Avatar::create();
    AvatarInstance* av  = mod->newVroidAvatar();

    Mesh        mesh;
    const float base[]   = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
    const float joyAbs[] = {0.f, 1.f, 0.f, 1.f, 1.f, 0.f};  // lift Y by 1
    mesh.initMorphBase(2, base, nullptr, nullptr);
    CHECK(mesh.addMorphTargetAbsolute("Joy", joyAbs));
    CHECK(mesh.hasMorph("Joy"));

    av->setMesh(&mesh);
    CHECK(av->hasParameter("Joy"));
    av->defineExpression("happy", "Joy=1");
    CHECK(av->applyExpression("happy"));
    CHECK_EQ(mesh.getMorphWeight("Joy"), 1.f);

    std::vector<float> pos, nrm;
    mesh.computeMorphedPositions(pos, nrm);
    REQUIRE(pos.size() >= 6u);
    CHECK(std::fabs(pos[1] - 1.f) < 1e-5f);
    CHECK(std::fabs(pos[4] - 1.f) < 1e-5f);

    av->setParameter("Joy", 0.5f);
    CHECK_EQ(mesh.getMorphWeight("Joy"), 0.5f);

    av->release();
    delete av;
}

TEST_CASE("avatar.vroid.motionPlayerAndRootMotion") {
    Avatar*         mod = Avatar::create();
    AvatarInstance* av  = mod->newVroidAvatar();

    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("hips", -1);
    eve::animation::AnimClip     walk("walk");
    walk.setDuration(1.f);
    walk.setLoop(true);
    walk.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    walk.addPositionKey(root, 1.f, 1.f, 0.f, 0.f);
    walk.addEvent(0.5f, "footstep", "left");
    eve::animation::AnimPlayer     player(&skeleton);
    eve::animation::AnimLayerMixer mixer(&skeleton);

    CHECK(mixer.setBasePlayer(&player));
    CHECK(av->bindAnimLayerMixer(&mixer));
    CHECK(av->registerMotion("walk", &walk));
    av->setMotionBlendTime(0.f);
    av->setApplyRootMotion(true);
    av->setMotion("walk");
    CHECK(player.isPlaying());

    av->update(0.25f);
    CHECK(std::fabs(av->getRootMotionDeltaX()) < 1e-5f);
    av->update(0.25f);
    CHECK(std::fabs(av->getRootMotionDeltaX() - 0.25f) < 1e-5f);
    CHECK(std::fabs(av->getRootMotionDeltaZ()) < 1e-5f);
    CHECK_EQ(av->getAnimationEventCount(), 1);
    CHECK_EQ(av->getAnimationEventLayer(0), std::string("base"));
    CHECK_EQ(av->getAnimationEventName(0), std::string("footstep"));
    CHECK_EQ(av->getAnimationEventPayload(0), std::string("left"));
    av->update(0.75f);
    CHECK(std::fabs(av->getRootMotionDeltaX() - 0.75f) < 1e-5f);

    av->release();
    delete av;
}

TEST_CASE("avatar.vroid.humanoidAndVisemeSemantics") {
    Avatar*         mod = Avatar::create();
    AvatarInstance* av  = mod->newVroidAvatar();

    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("Hips", -1);
    skeleton.addBone("Spine", 0);
    const int head = skeleton.addBone("Head", 1);
    skeleton.setBindPosition(head, 0.f, 2.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);
    CHECK(av->bindAnimPlayer(&player));
    CHECK_EQ(av->autoMapHumanoidBones(), 3);
    CHECK_EQ(av->getHumanoidBoneName("hips"), std::string("Hips"));
    CHECK_EQ(av->getHumanoidBoneName("head"), std::string("Head"));
    CHECK(!av->mapHumanoidBone("leftHand", "missing"));

    Renderable3D* hat = Renderable3D::create();
    av->setPosition3D(3.f, 4.f, 5.f);
    CHECK(av->attachToBone("hat", "head", hat, 0.f, 0.5f, 0.f));
    CHECK_EQ(av->getAttachmentCount(), 1);
    av->update(0.f);
    CHECK(std::fabs(hat->transform()->x - 3.f) < 1e-5f);
    CHECK(std::fabs(hat->transform()->y - 6.5f) < 1e-5f);
    CHECK(std::fabs(hat->transform()->z - 5.f) < 1e-5f);
    CHECK(av->detachAttachment("hat"));
    CHECK_EQ(av->getAttachmentCount(), 0);
    ecs::DestroyEntity(hat);

    CHECK(av->setLookAtTarget(10.f, 6.f, 5.f));
    av->setLookAtWeight(1.f);
    av->update(0.f);
    CHECK(std::fabs(player.getPose()->getLocalRotationY(head)) > 0.1f);
    av->clearLookAtTarget();
    av->update(0.f);
    CHECK(std::fabs(player.getPose()->getLocalRotationY(head)) < 1e-5f);

    Mesh        mesh;
    const float base[]  = {0.f, 0.f, 0.f};
    const float delta[] = {0.f, 1.f, 0.f};
    mesh.initMorphBase(1, base, nullptr, nullptr);
    CHECK(mesh.addMorphTarget("aa", delta));
    CHECK(mesh.addMorphTarget("ih", delta));
    av->setMesh(&mesh);
    CHECK(av->mapViseme("aa", "aa"));
    CHECK(av->mapViseme("ih", "ih"));
    CHECK(av->setViseme("aa", 0.75f));
    CHECK_EQ(mesh.getMorphWeight("aa"), 0.75f);
    CHECK(av->setViseme("ih", 2.f));
    CHECK_EQ(mesh.getMorphWeight("aa"), 0.f);
    CHECK_EQ(mesh.getMorphWeight("ih"), 1.f);
    CHECK(av->defineExpression("speak-aa", "aa=1;ih=0"));
    CHECK(av->transitionExpression("speak-aa", 1.f));
    av->update(0.5f);
    CHECK(std::fabs(mesh.getMorphWeight("aa") - 0.5f) < 1e-5f);
    CHECK(std::fabs(mesh.getMorphWeight("ih") - 0.5f) < 1e-5f);
    av->update(0.5f);
    CHECK_EQ(mesh.getMorphWeight("aa"), 1.f);
    CHECK_EQ(mesh.getMorphWeight("ih"), 0.f);

    av->release();
    delete av;
}

TEST_CASE("avatar.tween.bindDrivesPosition") {
    Avatar*         mod  = Avatar::create();
    auto*           anim = eve::animation::Animation::create();
    AvatarInstance* av   = mod->newImageAvatar();
    av->setPosition(0.f, 0.f);

    eve::animation::Tween* tw = anim->newTween(1.f);
    tw->setFrom("x", 0.f);
    tw->setTo("x", 100.f);
    tw->setFrom("y", 10.f);
    tw->setTo("y", 10.f);
    tw->setEase("linear");
    tw->start();
    av->bindTween(tw);

    anim->update(0.5f);
    av->update(0.f);
    CHECK(std::fabs(av->getX() - 50.f) < 1e-3f);
    CHECK(std::fabs(av->getY() - 10.f) < 1e-3f);

    av->unbindTween();
    av->release();
    delete av;
}

TEST_CASE("avatar.module.updateSyncCounts") {
    Avatar*         mod    = Avatar::create();
    const int       before = mod->getAvatarCount();
    AvatarInstance* a      = mod->newImageAvatar();
    AvatarInstance* b      = mod->newVroidAvatar();
    CHECK_EQ(mod->getAvatarCount(), before + 2);
    mod->update(0.01f);
    mod->sync();
    a->release();
    delete a;
    b->release();
    delete b;
    CHECK_EQ(mod->getAvatarCount(), before);
}

TEST_CASE("graphics.mesh.morphCpuBake") {
    Mesh        mesh;
    const float base[]  = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 2.f, 0.f};
    const float delta[] = {0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f};
    mesh.initMorphBase(3, base, nullptr, nullptr);
    CHECK(mesh.addMorphTarget("open", delta));
    CHECK_EQ(mesh.getMorphCount(), 1);
    CHECK(mesh.setMorphWeight("open", 1.f));
    std::vector<float> pos, nrm;
    mesh.computeMorphedPositions(pos, nrm);
    CHECK(std::fabs(pos[1] - 1.f) < 1e-5f);
    CHECK(std::fabs(pos[4] - 1.f) < 1e-5f);
    CHECK(std::fabs(pos[7] - 3.f) < 1e-5f);
}

// avatar_attachment_point.cpp
TEST_CASE("avatar.attachment_point samples an owned world-pose snapshot") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root  = skeleton.addBone("Root", -1);
    const int                    blade = skeleton.addBone("BladeRoot", root);
    skeleton.setBindPosition(blade, 0.f, 2.f, 0.f);
    eve::animation::AnimPlayer  player(&skeleton);
    eve::avatar::AvatarInstance avatar("vroid");
    REQUIRE(avatar.bindAnimPlayer(&player));
    REQUIRE(avatar.mapHumanoidBone("weaponRoot", "BladeRoot"));
    avatar.setPosition3D(10.f, 20.f, 30.f);
    avatar.setScale3D(2.f, 3.f, 4.f);
    avatar.setRotation3D(1.57079632679f, 0.f, 0.f);
    avatar.update(0.f);

    auto semantic = avatar.sampleAttachmentPoint("weaponRoot", {1.f, 0.f, 0.f});
    REQUIRE(semantic.ok());
    CHECK(std::fabs(semantic.value().x - 10.f) < 1e-4f);
    CHECK(std::fabs(semantic.value().y - 26.f) < 1e-4f);
    CHECK(std::fabs(semantic.value().z - 28.f) < 1e-4f);
    auto native = avatar.sampleAttachmentPoint("BladeRoot", {1.f, 0.f, 0.f});
    REQUIRE(native.ok());
    CHECK(std::fabs(native.value().x - semantic.value().x) < 1e-5f);
    REQUIRE(!avatar.sampleAttachmentPoint("missing").ok());

    eve::animation::AnimSkeleton replacementSkeleton;
    replacementSkeleton.addBone("Replacement", -1);
    eve::animation::AnimPlayer replacementPlayer(&replacementSkeleton);
    REQUIRE(avatar.bindAnimPlayer(&replacementPlayer));
    REQUIRE(!avatar.sampleAttachmentPoint("BladeRoot").ok());
    avatar.update(0.f);
    REQUIRE(!avatar.sampleAttachmentPoint("BladeRoot").ok());

    avatar.release();
    REQUIRE(!avatar.sampleAttachmentPoint("Replacement").ok());
}
}  // namespace test_avatar_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_avatar_vrm)
#include "PrivateTestScope.h"
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "Fixtures.h"
#include "avatar/AvatarInstance.h"
#include "avatar/VrmDocument.h"
#include "common/ECS.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// avatar_vrm.cpp
namespace test_avatar_vrm_unit {
namespace {
[[maybe_unused]] std::vector<std::byte> vrmGlb(std::string json) {
    while (json.size() % 4) json += ' ';
    std::vector<std::byte> bytes(20 + json.size());
    const uint32_t header[] = {0x46546c67, 2, static_cast<uint32_t>(bytes.size()), static_cast<uint32_t>(json.size()),
                               0x4e4f534a};
    std::memcpy(bytes.data(), header, 20);
    std::memcpy(bytes.data() + 20, json.data(), json.size());
    return bytes;
}
const char* minimalVrm = R"({"asset":{"version":"2.0"},"nodes":[{"name":"head"}],
"extensions":{"VRMC_vrm":{"specVersion":"1.0","humanoid":{"humanBones":{"head":{"node":0}}}}}})";
}  // namespace

TEST_CASE("avatar.vrm.parseVersionAndHumanoid") {
    auto bytes  = vrmGlb(minimalVrm);
    auto result = eve::avatar::parseVrm(bytes);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().version, std::string("1.0"));
    REQUIRE_EQ(result.value().humanoid.at("head"), 0);
}

TEST_CASE("avatar.vrm.rejectTruncatedAndUnknownVersion") {
    auto bytes = vrmGlb(minimalVrm);
    bytes.pop_back();
    auto truncated = eve::avatar::parseVrm(bytes);
    REQUIRE(!truncated.ok());
    std::string future = minimalVrm;
    future.replace(future.find("specVersion\":\"1.0") + 14, 3, "9.0");
    auto unknown = eve::avatar::parseVrm(vrmGlb(future));
    REQUIRE(!unknown.ok());
}

TEST_CASE("avatar.vrm.rejectInvalidNodeReference") {
    std::string invalid = minimalVrm;
    invalid.replace(invalid.find("\"node\":0"), 8, "\"node\":7");
    auto result = eve::avatar::parseVrm(vrmGlb(invalid));
    REQUIRE(!result.ok());
}

TEST_CASE("avatar.vrm.failedLoadPreservesPath") {
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        result = avatar.loadVroidModel("does-not-exist.vrm");
    REQUIRE(!result.ok());
    REQUIRE(avatar.getVroidModelPath().empty());
    REQUIRE_EQ(avatar.getVroidMeshCount(), 0);
}
}  // namespace test_avatar_vrm_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_avatar_appearance)
#include "avatar/Appearance.h"
#include "graphics/Mesh.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

namespace test_avatar_appearance {
eve::Value json(std::string_view source) {
    auto result = eve::Value::fromJson(source);
    if (!result.ok()) throw std::runtime_error("Invalid test JSON");
    return std::move(result).takeValue();
}
eve::Value definition() {
    return json(R"({"schema":"eve.appearance.definition","version":1,"profile":"human.v1",
      "parameters":[{"id":"jaw","min":-1,"max":1,"default":0},{"id":"smile","min":0,"max":1,"default":0}],
      "drivers":[
        {"output":"jawWide","scale":1,"inputs":[{"parameter":"jaw","start":0,"end":1}]},
        {"output":"jawNarrow","scale":1,"inputs":[{"parameter":"jaw","start":0,"end":-1}]},
        {"output":"cheekFix","scale":0.5,"inputs":[{"parameter":"jaw","start":0,"end":1},{"parameter":"smile","start":0,"end":1}]}],
      "slots":[{"id":"hair","default":"long"},{"id":"hat","default":""},{"id":"body","default":"skin"}],
      "items":[
        {"id":"long","slot":"hair","parts":["longHair"],"hides":[],"excludes":["helmet"]},
        {"id":"short","slot":"hair","parts":["shortHair"],"hides":[],"excludes":[]},
        {"id":"helmet","slot":"hat","parts":["helmet"],"hides":["shortHair"],"excludes":[]},
        {"id":"skin","slot":"body","parts":["head","torso"],"hides":[],"excludes":[]}]})");
}
double channel(const eve::avatar::Appearance& appearance, const std::string& name) {
    const auto output = appearance.evaluate();
    return output.find("channels")->find(name)->asDouble();
}
std::string encoded(const eve::Value& value) {
    auto result = value.toJson();
    if (!result.ok()) throw std::runtime_error("JSON encode failed");
    return std::move(result).takeValue();
}

TEST_CASE("avatar.appearance.signedChannelsAndCorrective") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.8,"smile":0.5}})")).ok());
    CHECK(std::abs(channel(appearance, "cheekFix") - 0.2) < 1e-12);
    CHECK(std::abs(channel(appearance, "jawWide") - 0.8) < 1e-12);
    CHECK_EQ(channel(appearance, "jawNarrow"), 0.0);
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":-0.6}})")).ok());
    CHECK(std::abs(channel(appearance, "jawNarrow") - 0.6) < 1e-12);
    CHECK_EQ(channel(appearance, "jawWide"), 0.0);
    CHECK_EQ(channel(appearance, "cheekFix"), 0.0);
}

TEST_CASE("avatar.appearance.atomicWardrobeAndHistory") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    const auto initial = encoded(appearance.snapshot());
    CHECK(!appearance.apply(json(R"({"parameters":{"jaw":0.5},"selections":{"hat":"helmet"}})")).ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    CHECK(!appearance.canUndo());
    REQUIRE(appearance.apply(json(R"({"selections":{"hair":"short","hat":"helmet"}})")).ok());
    const auto output  = appearance.evaluate();
    const auto visible = encoded(*output.find("visibleParts"));
    CHECK(visible.find("shortHair") == std::string::npos);
    CHECK(visible.find("helmet") != std::string::npos);
    CHECK(visible.find("head") != std::string::npos);
    const auto equipped = encoded(appearance.snapshot());
    REQUIRE(appearance.undo().ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    REQUIRE(appearance.redo().ok());
    CHECK_EQ(encoded(appearance.snapshot()), equipped);
    REQUIRE(appearance.apply(json(R"({"selections":{"hair":"short"}})")).ok());
    REQUIRE(appearance.undo().ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.2}})")).ok());
    CHECK(!appearance.canRedo());
}

TEST_CASE("avatar.appearance.rejectInvalidStateAndDefinition") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    const auto initial = encoded(appearance.snapshot());
    for (auto source :
         {R"({"parameters":{"jaw":2}})", R"({"parameters":{"unknown":0}})", R"({"selections":{"missing":""}})",
          R"({"selections":{"hat":"long"}})", R"({"unknown":true})", R"({"parameters":{"jaw":true}})"}) {
        CHECK(!appearance.apply(json(source)).ok());
        CHECK_EQ(encoded(appearance.snapshot()), initial);
    }
    auto nonfinite = json(R"({"parameters":{"jaw":0}})");
    nonfinite.find("parameters")->set("jaw", std::numeric_limits<double>::quiet_NaN());
    CHECK(!appearance.apply(nonfinite).ok());
    for (auto version : {0, 2}) {
        auto invalid = definition();
        invalid.set("version", version);
        CHECK(!appearance.configure(invalid).ok());
        CHECK_EQ(encoded(appearance.snapshot()), initial);
    }
    auto invalid = definition();
    invalid.find("drivers")->at(0).find("inputs")->at(0).set("end", 0.0);
    CHECK(!appearance.configure(invalid).ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    CHECK(!appearance.canUndo());
}

TEST_CASE("avatar.appearance.snapshotRoundTripAndProfileIsolation") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.7}})")).ok());
    const auto              saved = appearance.snapshot();
    eve::avatar::Appearance restored;
    REQUIRE(restored.configure(definition()).ok());
    REQUIRE(restored.restore(json(encoded(saved))).ok());
    CHECK_EQ(encoded(restored.evaluate()), encoded(appearance.evaluate()));
    auto foreign = saved;
    foreign.set("profile", "other.v1");
    CHECK(!restored.restore(foreign).ok());
    auto future = saved;
    future.set("version", 2);
    CHECK(!restored.restore(future).ok());
    auto incomplete = saved;
    incomplete.set("parameters", eve::Value(eve::Value::Object{}));
    CHECK(!restored.restore(incomplete).ok());
    CHECK_EQ(encoded(restored.snapshot()), encoded(saved));
    for (int i = 0; i < 80; ++i)
        REQUIRE(restored.apply(json(i % 2 ? R"({"parameters":{"jaw":0.1}})" : R"({"parameters":{"jaw":0.2}})")).ok());
    int count = 0;
    while (restored.canUndo()) {
        REQUIRE(restored.undo().ok());
        ++count;
    }
    CHECK_EQ(count, 64);
}

TEST_CASE("avatar.appearance.morphProjectionValidatesBeforeWriting") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.75,"smile":0.5}})")).ok());
    eve::graphics::Mesh mesh;
    const float         positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const float         delta[]     = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.initMorphBase(3, positions);
    REQUIRE(mesh.addMorphTarget("jawWide", delta));
    REQUIRE(mesh.addMorphTarget("jawNarrow", delta));
    REQUIRE(mesh.setMorphWeight("jawWide", 0.1f));
    CHECK(!appearance.applyMorphWeights(mesh, "").ok());  // cheekFix is missing.
    CHECK_EQ(mesh.getMorphWeight("jawWide"), 0.1f);
    REQUIRE(mesh.addMorphTarget("cheekFix", delta));
    REQUIRE(mesh.addMorphTarget("expression", delta));
    REQUIRE(mesh.setMorphWeight("expression", 0.9f));
    REQUIRE(appearance.applyMorphWeights(mesh, "").ok());
    CHECK_EQ(mesh.getMorphWeight("jawWide"), 0.75f);
    CHECK_EQ(mesh.getMorphWeight("cheekFix"), 0.1875f);
    CHECK_EQ(mesh.getMorphWeight("expression"), 0.9f);
    std::vector<float> blended, normals;
    mesh.computeMorphedPositions(blended, normals);
    CHECK(std::abs(blended[2] - 1.8375f) < 1e-6f);
}

TEST_CASE("avatar.appearance.morphNormalsBlendAndRejectAtomically") {
    eve::graphics::Mesh mesh;
    const float         positions[]   = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const float         normals[]     = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float         delta[]       = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    const float         normalDelta[] = {1, 0, -1, 1, 0, -1, 1, 0, -1};
    mesh.initMorphBase(3, positions, normals);
    REQUIRE(mesh.addMorphTarget("jaw", delta));
    REQUIRE(mesh.setMorphNormalDeltas("jaw", normalDelta).ok());
    REQUIRE(mesh.setMorphWeight("jaw", 0.5f));
    const float malformed[] = {1, 2, 3};
    CHECK(!mesh.setMorphNormalDeltas("jaw", malformed).ok());
    CHECK(!mesh.setMorphNormalDeltas("missing", normalDelta).ok());
    std::vector<float> pos, nrm;
    mesh.computeMorphedPositions(pos, nrm);
    CHECK(std::abs(nrm[0] - std::sqrt(0.5f)) < 1e-6f);
    CHECK(std::abs(nrm[2] - std::sqrt(0.5f)) < 1e-6f);
    auto invalid = std::vector<float>(std::begin(normalDelta), std::end(normalDelta));
    invalid[0]   = std::numeric_limits<float>::quiet_NaN();
    CHECK(!mesh.setMorphNormalDeltas("jaw", invalid).ok());
    REQUIRE(mesh.setMorphNormalDeltas("jaw", {}).ok());
    mesh.computeMorphedPositions(pos, nrm);
    CHECK_EQ(nrm[0], 0.0f);
    CHECK_EQ(nrm[2], 1.0f);
}

}  // namespace test_avatar_appearance
#endif
