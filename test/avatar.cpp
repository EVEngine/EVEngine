#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "animation/Animation.h"
#include "animation/Tween.h"
#include "avatar/Avatar.h"
#include "avatar/AvatarInstance.h"
#include "avatar/Live2DNullBackend.h"
#include "common/ECS.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace eve::avatar;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;

namespace {

class FakeLive2D : public ILive2DBackend {
public:
    std::string getName() const override { return "fake"; }
    bool loadModel(const std::string &path) override {
        loadedPath = path;
        return !path.empty();
    }
    void update(float) override { ++updates; }
    void setParameter(const std::string &name, float value) override { params[name] = value; }
    float getParameter(const std::string &name) const override {
        auto it = params.find(name);
        return it == params.end() ? 0.f : it->second;
    }
    bool setExpression(const std::string &name) override {
        expression = name;
        return true;
    }
    bool setMotion(const std::string &name) override {
        motion = name;
        return true;
    }

    std::string loadedPath;
    std::string expression;
    std::string motion;
    int updates = 0;
    std::unordered_map<std::string, float> params;
};

ILive2DBackend *makeFakeLive2D() { return new FakeLive2D(); }

}  // namespace

TEST_CASE("avatar.image.layersAndExpression") {
    Avatar *mod = Avatar::create();
    REQUIRE(mod != nullptr);

    AvatarInstance *av = mod->newImageAvatar();
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
    Avatar *mod = Avatar::create();
    AvatarInstance *av = mod->newLive2DAvatar();
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

TEST_CASE("avatar.live2d.backendPlugIn") {
    Avatar::registerLive2DBackend(&makeFakeLive2D);
    Avatar *mod = Avatar::create();
    AvatarInstance *av = mod->newLive2DAvatar();
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
    Avatar *mod = Avatar::create();
    AvatarInstance *av = mod->newVroidAvatar();
    CHECK_EQ(av->getKind(), std::string("vroid"));
    CHECK(av->loadVroidModelPath("chars/hero.vrm"));
    CHECK_EQ(av->getVroidModelPath(), std::string("chars/hero.vrm"));
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

TEST_CASE("avatar.vroid.meshMorphWeights") {
    Avatar *mod = Avatar::create();
    AvatarInstance *av = mod->newVroidAvatar();

    Mesh mesh;
    const float base[] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
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
    Avatar *mod = Avatar::create();
    auto *anim = eve::animation::Animation::create();
    AvatarInstance *av = mod->newImageAvatar();
    av->setPosition(0.f, 0.f);

    eve::animation::Tween *tw = anim->newTween(1.f);
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
    Avatar *mod = Avatar::create();
    const int before = mod->getAvatarCount();
    AvatarInstance *a = mod->newImageAvatar();
    AvatarInstance *b = mod->newVroidAvatar();
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
    Mesh mesh;
    const float base[] = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 2.f, 0.f};
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
