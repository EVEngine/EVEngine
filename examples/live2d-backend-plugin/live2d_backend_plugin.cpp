#include "avatar/Avatar.h"
#include "avatar/AvatarInstance.h"
#include "common/Export.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>
#include <unordered_map>

// Demo Live2D backend — proves plugin registration. Swap the body for Cubism Core.
namespace {

class DemoLive2DBackend : public eve::avatar::ILive2DBackend {
public:
    std::string getName() const override { return "demo"; }

    bool loadModel(const std::string &path) override {
        path_ = path;
        return !path.empty();
    }

    void update(float /*dt*/) override {}

    void setParameter(const std::string &name, float value) override {
        if (!name.empty()) params_[name] = value;
    }

    float getParameter(const std::string &name) const override {
        auto it = params_.find(name);
        return it == params_.end() ? 0.f : it->second;
    }

    bool setExpression(const std::string &name) override {
        expression_ = name;
        return true;
    }

    bool setMotion(const std::string &name) override {
        motion_ = name;
        return true;
    }

private:
    std::string path_;
    std::string expression_;
    std::string motion_;
    std::unordered_map<std::string, float> params_;
};

eve::avatar::ILive2DBackend *createDemoLive2D() { return new DemoLive2DBackend(); }

}  // namespace

namespace eve::live2dplugin {

class Live2DBackendPlugin : public Module {
public:
    Module_REG(Live2DBackendPlugin);
    Live2DBackendPlugin() = default;
    ~Live2DBackendPlugin() override = default;

    std::string backendName() const { return eve::avatar::Avatar::getLive2DBackendName(); }
};

Module_IMPL(Live2DBackendPlugin, new Live2DBackendPlugin());

void Live2DBackendPlugin::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Live2DBackendPlugin::create, false);
    expose(cls);
}

void Live2DBackendPlugin::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Live2DBackendPlugin::getName);
    cls.addFunc("backendName", &Live2DBackendPlugin::backendName);
}

}  // namespace eve::live2dplugin

EVE_PLUGIN_EXPORT int eve_plugin_init(void) {
    eve::avatar::Avatar::registerLive2DBackend(&createDemoLive2D);
    return 0;
}
