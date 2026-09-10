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
// ---- vroid ----

eve::Result<void> AvatarInstance::loadVroidModel(std::string_view path) {
    if (kind_ != "vroid" || released_ || path.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Expected a live VRoid Avatar and a nonempty path", "path"));
    auto prepared = VrmRuntime::prepare(path);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    auto                                         candidate = std::move(prepared).takeValue();
    std::unordered_map<std::string, std::string> humanoid;
    for (const auto &[name, node] : candidate->document.humanoid) humanoid[name] = candidate->document.nodes[node];
    decltype(parameters_)        parameters;
    decltype(parameterOrder_)    order;
    decltype(parameterMetadata_) metadata;
    auto                         add = [&](const std::string &name) {
        if (name.empty() || parameters.contains(name)) return;
        parameters.emplace(name, 0.f);
        order.push_back(name);
        metadata.emplace(name, ParameterMetadata{});
    };
    for (const auto &expression : candidate->document.expressions) add(expression.name);
    for (int mesh = 0; mesh < candidate->model->getMeshCount(); ++mesh)
        for (int morph = 0; morph < candidate->model->getMorphTargetCount(mesh); ++morph)
            add(candidate->model->getMorphTargetName(mesh, morph));
    std::string importedPath(path);
    animPlayer_       = nullptr;
    animSkeleton_     = nullptr;
    animSkin_         = nullptr;
    animStateMachine_ = nullptr;
    animLayerMixer_   = nullptr;
    animConstraintStack_.reset();
    footIKSolver_.reset();
    dynamicBoneSolver_.reset();
    skinnedParts_.clear();
    motions_.clear();
    attachmentPoseMatrices_.clear();
    humanoidBones_ = std::move(humanoid);
    visemeMorphs_.clear();
    activeViseme_.clear();
    parameters_.swap(parameters);
    parameterOrder_.swap(order);
    parameterMetadata_.swap(metadata);
    expressionDefs_.clear();
    expressionBlendFrom_.clear();
    expressionBlendTo_.clear();
    expression_.clear();
    motion_.clear();
    lookAtApplied_   = false;
    hasPreviousRoot_ = false;
    vrm_             = std::move(candidate);
    vroidPath_       = std::move(importedPath);
    vroidData_       = vrm_->model.get();
    animPlayer_      = vrm_->player.get();
    animSkeleton_    = vrm_->skeleton.get();
    return eve::Result<void>::success();
}
eve::Result<void> AvatarInstance::setHumanoidBoneRotation(std::string_view semantic, float yaw, float pitch,
                                                          float roll) {
    if (!vrm_ || !std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(roll))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Expected imported VRM and finite angles"));
    auto found = vrm_->document.humanoid.find(std::string(semantic));
    if (found == vrm_->document.humanoid.end())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "Humanoid semantic is not mapped", std::string(semantic)));
    vrm_->rotations[vrm_->nodeBones[found->second]] = {yaw, pitch, roll};
    return eve::Result<void>::success();
}
bool AvatarInstance::loadVroidModelPath(const std::string &path) { return loadVroidModel(path).ok(); }
int  AvatarInstance::getVroidMeshCount() const { return vrm_ ? static_cast<int>(vrm_->parts.size()) : 0; }
int  AvatarInstance::getVroidSpringCount() const { return vrm_ ? static_cast<int>(vrm_->document.springs.size()) : 0; }
std::string AvatarInstance::getVroidVersion() const { return vrm_ ? vrm_->document.version : std::string{}; }
graphics::Renderable3D *AvatarInstance::getRenderable3D() const {
    if (vrm_ && !vrm_->parts.empty())
        return dynamic_cast<graphics::Renderable3D *>(ecs::try_get(vrm_->parts.front().entity));
    return renderable3d_;
}

bool AvatarInstance::bindVroidModelData(model3d::ModelData *data) {
    if (kind_ != "vroid" || vrm_) return false;
    vroidData_ = data;
    if (data) loadMorphNamesFromModel(0);
    return data != nullptr;
}

int AvatarInstance::loadMorphNamesFromModel(int meshIndex) {
    if (kind_ != "vroid" || !vroidData_) return 0;
    const int n     = vroidData_->getMorphTargetCount(meshIndex);
    int       added = 0;
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
    if (kind_ != "vroid" || vrm_) return;
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
    if (kind_ != "vroid" || vrm_) return;
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    renderable3d_->setTexture(texture);
}

void AvatarInstance::setPosition3D(float x, float y, float z) {
    x3_ = x;
    y3_ = y;
    z3_ = z;
}

void AvatarInstance::setRotation3D(float yaw, float pitch, float roll) {
    yaw_   = yaw;
    pitch_ = pitch;
    roll_  = roll;
}

void AvatarInstance::setScale3D(float sx, float sy, float sz) {
    sx3_ = sx;
    sy3_ = sy;
    sz3_ = sz;
}

graphics::Mesh *AvatarInstance::getBoundMesh() const {
    return vrm_ ? (!vrm_->providerLifetime.expired() && !vrm_->parts.empty() ? vrm_->parts.front().mesh : nullptr)
                : boundMesh_;
}

void AvatarInstance::syncMorphWeightsToMesh() {
    if (!boundMesh_ || !boundMesh_->hasMorphData()) return;
    for (int i = 0; i < boundMesh_->getMorphCount(); ++i) {
        const std::string name = boundMesh_->getMorphName(i);
        boundMesh_->setMorphWeight(name, getParameter(name));
    }
}

bool AvatarInstance::bakeMorphs() {
    if (vrm_) {
        vrm_->morphs(*this);
        return true;
    }
    syncMorphWeightsToMesh();
    if (!boundMesh_ || !boundMesh_->isMorphDirty()) return false;
    auto *gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!gfx) return false;
    return gfx->bakeMeshMorph(boundMesh_);
}

void AvatarInstance::syncVroid() {
    if (vrm_) {
        glm::mat4 world = glm::translate(glm::mat4(1), glm::vec3(x3_, y3_, z3_));
        world           = glm::rotate(world, yaw_, glm::vec3(0, 1, 0));
        world           = glm::rotate(world, pitch_, glm::vec3(1, 0, 0));
        world           = glm::rotate(world, roll_, glm::vec3(0, 0, 1));
        world           = glm::scale(world, glm::vec3(sx3_ * sx_, sy3_ * sy_, sz3_));
        vrm_->sync(*this, world, visible_);
        vrm_->morphs(*this);
        return;
    }
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    if (!sceneLinked_) {
        renderable3d_->setPosition(x3_, y3_, z3_);
        renderable3d_->setRotation(yaw_, pitch_, roll_);
        renderable3d_->setScale(sx3_ * sx_, sy3_ * sy_, sz3_);
        renderable3d_->setVisible(visible_);
    }
    for (auto &visual : equipmentVisuals_) {
        const auto active = projectedEquipment_.find(visual.equipmentSlot);
        if (!visual.renderable || !visual.boneName.empty() || active == projectedEquipment_.end() ||
            active->second != visual.itemId)
            continue;
        visual.renderable->setPosition(x3_ + visual.ox, y3_ + visual.oy, z3_ + visual.oz);
        visual.renderable->setRotation(yaw_, pitch_, roll_);
        visual.renderable->setScale(sx3_ * sx_, sy3_ * sy_, sz3_);
        visual.renderable->setVisible(visible_);
    }
    bakeMorphs();
}

void AvatarInstance::destroyVroid() {
    vrm_.reset();
    if (renderable3d_) {
        ecs::DestroyEntity(renderable3d_);
        renderable3d_ = nullptr;
    }
    boundMesh_ = nullptr;
    vroidData_ = nullptr;
    vroidPath_.clear();
}

}  // namespace eve::avatar
