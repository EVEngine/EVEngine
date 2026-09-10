#include "avatar/AvatarInstance.h"
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
    equipmentLayers_.emplace("body", EquipmentLayer{"", 0, {}});
    equipmentLayers_.emplace("underwear", EquipmentLayer{"body", 100, {}});
    equipmentLayers_.emplace("shirt", EquipmentLayer{"underwear", 200, {}});
    equipmentLayers_.emplace("outerwear", EquipmentLayer{"shirt", 300, {}});
    equipmentLayers_.emplace("cape", EquipmentLayer{"outerwear", 400, {}});
    equipmentLayers_.emplace("weapon", EquipmentLayer{"body", 500, {}});
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
    animConstraintStack_.reset();
    footIKSolver_.reset();
    dynamicBoneSolver_ = nullptr;
    animSkin_         = nullptr;
    animSkeleton_     = nullptr;
    attachmentPoseMatrices_.clear();
    motions_.clear();
    attachments_.clear();
    hideEquipmentVisuals();
    equipment_ = nullptr;
    equipmentVisuals_.clear();
    skinnedParts_.clear();
    projectedEquipment_.clear();
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

EquipmentVisualChange AvatarInstance::bindEquipment(inventory::EquipmentSet* equipment) {
    if (!equipment) return unbindEquipment();
    equipment_ = equipment;
    equipmentSignature_.clear();
    return syncEquipmentAppearance();
}

EquipmentVisualChange AvatarInstance::unbindEquipment() {
    const bool hadBinding = equipment_ != nullptr || !projectedEquipment_.empty();
    equipment_ = nullptr;
    equipmentSignature_.clear();
    projectedEquipment_.clear();
    hideEquipmentVisuals();
    return hadBinding ? EquipmentVisualChange::Removed : EquipmentVisualChange::Unchanged;
}

EquipmentVisualChange AvatarInstance::defineEquipmentVisual2D(
    const std::string& itemId, const std::string& equipmentSlot, const std::string& layerName,
    graphics::Texture* texture, int zIndex) {
    if (kind_ != "image" || itemId.empty() || equipmentSlot.empty() || layerName.empty())
        return EquipmentVisualChange::Rejected;
    for (auto& visual : equipmentVisuals_) {
        if (visual.itemId != itemId || visual.equipmentSlot != equipmentSlot) continue;
        if (visual.layerName == layerName && visual.texture == texture && visual.zIndex == zIndex)
            return EquipmentVisualChange::Unchanged;
        visual.layerName = layerName;
        visual.texture   = texture;
        visual.zIndex    = zIndex;
        equipmentSignature_.clear();
        return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
    }
    EquipmentVisual visual;
    visual.itemId        = itemId;
    visual.equipmentSlot = equipmentSlot;
    visual.layerName     = layerName;
    visual.texture       = texture;
    visual.wearLayer     = equipmentSlot;
    visual.zIndex        = zIndex;
    equipmentVisuals_.push_back(std::move(visual));
    equipmentSignature_.clear();
    return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
}

EquipmentVisualChange AvatarInstance::defineEquipmentVisual3D(
    const std::string& itemId, const std::string& equipmentSlot,
    graphics::Renderable3D* renderable, const std::string& boneSemanticOrName, float ox, float oy,
    float oz) {
    if (kind_ != "vroid" || itemId.empty() || equipmentSlot.empty() || !renderable)
        return EquipmentVisualChange::Rejected;
    for (auto& visual : equipmentVisuals_) {
        if (visual.itemId != itemId || visual.equipmentSlot != equipmentSlot) continue;
        if (visual.renderable == renderable && visual.boneName == boneSemanticOrName &&
            visual.ox == ox && visual.oy == oy && visual.oz == oz)
            return EquipmentVisualChange::Unchanged;
        visual.renderable = renderable;
        visual.boneName   = boneSemanticOrName;
        visual.ox = ox;
        visual.oy = oy;
        visual.oz = oz;
        equipmentSignature_.clear();
        return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
    }
    EquipmentVisual visual;
    visual.itemId       = itemId;
    visual.equipmentSlot = equipmentSlot;
    visual.renderable   = renderable;
    visual.boneName     = boneSemanticOrName;
    visual.wearLayer    = equipmentSlot;
    visual.ox = ox;
    visual.oy = oy;
    visual.oz = oz;
    equipmentVisuals_.push_back(std::move(visual));
    renderable->setVisible(false);
    equipmentSignature_.clear();
    return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
}

EquipmentVisualChange AvatarInstance::defineEquipmentSkinnedVisual3D(
    const std::string& itemId, const std::string& equipmentSlot, int partIndex,
    const std::string& partName, graphics::Mesh* mesh, graphics::Material* material,
    animation::AnimSkin* skin) {
    if (kind_ != "vroid" || itemId.empty() || equipmentSlot.empty() || partName.empty() ||
        !mesh || !skin || partIndex < 0 ||
        partIndex >= graphics::Renderable3D::MeshRenderer::kMaxParts)
        return EquipmentVisualChange::Rejected;
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    for (const auto& part : skinnedParts_) {
        if (part.partIndex == partIndex) return EquipmentVisualChange::Rejected;
    }
    for (const auto& visual : equipmentVisuals_) {
        if (visual.partIndex == partIndex && visual.equipmentSlot != equipmentSlot)
            return EquipmentVisualChange::Rejected;
    }
    for (auto& visual : equipmentVisuals_) {
        if (visual.itemId != itemId || visual.equipmentSlot != equipmentSlot) continue;
        if (visual.partIndex == partIndex && visual.layerName == partName &&
            visual.skinnedMesh == mesh && visual.skinnedMaterial == material &&
            visual.skinnedSkin == skin)
            return EquipmentVisualChange::Unchanged;
        if (renderable3d_ && visual.partIndex >= 0) {
            renderable3d_->setPart(visual.partIndex, visual.layerName, nullptr, nullptr);
            renderable3d_->clearPartSortPriority(visual.partIndex);
        }
        if (visual.renderable) visual.renderable->setVisible(false);
        if (!visual.boneName.empty()) detachAttachment("equipment:" + visual.equipmentSlot);
        visual.partIndex       = partIndex;
        visual.layerName       = partName;
        visual.skinnedMesh     = mesh;
        visual.skinnedMaterial = material;
        visual.skinnedSkin     = skin;
        visual.renderable      = nullptr;
        visual.boneName.clear();
        visual.cpuPositions.clear();
        equipmentSignature_.clear();
        return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
    }
    EquipmentVisual visual;
    visual.itemId         = itemId;
    visual.equipmentSlot  = equipmentSlot;
    visual.layerName      = partName;
    visual.wearLayer      = equipmentSlot;
    visual.partIndex      = partIndex;
    visual.skinnedMesh     = mesh;
    visual.skinnedMaterial = material;
    visual.skinnedSkin     = skin;
    equipmentVisuals_.push_back(std::move(visual));
    equipmentSignature_.clear();
    return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
}

EquipmentVisualChange AvatarInstance::defineEquipmentLayer(const std::string& name, int order,
                                                            const std::string& parent) {
    if (name.empty() || name == parent || (!parent.empty() && !equipmentLayers_.count(parent)))
        return EquipmentVisualChange::Rejected;
    auto found = equipmentLayers_.find(name);
    if (found != equipmentLayers_.end() && found->second.order == order &&
        found->second.parent == parent)
        return EquipmentVisualChange::Unchanged;
    auto& layer  = equipmentLayers_[name];
    layer.order  = order;
    layer.parent = parent;
    equipmentSignature_.clear();
    return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
}

EquipmentVisualChange AvatarInstance::addEquipmentLayerOcclusion(const std::string& outerLayer,
                                                                  const std::string& innerLayer) {
    const auto outer = equipmentLayers_.find(outerLayer);
    const auto inner = equipmentLayers_.find(innerLayer);
    if (outer == equipmentLayers_.end() || inner == equipmentLayers_.end() ||
        outerLayer == innerLayer || outer->second.order <= inner->second.order)
        return EquipmentVisualChange::Rejected;
    auto& occludes = outer->second.occludes;
    if (std::find(occludes.begin(), occludes.end(), innerLayer) != occludes.end())
        return EquipmentVisualChange::Unchanged;
    occludes.push_back(innerLayer);
    equipmentSignature_.clear();
    return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
}

EquipmentVisualChange AvatarInstance::setEquipmentVisualLayer(
    const std::string& itemId, const std::string& equipmentSlot, const std::string& wearLayer,
    int withinLayerOrder) {
    if (!equipmentLayers_.count(wearLayer)) return EquipmentVisualChange::Rejected;
    for (auto& visual : equipmentVisuals_) {
        if (visual.itemId != itemId || visual.equipmentSlot != equipmentSlot) continue;
        if (visual.wearLayer == wearLayer && visual.withinLayerOrder == withinLayerOrder)
            return EquipmentVisualChange::Unchanged;
        visual.wearLayer        = wearLayer;
        visual.withinLayerOrder = withinLayerOrder;
        equipmentSignature_.clear();
        return equipment_ ? syncEquipmentAppearance() : EquipmentVisualChange::Applied;
    }
    return EquipmentVisualChange::Rejected;
}

int AvatarInstance::equipmentLayerOrder(const std::string& name) const {
    const auto found = equipmentLayers_.find(name);
    return found == equipmentLayers_.end() ? 0 : found->second.order;
}

void AvatarInstance::hideEquipmentVisuals() {
    for (auto& visual : equipmentVisuals_) {
        visual.updateMode = SkinnedPartUpdateMode::Unavailable;
        if (kind_ == "image" && !visual.layerName.empty()) {
            if (Layer* layer = findLayer(visual.layerName)) setLayerVisible(layer->name, false);
        }
        if (visual.renderable) visual.renderable->setVisible(false);
        if (visual.partIndex >= 0 && renderable3d_)
            renderable3d_->setPart(visual.partIndex, visual.layerName, nullptr, nullptr);
        if (visual.partIndex >= 0 && renderable3d_)
            renderable3d_->clearPartSortPriority(visual.partIndex);
        if (!visual.boneName.empty()) detachAttachment("equipment:" + visual.equipmentSlot);
    }
}

EquipmentVisualChange AvatarInstance::syncEquipmentAppearance() {
    if (!equipment_) return EquipmentVisualChange::Rejected;
    std::string signature;
    std::unordered_map<std::string, std::string> equipped;
    for (int index = 0; index < equipment_->getSlotCount(); ++index) {
        const std::string slot = equipment_->getSlotName(index);
        const std::string item = equipment_->getSlotItemId(slot);
        signature += slot + "=" + item + "#" + std::to_string(equipment_->getSlotInstanceId(slot)) + ";";
        if (!item.empty()) equipped.emplace(slot, item);
    }
    if (signature == equipmentSignature_) return EquipmentVisualChange::Unchanged;

    hideEquipmentVisuals();
    projectedEquipment_.clear();
    equipmentRenderStack_.clear();
    std::vector<std::string> occludedLayers;
    for (const auto& visual : equipmentVisuals_) {
        const auto active = equipped.find(visual.equipmentSlot);
        if (active == equipped.end() || active->second != visual.itemId) continue;
        const auto layer = equipmentLayers_.find(visual.wearLayer);
        if (layer == equipmentLayers_.end()) continue;
        occludedLayers.insert(occludedLayers.end(), layer->second.occludes.begin(),
                              layer->second.occludes.end());
    }
    for (auto& visual : equipmentVisuals_) {
        const auto active = equipped.find(visual.equipmentSlot);
        if (active == equipped.end() || active->second != visual.itemId) continue;
        if (std::find(occludedLayers.begin(), occludedLayers.end(), visual.wearLayer) !=
            occludedLayers.end())
            continue;
        const int renderOrder = equipmentLayerOrder(visual.wearLayer) * 1000 +
                                visual.withinLayerOrder;
        if (kind_ == "image" && !visual.layerName.empty()) {
            if (!hasLayer(visual.layerName)) addLayer(visual.layerName, visual.texture, visual.zIndex);
            setLayerTexture(visual.layerName, visual.texture);
            setLayerZ(visual.layerName, renderOrder + visual.zIndex);
            setLayerVisible(visual.layerName, true);
        }
        if (visual.renderable) {
            visual.renderable->setVisible(visible_);
            if (visual.boneName.empty()) {
                visual.renderable->setPosition(x3_ + visual.ox, y3_ + visual.oy, z3_ + visual.oz);
                visual.renderable->setRotation(yaw_, pitch_, roll_);
                visual.renderable->setScale(sx3_, sy3_, sz3_);
            } else {
                attachToBone("equipment:" + visual.equipmentSlot, visual.boneName, visual.renderable,
                             visual.ox, visual.oy, visual.oz);
            }
        }
        if (visual.partIndex >= 0 && visual.skinnedMesh && renderable3d_) {
            renderable3d_->setPart(visual.partIndex, visual.layerName, visual.skinnedMesh,
                                   visual.skinnedMaterial);
            renderable3d_->setPartSortPriority(visual.partIndex, renderOrder);
        }
        projectedEquipment_[visual.equipmentSlot] = visual.itemId;
        equipmentRenderStack_.push_back({visual.itemId, visual.wearLayer, renderOrder});
    }
    std::stable_sort(equipmentRenderStack_.begin(), equipmentRenderStack_.end(),
                     [](const EquipmentRenderEntry& a, const EquipmentRenderEntry& b) {
                         if (a.order != b.order) return a.order < b.order;
                         return a.itemId < b.itemId;
                     });
    equipmentSignature_ = std::move(signature);
    return EquipmentVisualChange::Applied;
}

std::string AvatarInstance::getEquipmentVisualItem(const std::string& equipmentSlot) const {
    const auto found = projectedEquipment_.find(equipmentSlot);
    return found == projectedEquipment_.end() ? std::string{} : found->second;
}

int AvatarInstance::getEquipmentRenderStackCount() const {
    return static_cast<int>(equipmentRenderStack_.size());
}

std::string AvatarInstance::getEquipmentRenderStackItem(int index) const {
    return index < 0 || index >= static_cast<int>(equipmentRenderStack_.size())
               ? std::string{}
               : equipmentRenderStack_[static_cast<size_t>(index)].itemId;
}

std::string AvatarInstance::getEquipmentRenderStackLayer(int index) const {
    return index < 0 || index >= static_cast<int>(equipmentRenderStack_.size())
               ? std::string{}
               : equipmentRenderStack_[static_cast<size_t>(index)].wearLayer;
}

void AvatarInstance::setExpression(const std::string &name) {
    if (vrm_) {
        for (const auto& e : vrm_->document.expressions) setParameter(e.name, e.name == name ? 1.f : 0.f);
        expression_ = name;
        return;
    }
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
    if (equipment_) (void)syncEquipmentAppearance();
    applyTweenTracks();
    updateExpressionTransition(dt);
    if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->update(dt);
    }
    if (kind_ == "vroid") updateSkeletalAnimation(dt);
}

bool AvatarInstance::bindAnimPlayer(animation::AnimPlayer* player) {
    if (kind_ != "vroid" || vrm_) return false;
    animPlayer_       = player;
    animStateMachine_ = nullptr;
    animLayerMixer_   = nullptr;
    animSkeleton_     = player ? player->getSkeleton() : nullptr;
    attachmentPoseMatrices_.clear();
    hasPreviousRoot_  = false;
    if (footIKSolver_) footIKSolver_->setSkeleton(animSkeleton_);
    if (animConstraintStack_) animConstraintStack_->setSkeleton(animSkeleton_);
    if (dynamicBoneSolver_) dynamicBoneSolver_->setSkeleton(animSkeleton_);
    return player != nullptr;
}

bool AvatarInstance::bindAnimStateMachine(animation::AnimStateMachine* machine) {
    if (kind_ != "vroid" || vrm_) return false;
    animStateMachine_ = machine;
    animPlayer_       = nullptr;
    animLayerMixer_   = nullptr;
    animSkeleton_     = machine ? machine->getSkeleton() : nullptr;
    attachmentPoseMatrices_.clear();
    hasPreviousRoot_  = false;
    if (footIKSolver_) footIKSolver_->setSkeleton(animSkeleton_);
    if (animConstraintStack_) animConstraintStack_->setSkeleton(animSkeleton_);
    if (dynamicBoneSolver_) dynamicBoneSolver_->setSkeleton(animSkeleton_);
    return machine != nullptr;
}

bool AvatarInstance::bindAnimLayerMixer(animation::AnimLayerMixer* mixer) {
    if (kind_ != "vroid" || vrm_) return false;
    animLayerMixer_   = mixer;
    animStateMachine_ = nullptr;
    animPlayer_       = mixer ? mixer->getBasePlayer() : nullptr;
    animSkeleton_     = mixer ? mixer->getSkeleton() : nullptr;
    attachmentPoseMatrices_.clear();
    hasPreviousRoot_  = false;
    if (footIKSolver_) footIKSolver_->setSkeleton(animSkeleton_);
    if (animConstraintStack_) animConstraintStack_->setSkeleton(animSkeleton_);
    if (dynamicBoneSolver_) dynamicBoneSolver_->setSkeleton(animSkeleton_);
    return mixer != nullptr;
}

void AvatarInstance::setAnimConstraintStack(animation::AnimConstraintStack* stack) {
    if (kind_ != "vroid" || !stack) {
        animConstraintStack_.reset();
        return;
    }
    animConstraintStack_ = std::make_unique<animation::AnimConstraintStack>(*stack);
    animConstraintStack_->setSkeleton(animSkeleton_);
}

void AvatarInstance::setFootIKSolver(animation::FootIKSolver* solver) {
    if (kind_ != "vroid" || !solver) {
        footIKSolver_.reset();
        return;
    }
    footIKSolver_ = std::make_unique<animation::FootIKSolver>(*solver);
    footIKSolver_->setSkeleton(animSkeleton_);
}

void AvatarInstance::setDynamicBoneSolver(animation::DynamicBoneSolver* solver) {
    if (kind_ != "vroid") {
        dynamicBoneSolver_.reset();
        return;
    }
    if (!solver) {
        dynamicBoneSolver_.reset();
        return;
    }
    dynamicBoneSolver_ = std::make_unique<animation::DynamicBoneSolver>(*solver);
    if (dynamicBoneSolver_) dynamicBoneSolver_->setSkeleton(animSkeleton_);
}

bool AvatarInstance::bindAnimSkin(animation::AnimSkin* skin) {
    if (kind_ != "vroid" || vrm_) return false;
    animSkin_ = skin;
    skinnedPositions_.clear();
    return skin != nullptr;
}

EquipmentVisualChange AvatarInstance::bindSkinnedPart(
    int partIndex, const std::string& partName, graphics::Mesh* mesh,
    graphics::Material* material, animation::AnimSkin* skin) {
    if (kind_ != "vroid" || partIndex < 0 ||
        partIndex >= graphics::Renderable3D::MeshRenderer::kMaxParts || partName.empty() ||
        !mesh || !skin)
        return EquipmentVisualChange::Rejected;
    for (const auto& visual : equipmentVisuals_) {
        if (visual.partIndex == partIndex) return EquipmentVisualChange::Rejected;
    }
    for (auto& part : skinnedParts_) {
        if (part.partIndex != partIndex) continue;
        if (part.partName == partName && part.mesh == mesh && part.material == material &&
            part.skin == skin)
            return EquipmentVisualChange::Unchanged;
        part.partName = partName;
        part.mesh = mesh;
        part.material = material;
        part.skin = skin;
        part.cpuPositions.clear();
        if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
        renderable3d_->setPart(partIndex, partName, mesh, material);
        return EquipmentVisualChange::Applied;
    }
    SkinnedPart part;
    part.partIndex = partIndex;
    part.partName  = partName;
    part.mesh      = mesh;
    part.material  = material;
    part.skin      = skin;
    skinnedParts_.push_back(std::move(part));
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    renderable3d_->setPart(partIndex, partName, mesh, material);
    return EquipmentVisualChange::Applied;
}

EquipmentVisualChange AvatarInstance::unbindSkinnedPart(int partIndex) {
    const auto found = std::find_if(skinnedParts_.begin(), skinnedParts_.end(),
                                    [&](const SkinnedPart& part) {
                                        return part.partIndex == partIndex;
                                    });
    if (found == skinnedParts_.end()) return EquipmentVisualChange::Unchanged;
    skinnedParts_.erase(found);
    if (renderable3d_) renderable3d_->setPart(partIndex, {}, nullptr, nullptr);
    return EquipmentVisualChange::Removed;
}

SkinnedPartUpdateMode AvatarInstance::getSkinnedPartUpdateMode(int partIndex) const {
    for (const auto& part : skinnedParts_) {
        if (part.partIndex == partIndex) return part.updateMode;
    }
    for (const auto& visual : equipmentVisuals_) {
        if (visual.partIndex == partIndex) return visual.updateMode;
    }
    return SkinnedPartUpdateMode::Unavailable;
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
    if (texture && !texture->hasDeferredFilePixels()) {
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
    if (texture && L->autoSize && !texture->hasDeferredFilePixels()) {
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
    if (vrm_) {
        for (const auto& e : vrm_->document.expressions)
            if (e.name == name) {
                setExpression(name);
                return true;
            }
    }
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

}  // namespace eve::avatar
