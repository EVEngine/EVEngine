#include "avatar/Avatar.h"
#include "avatar/Live2DNullBackend.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"

#include <algorithm>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::avatar {

Live2DBackendFactory Avatar::live2dFactory_ = &createNullLive2DBackend;

Module_IMPL(Avatar, new Avatar());

Avatar::~Avatar() {
    // Instances may outlive the module if script GC still holds them; detach.
    for (AvatarInstance *a : avatars_) {
        (void)a;
    }
    avatars_.clear();
}

void Avatar::registerInstance(AvatarInstance *a) {
    if (!a) return;
    if (std::find(avatars_.begin(), avatars_.end(), a) == avatars_.end())
        avatars_.push_back(a);
}

void Avatar::unregisterInstance(AvatarInstance *a) {
    if (!a) return;
    auto it = std::find(avatars_.begin(), avatars_.end(), a);
    if (it != avatars_.end()) avatars_.erase(it);
}

AvatarInstance *Avatar::newImageAvatar() { return new AvatarInstance("image"); }

AvatarInstance *Avatar::newLive2DAvatar() { return new AvatarInstance("live2d"); }

AvatarInstance *Avatar::newVroidAvatar() { return new AvatarInstance("vroid"); }

void Avatar::update(float dt) {
    std::vector<AvatarInstance *> snapshot = avatars_;
    for (AvatarInstance *a : snapshot) {
        if (a) a->update(dt);
    }
}

void Avatar::sync() {
    std::vector<AvatarInstance *> snapshot = avatars_;
    for (AvatarInstance *a : snapshot) {
        if (a) a->sync();
    }
}

void Avatar::render(graphics::Graphics *gfx) {
    sync();
    if (!gfx) return;
    std::vector<graphics::DrawItem2D> items;
    graphics::RenderSystem::collectSprites(items);
    graphics::RenderSystem::drawItems(*gfx, items, false);
}

int Avatar::getAvatarCount() const { return int(avatars_.size()); }

void Avatar::registerLive2DBackend(Live2DBackendFactory factory) {
    live2dFactory_ = factory ? factory : &createNullLive2DBackend;
}

Live2DBackendFactory Avatar::live2DBackendFactory() { return live2dFactory_; }

ILive2DBackend *Avatar::createLive2DBackend() {
    Live2DBackendFactory f = live2dFactory_ ? live2dFactory_ : &createNullLive2DBackend;
    return f();
}

std::string Avatar::getLive2DBackendName() {
    ILive2DBackend *tmp = createLive2DBackend();
    if (!tmp) return "none";
    std::string n = tmp->getName();
    delete tmp;
    return n;
}

void Avatar::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Avatar::create, false);
    expose(cls);

    auto av = table.addClass<AvatarInstance>(
        "AvatarInstance",
        std::function<AvatarInstance *()>([]() -> AvatarInstance * { return nullptr; }), true);

    av.addFunc("getKind", &AvatarInstance::getKind);
    av.addFunc("setPosition", &AvatarInstance::setPosition);
    av.addFunc("getX", &AvatarInstance::getX);
    av.addFunc("getY", &AvatarInstance::getY);
    av.addFunc("setScale", &AvatarInstance::setScale);
    av.addFunc("getScaleX", &AvatarInstance::getScaleX);
    av.addFunc("getScaleY", &AvatarInstance::getScaleY);
    av.addFunc("setVisible", &AvatarInstance::setVisible);
    av.addFunc("isVisible", &AvatarInstance::isVisible);
    av.addFunc("setLayer", &AvatarInstance::setLayer);
    av.addFunc("getLayer", &AvatarInstance::getLayer);
    av.addFunc("setExpression", &AvatarInstance::setExpression);
    av.addFunc("getExpression", &AvatarInstance::getExpression);
    av.addFunc("setMotion", &AvatarInstance::setMotion);
    av.addFunc("getMotion", &AvatarInstance::getMotion);
    av.addFunc("setParameter", &AvatarInstance::setParameter);
    av.addFunc("getParameter", &AvatarInstance::getParameter);
    av.addFunc("hasParameter", &AvatarInstance::hasParameter);
    av.addFunc("getParameterCount", &AvatarInstance::getParameterCount);
    av.addFunc("getParameterName", &AvatarInstance::getParameterName);
    av.addFunc("update", &AvatarInstance::update);
    av.addFunc("sync", &AvatarInstance::sync);
    av.addFunc("release", &AvatarInstance::release);

    // addLayer / setLayerTexture accept a nullable texture (colored placeholder
    // layers). ssq's default pointer binding rejects Squirrel `null`, so accept
    // ssq::Object and translate null -> nullptr.
    av.addFunc("addLayer", [](AvatarInstance *a, const std::string &name, ssq::Object textureObj,
                              int zIndex) -> bool {
        graphics::Texture *tex =
            textureObj.isNull() ? nullptr : textureObj.toPtrUnsafe<graphics::Texture *>();
        return a->addLayer(name, tex, zIndex);
    });
    av.addFunc("setLayerTexture", [](AvatarInstance *a, const std::string &name,
                                     ssq::Object textureObj) -> bool {
        graphics::Texture *tex =
            textureObj.isNull() ? nullptr : textureObj.toPtrUnsafe<graphics::Texture *>();
        return a->setLayerTexture(name, tex);
    });
    av.addFunc("setLayerVisible", &AvatarInstance::setLayerVisible);
    av.addFunc("setLayerOffset", &AvatarInstance::setLayerOffset);
    av.addFunc("setLayerColor", &AvatarInstance::setLayerColor);
    av.addFunc("setLayerZ", &AvatarInstance::setLayerZ);
    av.addFunc("setLayerSize", &AvatarInstance::setLayerSize);
    av.addFunc("getLayerCount", &AvatarInstance::getLayerCount);
    av.addFunc("getLayerName", &AvatarInstance::getLayerName);
    av.addFunc("hasLayer", &AvatarInstance::hasLayer);
    av.addFunc("defineExpression", &AvatarInstance::defineExpression);
    av.addFunc("applyExpression", &AvatarInstance::applyExpression);

    av.addFunc("loadLive2DModel", &AvatarInstance::loadLive2DModel);
    av.addFunc("getLive2DBackendName", &AvatarInstance::getLive2DBackendName);
    av.addFunc("hasLive2DBackend", &AvatarInstance::hasLive2DBackend);

    av.addFunc("loadVroidModelPath", &AvatarInstance::loadVroidModelPath);
    av.addFunc("bindVroidModelData", &AvatarInstance::bindVroidModelData);
    av.addFunc("loadMorphNamesFromModel", &AvatarInstance::loadMorphNamesFromModel);
    av.addFunc("setMesh", &AvatarInstance::setMesh);
    av.addFunc("setTexture", &AvatarInstance::setTexture);
    av.addFunc("setPosition3D", &AvatarInstance::setPosition3D);
    av.addFunc("setRotation3D", &AvatarInstance::setRotation3D);
    av.addFunc("setScale3D", &AvatarInstance::setScale3D);
    av.addFunc("getRenderable3D", &AvatarInstance::getRenderable3D);
    av.addFunc("getBoundMesh", &AvatarInstance::getBoundMesh);
    av.addFunc("getVroidModelPath", &AvatarInstance::getVroidModelPath);
    av.addFunc("bakeMorphs", &AvatarInstance::bakeMorphs);
    av.addFunc("bindTween", &AvatarInstance::bindTween);
    av.addFunc("unbindTween", &AvatarInstance::unbindTween);
    av.addFunc("getBoundTween", &AvatarInstance::getBoundTween);
}

void Avatar::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Avatar::getName);
    cls.addFunc("newImageAvatar", &Avatar::newImageAvatar);
    cls.addFunc("newLive2DAvatar", &Avatar::newLive2DAvatar);
    cls.addFunc("newVroidAvatar", &Avatar::newVroidAvatar);
    cls.addFunc("update", &Avatar::update);
    cls.addFunc("sync", &Avatar::sync);
    cls.addFunc("render", &Avatar::render);
    cls.addFunc("getAvatarCount", &Avatar::getAvatarCount);
    // Static method: wrap so SSQ gets an instance-method signature (raw static
    // function pointers make Class::addFunc recurse until stack overflow).
    cls.addFunc("getLive2DBackendName",
                [](Avatar *) -> std::string { return Avatar::getLive2DBackendName(); });
}

}  // namespace eve::avatar
