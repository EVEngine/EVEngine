#include "avatar/Avatar.h"
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


TEST_CASE("avatar.script.buildsInspectorAndMotionBrowserFromMetadata") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        avatarModule <- eve.Avatar();
        animationModule <- eve.Animation();
        avatar <- avatarModule.newVroidAvatar();
        blinkDefined <- avatar.defineParameter("blink", 0.0, 0.0, 1.0);
        blinkMin <- avatar.getParameterMinimum("blink");
        blinkMax <- avatar.getParameterMaximum("blink");
        blinkDefault <- avatar.getParameterDefault("blink");
        clip <- animationModule.newClip("idle");
        motionRegistered <- avatar.registerMotion("idle", clip);
        motionCount <- avatar.getMotionCount();
        motionName <- avatar.getMotionName(0);
        sameClip <- avatar.getMotionClip("idle").getName();
        expressionDefined <- avatar.defineExpression("happy", "blink=0");
        expressionName <- avatar.getExpressionName(0);
        motionRemoved <- avatar.unregisterMotion("idle");
    )"));

    CHECK(vm.find("blinkDefined").toBool());
    CHECK_EQ(vm.find("blinkMin").toFloat(), 0.f);
    CHECK_EQ(vm.find("blinkMax").toFloat(), 1.f);
    CHECK_EQ(vm.find("blinkDefault").toFloat(), 0.f);
    CHECK(vm.find("motionRegistered").toBool());
    CHECK_EQ(vm.find("motionCount").toInt(), 1);
    CHECK_EQ(vm.find("motionName").toString(), std::string("idle"));
    CHECK_EQ(vm.find("sameClip").toString(), std::string("idle"));
    CHECK(vm.find("expressionDefined").toBool());
    CHECK_EQ(vm.find("expressionName").toString(), std::string("happy"));
    CHECK(vm.find("motionRemoved").toBool());
}


// avatar_attachment_point.cpp
