#include "avatar/Avatar.h"
#include "avatar/Live2DNullBackend.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"

#include <algorithm>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::avatar {
namespace {

template <class Map>
std::vector<std::string> sortedKeys(const Map& values) {
    std::vector<std::string> names;
    names.reserve(values.size());
    for (const auto& [name, value] : values) {
        (void)value;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

void AvatarInstance::ensureParameter(const std::string& name, float value) {
    if (name.empty()) return;
    if (parameters_.find(name) == parameters_.end()) {
        parameterOrder_.push_back(name);
        parameterMetadata_.emplace(name, ParameterMetadata{value, 0.f, 1.f});
    }
    parameters_[name] = value;
}

bool AvatarInstance::defineParameter(const std::string& name, float defaultValue, float minimum,
                                     float maximum) {
    if (name.empty() || minimum > maximum) return false;
    ensureParameter(name, defaultValue);
    parameterMetadata_[name] = {defaultValue, minimum, maximum};
    return true;
}

float AvatarInstance::getParameterDefault(const std::string& name) const {
    const auto found = parameterMetadata_.find(name);
    return found == parameterMetadata_.end() ? 0.f : found->second.defaultValue;
}

float AvatarInstance::getParameterMinimum(const std::string& name) const {
    const auto found = parameterMetadata_.find(name);
    return found == parameterMetadata_.end() ? 0.f : found->second.minimum;
}

float AvatarInstance::getParameterMaximum(const std::string& name) const {
    const auto found = parameterMetadata_.find(name);
    return found == parameterMetadata_.end() ? 1.f : found->second.maximum;
}

bool AvatarInstance::removeExpression(const std::string& name) {
    return expressionDefs_.erase(name) > 0;
}

int AvatarInstance::getExpressionCount() const {
    return static_cast<int>(expressionDefs_.size());
}

std::string AvatarInstance::getExpressionName(int index) const {
    const std::vector<std::string> names = sortedKeys(expressionDefs_);
    return index < 0 || index >= static_cast<int>(names.size()) ? std::string{}
                                                               : names[static_cast<size_t>(index)];
}

bool AvatarInstance::unregisterMotion(const std::string& name) {
    return motions_.erase(name) > 0;
}

int AvatarInstance::getMotionCount() const {
    return static_cast<int>(motions_.size());
}

std::string AvatarInstance::getMotionName(int index) const {
    const std::vector<std::string> names = sortedKeys(motions_);
    return index < 0 || index >= static_cast<int>(names.size()) ? std::string{}
                                                               : names[static_cast<size_t>(index)];
}

animation::AnimClip* AvatarInstance::getMotionClip(const std::string& name) const {
    const auto found = motions_.find(name);
    return found == motions_.end() ? nullptr : found->second;
}

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
    for (AvatarInstance* a : avatars_) {
        if (a) a->collectLive2DDrawItems(items);
    }
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

bool Avatar::hasLive2DBackend() {
    ILive2DBackend* tmp = createLive2DBackend();
    if (!tmp) return false;
    const bool available = tmp->isRuntimeAvailable();
    delete tmp;
    return available;
}

void Avatar::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Avatar::create, false);
    expose(cls);

    auto av = table.addClass<AvatarInstance>(
        "AvatarInstance",
        std::function<AvatarInstance *()>([]() -> AvatarInstance * { return nullptr; }), true);

    auto sprite = table.addClass<graphics::Renderable2D>(
        "Renderable2D", std::function<graphics::Renderable2D*()>([]() { return graphics::Renderable2D::create(); }),
        false);
    sprite.addFunc("setPosition", &graphics::Renderable2D::setPosition);
    sprite.addFunc("getX", &graphics::Renderable2D::getX);
    sprite.addFunc("getY", &graphics::Renderable2D::getY);
    sprite.addFunc("setRotation", &graphics::Renderable2D::setRotation);
    sprite.addFunc("getRotation", &graphics::Renderable2D::getRotation);
    sprite.addFunc("setScale", &graphics::Renderable2D::setScale);
    sprite.addFunc("setSize", &graphics::Renderable2D::setSize);
    sprite.addFunc("setColor", &graphics::Renderable2D::setColor);
    sprite.addFunc("setLayer", &graphics::Renderable2D::setLayer);
    sprite.addFunc("getLayer", &graphics::Renderable2D::getLayer);
    sprite.addFunc("setVisible", &graphics::Renderable2D::setVisible);
    sprite.addFunc("isVisible", &graphics::Renderable2D::getVisible);
    sprite.addFunc("setTexture", &graphics::Renderable2D::setTexture);
    sprite.addFunc("setQuad", &graphics::Renderable2D::setQuad);
    sprite.addFunc("setReceiveLight", &graphics::Renderable2D::setReceiveLight);
    sprite.addFunc("getReceiveLight", &graphics::Renderable2D::getReceiveLight);
    sprite.addFunc("setCastOcclusion", &graphics::Renderable2D::setCastOcclusion);
    sprite.addFunc("getCastOcclusion", &graphics::Renderable2D::getCastOcclusion);
    sprite.addFunc("setBlendMode", &graphics::Renderable2D::setBlend);
    sprite.addFunc("getBlendMode", &graphics::Renderable2D::getBlend);

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
    av.addFunc("defineParameter", &AvatarInstance::defineParameter);
    av.addFunc("getParameterDefault", &AvatarInstance::getParameterDefault);
    av.addFunc("getParameterMinimum", &AvatarInstance::getParameterMinimum);
    av.addFunc("getParameterMaximum", &AvatarInstance::getParameterMaximum);
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
    av.addFunc("getLayerRenderable", &AvatarInstance::getLayerRenderable);
    av.addFunc("defineExpression", &AvatarInstance::defineExpression);
    av.addFunc("removeExpression", &AvatarInstance::removeExpression);
    av.addFunc("getExpressionCount", &AvatarInstance::getExpressionCount);
    av.addFunc("getExpressionName", &AvatarInstance::getExpressionName);
    av.addFunc("applyExpression", &AvatarInstance::applyExpression);
    av.addFunc("transitionExpression", &AvatarInstance::transitionExpression);

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
    av.addFunc("bindAnimPlayer", &AvatarInstance::bindAnimPlayer);
    av.addFunc("bindAnimStateMachine", &AvatarInstance::bindAnimStateMachine);
    av.addFunc("bindAnimLayerMixer", &AvatarInstance::bindAnimLayerMixer);
    av.addFunc("bindAnimSkin", &AvatarInstance::bindAnimSkin);
    av.addFunc("registerMotion", &AvatarInstance::registerMotion);
    av.addFunc("unregisterMotion", &AvatarInstance::unregisterMotion);
    av.addFunc("getMotionCount", &AvatarInstance::getMotionCount);
    av.addFunc("getMotionName", &AvatarInstance::getMotionName);
    av.addFunc("getMotionClip", &AvatarInstance::getMotionClip);
    av.addFunc("setMotionBlendTime", &AvatarInstance::setMotionBlendTime);
    av.addFunc("setApplyRootMotion", &AvatarInstance::setApplyRootMotion);
    av.addFunc("getApplyRootMotion", &AvatarInstance::getApplyRootMotion);
    av.addFunc("getRootMotionDeltaX", &AvatarInstance::getRootMotionDeltaX);
    av.addFunc("getRootMotionDeltaZ", &AvatarInstance::getRootMotionDeltaZ);
    av.addFunc("getAnimationEventCount", &AvatarInstance::getAnimationEventCount);
    av.addFunc("getAnimationEventLayer", &AvatarInstance::getAnimationEventLayer);
    av.addFunc("getAnimationEventName", &AvatarInstance::getAnimationEventName);
    av.addFunc("getAnimationEventPayload", &AvatarInstance::getAnimationEventPayload);
    av.addFunc("mapHumanoidBone", &AvatarInstance::mapHumanoidBone);
    av.addFunc("autoMapHumanoidBones", &AvatarInstance::autoMapHumanoidBones);
    av.addFunc("getHumanoidBoneName", &AvatarInstance::getHumanoidBoneName);
    av.addFunc("mapViseme", &AvatarInstance::mapViseme);
    av.addFunc("setViseme", &AvatarInstance::setViseme);
    av.addFunc("setLookAtTarget", &AvatarInstance::setLookAtTarget);
    av.addFunc("setLookAtWeight", &AvatarInstance::setLookAtWeight);
    av.addFunc("clearLookAtTarget", &AvatarInstance::clearLookAtTarget);
    av.addFunc("attachToBone", &AvatarInstance::attachToBone);
    av.addFunc("detachAttachment", &AvatarInstance::detachAttachment);
    av.addFunc("getAttachmentCount", &AvatarInstance::getAttachmentCount);
    av.addFunc("linkSceneNode", &AvatarInstance::linkSceneNode);
    av.addFunc("isSceneLinked", &AvatarInstance::isSceneLinked);
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
    cls.addFunc("hasLive2DBackend", [](Avatar*) -> bool { return Avatar::hasLive2DBackend(); });
}

}  // namespace eve::avatar
