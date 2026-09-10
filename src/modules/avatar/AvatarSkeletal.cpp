#include <glm/gtc/matrix_transform.hpp>
#include "animation/AnimClip.h"
#include "animation/AnimConstraintStack.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/AnimStateMachine.h"
#include "animation/DynamicBoneSolver.h"
#include "animation/FootIKSolver.h"
#include "animation/Tween.h"
#include "avatar/Avatar.h"
#include "avatar/AvatarInstance.h"
#include "avatar/VrmRuntime.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "inventory/Equipment.h"
#include "model3d/ModelData.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <sstream>

namespace eve::avatar {
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
    if (vrm_ && hasParameter(viseme)) {
        if (!activeViseme_.empty()) setParameter(activeViseme_, 0);
        setParameter(viseme, std::clamp(weight, 0.f, 1.f));
        activeViseme_ = viseme;
        return true;
    }
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

eve::Result<eve::AttachmentPoint> AvatarInstance::sampleAttachmentPoint(std::string_view     name,
                                                                        eve::AttachmentPoint localOffset) const {
    if (!animSkeleton_ || attachmentPoseMatrices_.empty())
        return eve::Result<eve::AttachmentPoint>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "Avatar has no evaluated skeletal pose",
                                   "avatar.attachment.pose"));
    std::string boneName(name);
    const auto  semantic = humanoidBones_.find(boneName);
    if (semantic != humanoidBones_.end()) boneName = semantic->second;
    const int bone = animSkeleton_->findBone(boneName);
    if (bone < 0 || static_cast<std::size_t>(bone) >= attachmentPoseMatrices_.size())
        return eve::Result<eve::AttachmentPoint>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "Avatar attachment bone was not found", boneName));
    const auto& matrix = attachmentPoseMatrices_[static_cast<std::size_t>(bone)];
    const float bx     = matrix[0] * localOffset.x + matrix[4] * localOffset.y + matrix[8] * localOffset.z + matrix[12];
    const float by     = matrix[1] * localOffset.x + matrix[5] * localOffset.y + matrix[9] * localOffset.z + matrix[13];
    const float bz = matrix[2] * localOffset.x + matrix[6] * localOffset.y + matrix[10] * localOffset.z + matrix[14];
    const float sinYaw = std::sin(yaw_);
    const float cosYaw = std::cos(yaw_);
    return eve::Result<eve::AttachmentPoint>::success({x3_ + bx * sx3_ * cosYaw + bz * sz3_ * sinYaw, y3_ + by * sy3_,
                                                       z3_ - bx * sx3_ * sinYaw + bz * sz3_ * cosYaw});
}

animation::AnimPose* AvatarInstance::updateSkeletalAnimation(float dt) {
    if (vrm_) {
        auto matrix = glm::translate(glm::mat4(1), glm::vec3(x3_, y3_, z3_));
        matrix      = glm::rotate(matrix, yaw_, glm::vec3(0, 1, 0));
        matrix      = glm::rotate(matrix, pitch_, glm::vec3(1, 0, 0));
        matrix      = glm::rotate(matrix, roll_, glm::vec3(0, 0, 1));
        vrm_->world = glm::scale(matrix, glm::vec3(sx3_ * sx_, sy3_ * sy_, sz3_));
    }
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
    if (!pose || !animSkeleton_) {
        attachmentPoseMatrices_.clear();
        return nullptr;
    }
    if (vrm_ && animPlayer_ == vrm_->player.get() && !animPlayer_->getClip()) animSkeleton_->applyBindPose(pose);
    pose->computeWorld(animSkeleton_);
    applyLookAt(pose);
    if (footIKSolver_) footIKSolver_->apply(pose, dt);
    if (animConstraintStack_) animConstraintStack_->apply(pose);
    if (dynamicBoneSolver_) dynamicBoneSolver_->update(pose, dt);
    if (vrm_) vrm_->animate(*this, *pose, dt);
    updateRootMotion(pose);
    updateSkin(pose);
    updateAttachments(pose);
    attachmentPoseMatrices_.resize(static_cast<std::size_t>(pose->getBoneCount()));
    for (int bone = 0; bone < pose->getBoneCount(); ++bone)
        pose->getWorldMatrix(bone, attachmentPoseMatrices_[static_cast<std::size_t>(bone)].data());
    return pose;
}

int AvatarInstance::getAnimationEventCount() const {
    if (animLayerMixer_) return animLayerMixer_->getEventCount();
    if (animPlayer_) return animPlayer_->getEventCount();
    return 0;
}

std::size_t AvatarInstance::animationEventCount() const noexcept {
    const int count = getAnimationEventCount();
    return count > 0 ? static_cast<std::size_t>(count) : 0u;
}

std::string AvatarInstance::animationEventName(std::size_t index) const {
    if (index > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    return getAnimationEventName(static_cast<int>(index));
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
    if (!pose) return;
    if (animSkin_ && boundMesh_) (void)updateSkinnedMesh(animSkin_, boundMesh_, pose, skinnedPositions_);
    for (auto& part : skinnedParts_) part.updateMode = updateSkinnedMesh(part.skin, part.mesh, pose, part.cpuPositions);
    for (auto& visual : equipmentVisuals_) {
        if (visual.partIndex < 0 || !visual.skinnedSkin || !visual.skinnedMesh) continue;
        const auto active = projectedEquipment_.find(visual.equipmentSlot);
        if (active == projectedEquipment_.end() || active->second != visual.itemId) continue;
        visual.updateMode = updateSkinnedMesh(visual.skinnedSkin, visual.skinnedMesh, pose, visual.cpuPositions);
    }
}

SkinnedPartUpdateMode AvatarInstance::updateSkinnedMesh(animation::AnimSkin* skin, graphics::Mesh* mesh,
                                                        animation::AnimPose* pose, std::vector<float>& cpuPositions) {
    if (!skin || !mesh || !pose || mesh->getVertexCount() != skin->getVertexCount())
        return SkinnedPartUpdateMode::Unavailable;
    for (int bone = 0; bone < skin->getBoneCount(); ++bone) {
        const int poseBone = skin->getSkeletonBone(bone);
        if (poseBone < 0 || poseBone >= pose->getBoneCount()) return SkinnedPartUpdateMode::Unavailable;
    }
    auto* gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (gfx && !mesh->hasGpuSkinning()) (void)skin->bindGpuMesh(gfx, mesh);
    if (mesh->hasGpuSkinning() && skin->updateGpuMesh(mesh, pose)) return SkinnedPartUpdateMode::Gpu;
    if (!gfx || !skin->skinPositionsTo(pose, cpuPositions)) return SkinnedPartUpdateMode::Unavailable;
    return gfx->updateMeshVertices(mesh, cpuPositions.data(), nullptr, nullptr, skin->getVertexCount(), nullptr, 0)
               ? SkinnedPartUpdateMode::Cpu
               : SkinnedPartUpdateMode::Unavailable;
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
    if (vrm_ && pose) {
        vrm_->gaze(*this, *pose, vrm_->world, lookAtEnabled_, glm::vec3(lookAtX_, lookAtY_, lookAtZ_), lookAtWeight_);
        return;
    }
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
    if (equipment_) (void)syncEquipmentAppearance();
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

void AvatarInstance::bindTween(animation::Tween* tween) { tween_ = tween; }

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
        if (name == "x" || name == "y" || name == "sx" || name == "sy" || name == "x3" || name == "y3" ||
            name == "z3" || name == "yaw")
            continue;
        setParameter(name, tween_->get(name));
    }
}

}  // namespace eve::avatar
