#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/Tween.h"
#include "avatar/Avatar.h"
#include "avatar/AvatarInstance.h"
#include "avatar/Live2DNullBackend.h"
#include "common/ECS.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace eve::avatar;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;

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
    CHECK(av->hasLive2DBackend());
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
