#include "avatar/AvatarInstance.h"
#include "animation/AnimClip.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/AnimStateMachine.h"
#include "animation/Tween.h"
#include "avatar/Avatar.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "model3d/ModelData.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <sstream>

namespace eve::avatar {
namespace {

std::string trimCopy(const std::string &s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool parseBoolish(const std::string &v, bool &out) {
    std::string t;
    t.reserve(v.size());
    for (char c : v) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (t == "1" || t == "true" || t == "on" || t == "yes") {
        out = true;
        return true;
    }
    if (t == "0" || t == "false" || t == "off" || t == "no") {
        out = false;
        return true;
    }
    return false;
}

}  // namespace

AvatarInstance::AvatarInstance(std::string kind) : kind_(std::move(kind)) {
    if (auto *mod = ModuleManager::getInstance<Avatar>("Avatar"))
        mod->registerInstance(this);
}

AvatarInstance::~AvatarInstance() { release(); }

void AvatarInstance::release() {
    if (released_) return;
    released_ = true;
    // Notify owners (e.g. Dialogue) before the object is destroyed so they can
    // drop dangling raw pointers. Runs exactly once: the destructor re-enters
    // release() but the guard above short-circuits.
    for (auto &[hookId, hook] : destroyHooks_) {
        try {
            hook(this);
        } catch (...) {
        }
    }
    destroyHooks_.clear();
    tween_ = nullptr;
    animPlayer_       = nullptr;
    animLayerMixer_   = nullptr;
    animStateMachine_ = nullptr;
    animSkin_         = nullptr;
    animSkeleton_     = nullptr;
    motions_.clear();
    attachments_.clear();
    skinnedPositions_.clear();
    if (sceneAnchor2d_) {
        ecs::DestroyEntity(sceneAnchor2d_);
        sceneAnchor2d_ = nullptr;
    }
    sceneLinked_ = false;
    if (auto *mod = ModuleManager::getInstance<Avatar>("Avatar"))
        mod->unregisterInstance(this);
    destroyLayers();
    destroyVroid();
    delete live2d_;
    live2d_ = nullptr;
}

size_t AvatarInstance::addDestroyHook(DestroyHook hook) {
    if (!hook)
        return static_cast<size_t>(-1);
    const size_t id = nextDestroyHookId_++;
    destroyHooks_.emplace_back(id, std::move(hook));
    return id;
}

void AvatarInstance::removeDestroyHook(size_t id) {
    for (auto it = destroyHooks_.begin(); it != destroyHooks_.end(); ++it) {
        if (it->first == id) {
            destroyHooks_.erase(it);
            return;
        }
    }
}

void AvatarInstance::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

void AvatarInstance::setScale(float sx, float sy) {
    sx_ = sx;
    sy_ = sy;
}

void AvatarInstance::setVisible(bool visible) { visible_ = visible; }

void AvatarInstance::setLayer(int layer) { layer_ = layer; }

void AvatarInstance::ensureParameter(const std::string &name, float value) {
    if (name.empty()) return;
    if (parameters_.find(name) == parameters_.end()) parameterOrder_.push_back(name);
    parameters_[name] = value;
}

void AvatarInstance::setExpression(const std::string &name) {
    expression_ = name;
    if (kind_ == "image") {
        applyExpression(name);
    } else if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->setExpression(name);
    } else if (kind_ == "vroid") {
        if (expressionDefs_.count(name)) {
            applyExpression(name);
        } else if (!name.empty()) {
            // Solo morph: zero known morph params then set named weight to 1.
            if (boundMesh_ && boundMesh_->hasMorphData()) {
                for (int i = 0; i < boundMesh_->getMorphCount(); ++i) {
                    const std::string mn = boundMesh_->getMorphName(i);
                    ensureParameter(mn, 0.f);
                }
            }
            ensureParameter(name, 1.f);
            syncMorphWeightsToMesh();
        }
    }
}

void AvatarInstance::setMotion(const std::string &name) {
    motion_ = name;
    if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->setMotion(name);
    }
    if (kind_ == "vroid") {
        auto it = motions_.find(name);
        if (animPlayer_ && it != motions_.end()) {
            if (animPlayer_->isPlaying() && motionBlendTime_ > 0.f)
                animPlayer_->crossFade(it->second, motionBlendTime_);
            else
                animPlayer_->play(it->second);
            hasPreviousRoot_ = false;
        }
        if (animStateMachine_) animStateMachine_->setTrigger(name);
        setParameter("motion:" + name, 1.f);
    }
}

void AvatarInstance::setParameter(const std::string &name, float value) {
    if (name.empty()) return;
    ensureParameter(name, value);
    if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->setParameter(name, value);
    } else if (kind_ == "vroid") {
        if (boundMesh_ && boundMesh_->hasMorph(name)) boundMesh_->setMorphWeight(name, value);
    } else if (kind_ == "image") {
        // Parameter name matching a layer drives that layer's alpha (lip-sync etc.).
        if (Layer *L = findLayer(name)) {
            float a = value;
            if (a < 0.f) a = 0.f;
            if (a > 1.f) a = 1.f;
            L->entity->sprite()->a       = a;
            L->visible = a > 0.001f;
            L->entity->sprite()->visible = visible_ && L->visible;
        }
    }
}

float AvatarInstance::getParameter(const std::string &name) const {
    if (kind_ == "live2d" && live2d_) return live2d_->getParameter(name);
    auto it = parameters_.find(name);
    return it == parameters_.end() ? 0.f : it->second;
}

bool AvatarInstance::hasParameter(const std::string &name) const {
    if (kind_ == "live2d" && live2d_) {
        // Backend may not expose has; fall through to local cache.
    }
    return parameters_.find(name) != parameters_.end();
}

int AvatarInstance::getParameterCount() const { return int(parameterOrder_.size()); }

std::string AvatarInstance::getParameterName(int index) const {
    if (index < 0 || size_t(index) >= parameterOrder_.size()) return {};
    return parameterOrder_[size_t(index)];
}

void AvatarInstance::update(float dt) {
    applyTweenTracks();
    updateExpressionTransition(dt);
    if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->update(dt);
    }
    if (kind_ == "vroid") updateSkeletalAnimation(dt);
}

bool AvatarInstance::bindAnimPlayer(animation::AnimPlayer* player) {
    if (kind_ != "vroid") return false;
    animPlayer_       = player;
    animStateMachine_ = nullptr;
    animLayerMixer_   = nullptr;
    animSkeleton_     = player ? player->getSkeleton() : nullptr;
    hasPreviousRoot_  = false;
    return player != nullptr;
}

bool AvatarInstance::bindAnimStateMachine(animation::AnimStateMachine* machine) {
    if (kind_ != "vroid") return false;
    animStateMachine_ = machine;
    animPlayer_       = nullptr;
    animLayerMixer_   = nullptr;
    animSkeleton_     = machine ? machine->getSkeleton() : nullptr;
    hasPreviousRoot_  = false;
    return machine != nullptr;
}

bool AvatarInstance::bindAnimLayerMixer(animation::AnimLayerMixer* mixer) {
    if (kind_ != "vroid") return false;
    animLayerMixer_   = mixer;
    animStateMachine_ = nullptr;
    animPlayer_       = mixer ? mixer->getBasePlayer() : nullptr;
    animSkeleton_     = mixer ? mixer->getSkeleton() : nullptr;
    hasPreviousRoot_  = false;
    return mixer != nullptr;
}

bool AvatarInstance::bindAnimSkin(animation::AnimSkin* skin) {
    if (kind_ != "vroid") return false;
    animSkin_ = skin;
    skinnedPositions_.clear();
    return skin != nullptr;
}

bool AvatarInstance::registerMotion(const std::string& name, animation::AnimClip* clip) {
    if (kind_ != "vroid" || name.empty() || !clip) return false;
    motions_[name] = clip;
    return true;
}

void AvatarInstance::setMotionBlendTime(float seconds) { motionBlendTime_ = std::max(0.f, seconds); }

void AvatarInstance::setApplyRootMotion(bool enabled) {
    applyRootMotion_ = enabled;
    hasPreviousRoot_ = false;
}

bool AvatarInstance::mapHumanoidBone(const std::string& semantic, const std::string& boneName) {
    if (kind_ != "vroid" || !animSkeleton_ || semantic.empty() || boneName.empty()) return false;
    if (animSkeleton_->findBone(boneName) < 0) return false;
    humanoidBones_[semantic] = boneName;
    return true;
}

int AvatarInstance::autoMapHumanoidBones() {
    if (kind_ != "vroid" || !animSkeleton_) return 0;
    static const std::pair<const char*, std::vector<const char*>> aliases[] = {
        {"hips", {"hips", "Hips", "pelvis", "Pelvis"}},
        {"spine", {"spine", "Spine"}},
        {"chest", {"chest", "Chest", "spine1", "Spine1"}},
        {"upperChest", {"upperChest", "UpperChest", "spine2", "Spine2"}},
        {"neck", {"neck", "Neck"}},
        {"head", {"head", "Head"}},
        {"leftUpperArm", {"leftUpperArm", "LeftUpperArm", "upper_arm.L"}},
        {"leftLowerArm", {"leftLowerArm", "LeftLowerArm", "lower_arm.L"}},
        {"leftHand", {"leftHand", "LeftHand", "hand.L"}},
        {"rightUpperArm", {"rightUpperArm", "RightUpperArm", "upper_arm.R"}},
        {"rightLowerArm", {"rightLowerArm", "RightLowerArm", "lower_arm.R"}},
        {"rightHand", {"rightHand", "RightHand", "hand.R"}},
        {"leftUpperLeg", {"leftUpperLeg", "LeftUpperLeg", "thigh.L"}},
        {"leftLowerLeg", {"leftLowerLeg", "LeftLowerLeg", "shin.L"}},
        {"leftFoot", {"leftFoot", "LeftFoot", "foot.L"}},
        {"rightUpperLeg", {"rightUpperLeg", "RightUpperLeg", "thigh.R"}},
        {"rightLowerLeg", {"rightLowerLeg", "RightLowerLeg", "shin.R"}},
        {"rightFoot", {"rightFoot", "RightFoot", "foot.R"}},
    };
    int mapped = 0;
    for (const auto& [semantic, names] : aliases) {
        if (humanoidBones_.contains(semantic)) continue;
        for (const char* name : names) {
            if (animSkeleton_->findBone(name) < 0) continue;
            humanoidBones_[semantic] = name;
            ++mapped;
            break;
        }
    }
    return mapped;
}

std::string AvatarInstance::getHumanoidBoneName(const std::string& semantic) const {
    const auto it = humanoidBones_.find(semantic);
    return it == humanoidBones_.end() ? std::string{} : it->second;
}

bool AvatarInstance::mapViseme(const std::string& viseme, const std::string& morphName) {
    if (kind_ != "vroid" || !boundMesh_ || viseme.empty() || !boundMesh_->hasMorph(morphName)) return false;
    visemeMorphs_[viseme] = morphName;
    ensureParameter(morphName);
    return true;
}

bool AvatarInstance::setViseme(const std::string& viseme, float weight) {
    const auto it = visemeMorphs_.find(viseme);
    if (it == visemeMorphs_.end()) return false;
    if (!activeViseme_.empty() && activeViseme_ != viseme) {
        const auto previous = visemeMorphs_.find(activeViseme_);
        if (previous != visemeMorphs_.end()) setParameter(previous->second, 0.f);
    }
    setParameter(it->second, std::clamp(weight, 0.f, 1.f));
    activeViseme_ = viseme;
    return true;
}

bool AvatarInstance::setLookAtTarget(float x, float y, float z) {
    if (kind_ != "vroid" || !animSkeleton_ || !humanoidBones_.contains("head")) return false;
    lookAtX_       = x;
    lookAtY_       = y;
    lookAtZ_       = z;
    lookAtEnabled_ = true;
    return true;
}

void AvatarInstance::setLookAtWeight(float weight) { lookAtWeight_ = std::clamp(weight, 0.f, 1.f); }

void AvatarInstance::clearLookAtTarget() { lookAtEnabled_ = false; }

bool AvatarInstance::attachToBone(const std::string& name, const std::string& boneSemanticOrName,
                                  graphics::Renderable3D* renderable, float ox, float oy, float oz) {
    if (kind_ != "vroid" || name.empty() || !animSkeleton_ || !renderable) return false;
    std::string boneName = boneSemanticOrName;
    const auto  mapped   = humanoidBones_.find(boneSemanticOrName);
    if (mapped != humanoidBones_.end()) boneName = mapped->second;
    if (animSkeleton_->findBone(boneName) < 0) return false;
    for (auto& attachment : attachments_) {
        if (attachment.name != name) continue;
        attachment = {name, boneName, renderable, ox, oy, oz};
        return true;
    }
    attachments_.push_back({name, boneName, renderable, ox, oy, oz});
    return true;
}

bool AvatarInstance::detachAttachment(const std::string& name) {
    const auto it = std::find_if(attachments_.begin(), attachments_.end(),
                                 [&](const Attachment& item) { return item.name == name; });
    if (it == attachments_.end()) return false;
    attachments_.erase(it);
    return true;
}

animation::AnimPose* AvatarInstance::updateSkeletalAnimation(float dt) {
    animation::AnimPose* pose = nullptr;
    rootMotionLoopCount_      = 0;
    if (animLayerMixer_) {
        const float          oldTime = animPlayer_ ? animPlayer_->getTime() : 0.f;
        animation::AnimClip* oldClip = animPlayer_ ? animPlayer_->getClip() : nullptr;
        animLayerMixer_->update(dt);
        pose = animLayerMixer_->getPose();
        if (animPlayer_ && oldClip && oldClip == animPlayer_->getClip() && animPlayer_->getLoop() &&
            oldClip->getDuration() > 0.f) {
            const float duration = oldClip->getDuration();
            rootMotionLoopCount_ =
                static_cast<int>(std::floor(animPlayer_->getTime() / duration) - std::floor(oldTime / duration));
        }
    } else if (animStateMachine_) {
        animStateMachine_->update(dt);
        pose = animStateMachine_->getPose();
    } else if (animPlayer_) {
        const float          oldTime = animPlayer_->getTime();
        animation::AnimClip* oldClip = animPlayer_->getClip();
        animPlayer_->update(dt);
        pose = animPlayer_->getPose();
        if (oldClip && oldClip == animPlayer_->getClip() && animPlayer_->getLoop() && oldClip->getDuration() > 0.f) {
            const float duration = oldClip->getDuration();
            rootMotionLoopCount_ =
                static_cast<int>(std::floor(animPlayer_->getTime() / duration) - std::floor(oldTime / duration));
        }
    }
    if (!pose || !animSkeleton_) return nullptr;
    pose->computeWorld(animSkeleton_);
    applyLookAt(pose);
    updateRootMotion(pose);
    updateSkin(pose);
    updateAttachments(pose);
    return pose;
}

int AvatarInstance::getAnimationEventCount() const {
    if (animLayerMixer_) return animLayerMixer_->getEventCount();
    if (animPlayer_) return animPlayer_->getEventCount();
    return 0;
}

std::string AvatarInstance::getAnimationEventLayer(int index) const {
    if (animLayerMixer_) return animLayerMixer_->getEventLayer(index);
    return animPlayer_ && index >= 0 && index < animPlayer_->getEventCount() ? "base" : std::string{};
}

std::string AvatarInstance::getAnimationEventName(int index) const {
    if (animLayerMixer_) return animLayerMixer_->getEventName(index);
    return animPlayer_ ? animPlayer_->getEventName(index) : std::string{};
}

std::string AvatarInstance::getAnimationEventPayload(int index) const {
    if (animLayerMixer_) return animLayerMixer_->getEventPayload(index);
    return animPlayer_ ? animPlayer_->getEventPayload(index) : std::string{};
}

void AvatarInstance::updateRootMotion(animation::AnimPose* pose) {
    rootMotionDeltaX_ = 0.f;
    rootMotionDeltaZ_ = 0.f;
    if (!pose || pose->getBoneCount() <= 0) return;
    const float rootX = pose->getWorldPositionX(0);
    const float rootZ = pose->getWorldPositionZ(0);
    if (hasPreviousRoot_) {
        rootMotionDeltaX_ = rootX - previousRootX_;
        rootMotionDeltaZ_ = rootZ - previousRootZ_;
        if (rootMotionLoopCount_ > 0 && animPlayer_ && animPlayer_->getClip()) {
            animation::AnimClip* clip     = animPlayer_->getClip();
            const int            keyCount = clip->getPositionKeyCount(0);
            if (keyCount > 1) {
                const float cycleX = clip->getPositionKeyX(0, keyCount - 1) - clip->getPositionKeyX(0, 0);
                const float cycleZ = clip->getPositionKeyZ(0, keyCount - 1) - clip->getPositionKeyZ(0, 0);
                rootMotionDeltaX_ += cycleX * static_cast<float>(rootMotionLoopCount_);
                rootMotionDeltaZ_ += cycleZ * static_cast<float>(rootMotionLoopCount_);
            }
        }
        if (applyRootMotion_) {
            x3_ += rootMotionDeltaX_;
            z3_ += rootMotionDeltaZ_;
        }
    }
    previousRootX_   = rootX;
    previousRootZ_   = rootZ;
    hasPreviousRoot_ = true;
}

void AvatarInstance::updateSkin(animation::AnimPose* pose) {
    if (!pose || !animSkin_ || !boundMesh_) return;
    if (!animSkin_->skinPositionsTo(pose, skinnedPositions_)) return;
    auto* gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!gfx) return;
    gfx->updateMeshVertices(boundMesh_, skinnedPositions_.data(), nullptr, nullptr, animSkin_->getVertexCount(),
                            nullptr, 0);
}

void AvatarInstance::updateAttachments(animation::AnimPose* pose) {
    if (!pose || !animSkeleton_) return;
    const float sinYaw = std::sin(yaw_);
    const float cosYaw = std::cos(yaw_);
    for (auto& attachment : attachments_) {
        if (!attachment.renderable) continue;
        const int bone = animSkeleton_->findBone(attachment.boneName);
        if (bone < 0 || bone >= pose->getBoneCount()) continue;
        const animation::TransformTRS& world      = pose->world(bone);
        const animation::Mat4          boneMatrix = animation::Mat4::fromTRS(world);
        float                          bx = 0.f, by = 0.f, bz = 0.f;
        boneMatrix.transformPoint(attachment.ox, attachment.oy, attachment.oz, bx, by, bz);
        const float scaledX = bx * sx3_;
        const float scaledZ = bz * sz3_;
        attachment.renderable->setPosition(x3_ + scaledX * cosYaw + scaledZ * sinYaw, y3_ + by * sy3_,
                                           z3_ - scaledX * sinYaw + scaledZ * cosYaw);

        const float sinPitch  = 2.f * (world.qw * world.qx - world.qy * world.qz);
        const float bonePitch = std::asin(std::clamp(sinPitch, -1.f, 1.f));
        const float boneYaw   = std::atan2(2.f * (world.qw * world.qy + world.qx * world.qz),
                                           1.f - 2.f * (world.qx * world.qx + world.qy * world.qy));
        const float boneRoll  = std::atan2(2.f * (world.qw * world.qz + world.qx * world.qy),
                                           1.f - 2.f * (world.qx * world.qx + world.qz * world.qz));
        attachment.renderable->setRotation(yaw_ + boneYaw, pitch_ + bonePitch, roll_ + boneRoll);
    }
}

void AvatarInstance::applyLookAt(animation::AnimPose* pose) {
    if (!pose || !animSkeleton_) return;
    const auto mapped = humanoidBones_.find("head");
    if (mapped == humanoidBones_.end()) return;
    const int head = animSkeleton_->findBone(mapped->second);
    if (head < 0 || head >= pose->getBoneCount()) return;

    animation::TransformTRS& local = pose->local(head);
    if (lookAtApplied_) {
        const float similarity = std::fabs(local.qx * lookAtPostQx_ + local.qy * lookAtPostQy_ +
                                           local.qz * lookAtPostQz_ + local.qw * lookAtPostQw_);
        if (similarity > 0.9999f) {
            local.qx = lookAtBaseQx_;
            local.qy = lookAtBaseQy_;
            local.qz = lookAtBaseQz_;
            local.qw = lookAtBaseQw_;
        }
        lookAtApplied_ = false;
        pose->computeWorld(animSkeleton_);
    }
    if (!lookAtEnabled_ || lookAtWeight_ <= 0.f) return;

    const animation::TransformTRS& headWorld  = pose->world(head);
    const float                    dx         = lookAtX_ - (x3_ + headWorld.px * sx3_);
    const float                    dy         = lookAtY_ - (y3_ + headWorld.py * sy3_);
    const float                    dz         = lookAtZ_ - (z3_ + headWorld.pz * sz3_);
    const float                    cosYaw     = std::cos(yaw_);
    const float                    sinYaw     = std::sin(yaw_);
    const float                    localX     = dx * cosYaw - dz * sinYaw;
    const float                    localZ     = dx * sinYaw + dz * cosYaw;
    const float                    horizontal = std::sqrt(localX * localX + localZ * localZ);
    if (horizontal < 1e-6f && std::fabs(dy) < 1e-6f) return;

    constexpr float kMaxYaw      = 1.04719755f;
    constexpr float kMaxPitch    = 0.61086524f;
    const float     targetYaw    = std::clamp(std::atan2(localX, localZ), -kMaxYaw, kMaxYaw) * lookAtWeight_;
    const float     targetPitch  = std::clamp(-std::atan2(dy, horizontal), -kMaxPitch, kMaxPitch) * lookAtWeight_;
    const float     sinHalfYaw   = std::sin(targetYaw * 0.5f);
    const float     cosHalfYaw   = std::cos(targetYaw * 0.5f);
    const float     sinHalfPitch = std::sin(targetPitch * 0.5f);
    const float     cosHalfPitch = std::cos(targetPitch * 0.5f);
    const float     deltaX       = cosHalfYaw * sinHalfPitch;
    const float     deltaY       = sinHalfYaw * cosHalfPitch;
    const float     deltaZ       = -sinHalfYaw * sinHalfPitch;
    const float     deltaW       = cosHalfYaw * cosHalfPitch;

    lookAtBaseQx_  = local.qx;
    lookAtBaseQy_  = local.qy;
    lookAtBaseQz_  = local.qz;
    lookAtBaseQw_  = local.qw;
    const float qx = local.qw * deltaX + local.qx * deltaW + local.qy * deltaZ - local.qz * deltaY;
    const float qy = local.qw * deltaY - local.qx * deltaZ + local.qy * deltaW + local.qz * deltaX;
    const float qz = local.qw * deltaZ + local.qx * deltaY - local.qy * deltaX + local.qz * deltaW;
    const float qw = local.qw * deltaW - local.qx * deltaX - local.qy * deltaY - local.qz * deltaZ;
    local.qx       = qx;
    local.qy       = qy;
    local.qz       = qz;
    local.qw       = qw;
    local.normalizeRotation();
    lookAtPostQx_  = local.qx;
    lookAtPostQy_  = local.qy;
    lookAtPostQz_  = local.qz;
    lookAtPostQw_  = local.qw;
    lookAtApplied_ = true;
    pose->computeWorld(animSkeleton_);
}

bool AvatarInstance::linkSceneNode(scene::Scene* scene, const std::string& nodeId) {
    if (!scene || nodeId.empty()) return false;
    if (kind_ == "vroid") {
        if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
        sceneLinked_ = scene->linkRenderable3D(nodeId, renderable3d_);
        return sceneLinked_;
    }
    if (kind_ == "image") {
        if (!sceneAnchor2d_) {
            sceneAnchor2d_ = graphics::Renderable2D::create();
            sceneAnchor2d_->setSize(0.f, 0.f);
            sceneAnchor2d_->setColor(1.f, 1.f, 1.f, 0.f);
            sceneAnchor2d_->setReceiveLight(false);
            sceneAnchor2d_->setCastOcclusion(false);
        }
        sceneLinked_ = scene->linkRenderable2D(nodeId, sceneAnchor2d_);
        return sceneLinked_;
    }
    return false;
}

void AvatarInstance::sync() {
    if (kind_ == "image")
        syncImageLayers();
    else if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) {
            live2d_->setTransform(x_, y_, sx_, sy_);
            live2d_->setVisible(visible_);
            live2d_->setLayer(layer_);
        }
    } else if (kind_ == "vroid")
        syncVroid();
}

void AvatarInstance::bindTween(animation::Tween *tween) { tween_ = tween; }

void AvatarInstance::unbindTween() { tween_ = nullptr; }

void AvatarInstance::applyTweenTracks() {
    if (!tween_ || !tween_->isActive()) return;
    if (tween_->has("x")) x_ = tween_->get("x");
    if (tween_->has("y")) y_ = tween_->get("y");
    if (tween_->has("sx")) sx_ = tween_->get("sx");
    if (tween_->has("sy")) sy_ = tween_->get("sy");
    if (kind_ == "vroid") {
        if (tween_->has("x3")) x3_ = tween_->get("x3");
        if (tween_->has("y3")) y3_ = tween_->get("y3");
        if (tween_->has("z3")) z3_ = tween_->get("z3");
        if (tween_->has("yaw")) yaw_ = tween_->get("yaw");
    }
    const int n = tween_->getPropertyCount();
    for (int i = 0; i < n; ++i) {
        const std::string name = tween_->getPropertyName(i);
        if (name == "x" || name == "y" || name == "sx" || name == "sy" || name == "x3" ||
            name == "y3" || name == "z3" || name == "yaw")
            continue;
        setParameter(name, tween_->get(name));
    }
}

// ---- image ----

AvatarInstance::Layer *AvatarInstance::findLayer(const std::string &name) {
    for (auto &L : layers_)
        if (L.name == name) return &L;
    return nullptr;
}

const AvatarInstance::Layer *AvatarInstance::findLayer(const std::string &name) const {
    for (const auto &L : layers_)
        if (L.name == name) return &L;
    return nullptr;
}

bool AvatarInstance::addLayer(const std::string &name, graphics::Texture *texture, int zIndex) {
    if (kind_ != "image" || name.empty() || findLayer(name)) return false;
    Layer L;
    L.name = name;
    L.zIndex = zIndex;
    L.entity    = graphics::Renderable2D::create();
    auto sp     = L.entity->sprite();
    sp->texture = texture;
    // Layered portraits are UI-style artwork and retain their authored colors
    // when the scene has no Camera2D ambient light configured.
    sp->receiveLight = false;
    if (texture) {
        sp->width  = float(texture->getWidth());
        sp->height = float(texture->getHeight());
    }
    layers_.push_back(L);
    return true;
}

bool AvatarInstance::setLayerTexture(const std::string &name, graphics::Texture *texture) {
    Layer *L = findLayer(name);
    if (!L) return false;
    auto sp     = L->entity->sprite();
    sp->texture = texture;
    if (texture && L->autoSize) {
        sp->width  = float(texture->getWidth());
        sp->height = float(texture->getHeight());
    }
    return true;
}

bool AvatarInstance::setLayerVisible(const std::string &name, bool visible) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->visible = visible;
    L->entity->sprite()->visible = visible_ && visible;
    return true;
}

bool AvatarInstance::setLayerOffset(const std::string &name, float ox, float oy) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->ox = ox;
    L->oy = oy;
    return true;
}

bool AvatarInstance::setLayerColor(const std::string &name, float r, float g, float b, float a) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->entity->setColor(r, g, b, a);
    return true;
}

bool AvatarInstance::setLayerZ(const std::string &name, int zIndex) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->zIndex = zIndex;
    return true;
}

bool AvatarInstance::setLayerSize(const std::string &name, float w, float h) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->autoSize = false;
    L->entity->setSize(w, h);
    return true;
}

int AvatarInstance::getLayerCount() const { return int(layers_.size()); }

std::string AvatarInstance::getLayerName(int index) const {
    if (index < 0 || size_t(index) >= layers_.size()) return {};
    return layers_[size_t(index)].name;
}

bool AvatarInstance::hasLayer(const std::string &name) const { return findLayer(name) != nullptr; }

graphics::Renderable2D* AvatarInstance::getLayerRenderable(const std::string& name) {
    Layer* L = findLayer(name);
    return L ? L->entity : nullptr;
}

bool AvatarInstance::defineExpression(const std::string &name, const std::string &spec) {
    if ((kind_ != "image" && kind_ != "vroid") || name.empty()) return false;
    expressionDefs_[name] = spec;
    return true;
}

bool AvatarInstance::applyExpressionSpec(const std::string &spec) {
    if (spec.empty()) return true;
    std::stringstream ss(spec);
    std::string part;
    while (std::getline(ss, part, ';')) {
        part = trimCopy(part);
        if (part.empty()) continue;
        const auto eq = part.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trimCopy(part.substr(0, eq));
        const std::string val = trimCopy(part.substr(eq + 1));
        if (key.empty()) continue;

        if (kind_ == "vroid") {
            bool flag = false;
            if (parseBoolish(val, flag)) {
                setParameter(key, flag ? 1.f : 0.f);
            } else {
                char *end = nullptr;
                const float f = std::strtof(val.c_str(), &end);
                if (end && end != val.c_str())
                    setParameter(key, f);
                else
                    setParameter(key, 1.f);
            }
            continue;
        }

        Layer *L = findLayer(key);
        if (!L) {
            // Unknown layer name still recorded as parameter for tooling.
            setParameter(key, 1.f);
            continue;
        }
        bool flag = false;
        if (parseBoolish(val, flag)) {
            L->visible = flag;
            L->entity->sprite()->visible = visible_ && flag;
        } else {
            // Non-bool value: show layer and stash as parameter (texture swap by name).
            L->visible = true;
            L->entity->sprite()->visible = visible_;
            setParameter(key, 1.f);
            setParameter(key + ".variant", float(std::hash<std::string>{}(val) & 0xffff));
        }
    }
    return true;
}

bool AvatarInstance::applyExpression(const std::string &name) {
    auto it = expressionDefs_.find(name);
    if (it == expressionDefs_.end()) return false;
    expressionBlendFrom_.clear();
    expressionBlendTo_.clear();
    expressionBlendDuration_ = 0.f;
    expression_ = name;
    const bool ok = applyExpressionSpec(it->second);
    if (kind_ == "vroid") syncMorphWeightsToMesh();
    return ok;
}

bool AvatarInstance::transitionExpression(const std::string& name, float duration) {
    const auto definition = expressionDefs_.find(name);
    if (definition == expressionDefs_.end()) return false;
    if (duration <= 0.f) return applyExpression(name);

    std::unordered_map<std::string, float> targets;
    if (kind_ == "vroid" && boundMesh_ && boundMesh_->hasMorphData()) {
        for (int i = 0; i < boundMesh_->getMorphCount(); ++i) targets[boundMesh_->getMorphName(i)] = 0.f;
    }
    std::stringstream stream(definition->second);
    std::string       part;
    while (std::getline(stream, part, ';')) {
        part          = trimCopy(part);
        const auto eq = part.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = trimCopy(part.substr(0, eq));
        const std::string value = trimCopy(part.substr(eq + 1));
        if (key.empty()) continue;
        bool flag = false;
        if (parseBoolish(value, flag)) {
            targets[key] = flag ? 1.f : 0.f;
            continue;
        }
        char*       end     = nullptr;
        const float numeric = std::strtof(value.c_str(), &end);
        if (!end || end == value.c_str() || *end != '\0') return applyExpression(name);
        targets[key] = numeric;
    }
    if (targets.empty()) return applyExpression(name);

    expressionBlendFrom_.clear();
    expressionBlendTo_ = std::move(targets);
    for (const auto& [key, target] : expressionBlendTo_) {
        (void)target;
        if (kind_ == "image") {
            const Layer* layer        = findLayer(key);
            expressionBlendFrom_[key] = layer ? layer->entity->sprite()->a : getParameter(key);
        } else {
            expressionBlendFrom_[key] = getParameter(key);
        }
    }
    expression_              = name;
    expressionBlendElapsed_  = 0.f;
    expressionBlendDuration_ = duration;
    return true;
}

void AvatarInstance::updateExpressionTransition(float dt) {
    if (expressionBlendDuration_ <= 0.f || expressionBlendTo_.empty()) return;
    expressionBlendElapsed_ += std::max(0.f, dt);
    const float t = std::clamp(expressionBlendElapsed_ / expressionBlendDuration_, 0.f, 1.f);
    for (const auto& [key, target] : expressionBlendTo_) {
        const auto  start = expressionBlendFrom_.find(key);
        const float from  = start == expressionBlendFrom_.end() ? getParameter(key) : start->second;
        setParameter(key, from + (target - from) * t);
    }
    if (t >= 1.f) {
        expressionBlendFrom_.clear();
        expressionBlendTo_.clear();
        expressionBlendDuration_ = 0.f;
    }
}

void AvatarInstance::syncImageLayers() {
    float parentX = 0.f, parentY = 0.f, parentRot = 0.f, parentSx = 1.f, parentSy = 1.f;
    bool  parentVisible = true;
    if (sceneLinked_ && sceneAnchor2d_) {
        auto anchorTf = sceneAnchor2d_->transform();
        parentX       = anchorTf->x;
        parentY       = anchorTf->y;
        parentRot     = anchorTf->rot;
        parentSx      = anchorTf->sx;
        parentSy      = anchorTf->sy;
        parentVisible = sceneAnchor2d_->sprite()->visible;
    }
    const float parentRadians = parentRot * 3.14159265358979323846f / 180.f;
    const float c             = std::cos(parentRadians);
    const float s             = std::sin(parentRadians);
    for (Layer &L : layers_) {
        if (!L.entity) L.entity = graphics::Renderable2D::create();
        auto tf = L.entity->transform();
        auto sp = L.entity->sprite();
        const float localX      = (x_ + L.ox * sx_) * parentSx;
        const float localY      = (y_ + L.oy * sy_) * parentSy;
        tf->x                   = parentX + c * localX - s * localY;
        tf->y                   = parentY + s * localX + c * localY;
        tf->rot                 = tf->rot - L.appliedParentRotation + parentRot;
        L.appliedParentRotation = parentRot;
        tf->sx                  = sx_ * parentSx;
        tf->sy                  = sy_ * parentSy;
        sp->layer   = layer_ + L.zIndex;
        sp->visible = parentVisible && visible_ && L.visible;
    }
}

void AvatarInstance::destroyLayers() {
    for (Layer &L : layers_) {
        if (L.entity) {
            ecs::DestroyEntity(L.entity);
            L.entity = nullptr;
        }
    }
    layers_.clear();
}

// ---- live2d ----

bool AvatarInstance::loadLive2DModel(const std::string &path) {
    if (kind_ != "live2d") return false;
    if (!live2d_) live2d_ = Avatar::createLive2DBackend();
    if (!live2d_) return false;
    return live2d_->loadModel(path);
}

std::string AvatarInstance::getLive2DBackendName() const {
    if (live2d_) return live2d_->getName();
    return Avatar::getLive2DBackendName();
}

bool AvatarInstance::hasLive2DBackend() const {
    if (live2d_) return live2d_->isRuntimeAvailable();
    return Avatar::hasLive2DBackend();
}

void AvatarInstance::collectLive2DDrawItems(std::vector<graphics::DrawItem2D>& out) {
    if (kind_ != "live2d" || !visible_) return;
    if (!live2d_) live2d_ = Avatar::createLive2DBackend();
    if (live2d_) live2d_->collectDrawItems(out);
}

// ---- vroid ----

bool AvatarInstance::loadVroidModelPath(const std::string &path) {
    if (kind_ != "vroid") return false;
    vroidPath_ = path;
    return !path.empty();
}

bool AvatarInstance::bindVroidModelData(model3d::ModelData *data) {
    if (kind_ != "vroid") return false;
    vroidData_ = data;
    if (data) loadMorphNamesFromModel(0);
    return data != nullptr;
}

int AvatarInstance::loadMorphNamesFromModel(int meshIndex) {
    if (kind_ != "vroid" || !vroidData_) return 0;
    const int n = vroidData_->getMorphTargetCount(meshIndex);
    int added = 0;
    for (int i = 0; i < n; ++i) {
        const std::string name = vroidData_->getMorphTargetName(meshIndex, i);
        if (name.empty()) continue;
        if (!hasParameter(name)) {
            ensureParameter(name, 0.f);
            ++added;
        }
    }
    return added;
}

void AvatarInstance::setMesh(graphics::Mesh *mesh) {
    if (kind_ != "vroid") return;
    boundMesh_ = mesh;
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    renderable3d_->setMesh(mesh);
    if (mesh && mesh->hasMorphData()) {
        for (int i = 0; i < mesh->getMorphCount(); ++i) {
            const std::string name = mesh->getMorphName(i);
            ensureParameter(name, mesh->getMorphWeight(name));
        }
    }
}

void AvatarInstance::setTexture(graphics::Texture *texture) {
    if (kind_ != "vroid") return;
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    renderable3d_->setTexture(texture);
}

void AvatarInstance::setPosition3D(float x, float y, float z) {
    x3_ = x;
    y3_ = y;
    z3_ = z;
}

void AvatarInstance::setRotation3D(float yaw, float pitch, float roll) {
    yaw_ = yaw;
    pitch_ = pitch;
    roll_ = roll;
}

void AvatarInstance::setScale3D(float sx, float sy, float sz) {
    sx3_ = sx;
    sy3_ = sy;
    sz3_ = sz;
}

graphics::Mesh *AvatarInstance::getBoundMesh() const { return boundMesh_; }

void AvatarInstance::syncMorphWeightsToMesh() {
    if (!boundMesh_ || !boundMesh_->hasMorphData()) return;
    for (int i = 0; i < boundMesh_->getMorphCount(); ++i) {
        const std::string name = boundMesh_->getMorphName(i);
        boundMesh_->setMorphWeight(name, getParameter(name));
    }
}

bool AvatarInstance::bakeMorphs() {
    syncMorphWeightsToMesh();
    if (!boundMesh_ || !boundMesh_->isMorphDirty()) return false;
    auto *gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!gfx) return false;
    return gfx->bakeMeshMorph(boundMesh_);
}

void AvatarInstance::syncVroid() {
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    if (!sceneLinked_) {
        renderable3d_->setPosition(x3_, y3_, z3_);
        renderable3d_->setRotation(yaw_, pitch_, roll_);
        renderable3d_->setScale(sx3_ * sx_, sy3_ * sy_, sz3_);
        renderable3d_->setVisible(visible_);
    }
    bakeMorphs();
}

void AvatarInstance::destroyVroid() {
    if (renderable3d_) {
        ecs::DestroyEntity(renderable3d_);
        renderable3d_ = nullptr;
    }
    boundMesh_ = nullptr;
    vroidData_ = nullptr;
    vroidPath_.clear();
}

}  // namespace eve::avatar
