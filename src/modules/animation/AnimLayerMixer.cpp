#include "animation/AnimLayerMixer.h"

#include "animation/AnimGraph.h"
#include "animation/AnimMath.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimStateMachine.h"
#include "animation/AnimationTime.h"
#include "common/Exception.h"

#include <algorithm>
#include <cmath>

namespace eve::animation {
namespace {

float layerBoneWeight(const AnimBoneMask* mask, float layerWeight, int boneIndex) {
    const float maskWeight = mask ? mask->getBoneWeight(boneIndex) : 1.f;
    return clampf(layerWeight * maskWeight, 0.f, 1.f);
}

eve::Result<AnimAdditiveReference> parseAdditiveReference(const std::string& reference) {
    if (reference == "bind" || reference == "bind_pose" || reference == "BindPose")
        return eve::Result<AnimAdditiveReference>::success(AnimAdditiveReference::BindPose);
    if (reference == "identity" || reference == "Identity")
        return eve::Result<AnimAdditiveReference>::success(AnimAdditiveReference::Identity);
    return eve::Result<AnimAdditiveReference>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "animation additive reference must be \"bind\" or \"identity\""));
}

const char* additiveReferenceName(AnimAdditiveReference reference) {
    return reference == AnimAdditiveReference::Identity ? "identity" : "bind";
}

}  // namespace

AnimBoneMask::AnimBoneMask(AnimSkeleton* skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("AnimBoneMask: skeleton is null");
    weights_.assign(static_cast<size_t>(skeleton_->getBoneCount()), 0.f);
}

void AnimBoneMask::setAll(float weight) { std::fill(weights_.begin(), weights_.end(), clampf(weight, 0.f, 1.f)); }

bool AnimBoneMask::setBoneWeight(int boneIndex, float weight) {
    if (boneIndex < 0 || boneIndex >= getBoneCount()) return false;
    weights_[static_cast<size_t>(boneIndex)] = clampf(weight, 0.f, 1.f);
    return true;
}

bool AnimBoneMask::setBoneWeightByName(const std::string& boneName, float weight) {
    return skeleton_ && setBoneWeight(skeleton_->findBone(boneName), weight);
}

bool AnimBoneMask::setBoneAndChildren(const std::string& boneName, float weight) {
    if (!skeleton_) return false;
    const int root = skeleton_->findBone(boneName);
    if (root < 0) return false;
    const float clamped = clampf(weight, 0.f, 1.f);
    for (int bone = 0; bone < skeleton_->getBoneCount(); ++bone) {
        int current = bone;
        while (current >= 0 && current != root) current = skeleton_->getParent(current);
        if (current == root) weights_[static_cast<size_t>(bone)] = clamped;
    }
    return true;
}

float AnimBoneMask::getBoneWeight(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= getBoneCount()) return 0.f;
    return weights_[static_cast<size_t>(boneIndex)];
}

AnimLayerMixer::AnimLayerMixer(AnimSkeleton* skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("AnimLayerMixer: skeleton is null");
    pose_.resize(skeleton_->getBoneCount());
    skeleton_->applyBindPose(&pose_);
}

bool AnimLayerMixer::sourceAlreadyAttached(const IAnimPoseSource* source) const {
    if (!source) return false;
    if (baseSource_ == source) return true;
    for (const Layer& layer : layers_)
        if (layer.source == source) return true;
    return false;
}

eve::Result<void> AnimLayerMixer::attachSource(IAnimPoseSource* source, const char* role) {
    if (!source)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 std::string("animation mixer ") + role + " is null"));
    if (source == this)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "animation mixer cannot attach itself as a pose source"));
    if (source->getSkeleton() != skeleton_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, std::string("animation mixer ") + role + " skeleton mismatch"));
    if (sourceAlreadyAttached(source))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, std::string("animation mixer ") + role + " is already attached"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> AnimLayerMixer::setBaseSource(IAnimPoseSource* source) {
    if (!source) {
        baseSource_ = nullptr;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    // Allow replacing the current base with itself without conflict.
    if (source == baseSource_) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    if (source == this)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "animation mixer cannot attach itself as a pose source"));
    if (source->getSkeleton() != skeleton_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "animation mixer base skeleton mismatch"));
    for (const Layer& layer : layers_)
        if (layer.source == source)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "animation mixer base is already a layer"));
    baseSource_ = source;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool AnimLayerMixer::setBasePlayer(AnimPlayer* player) {
    auto result = setBaseSource(player);
    if (!result) {
        result.ignore("AnimLayerMixer.setBasePlayer compatibility facade");
        return false;
    }
    return true;
}

bool AnimLayerMixer::setBaseGraph(AnimGraph* graph) {
    auto result = setBaseSource(graph);
    if (!result) {
        result.ignore("AnimLayerMixer.setBaseGraph compatibility facade");
        return false;
    }
    return true;
}

bool AnimLayerMixer::setBaseStateMachine(AnimStateMachine* stateMachine) {
    auto result = setBaseSource(stateMachine);
    if (!result) {
        result.ignore("AnimLayerMixer.setBaseStateMachine compatibility facade");
        return false;
    }
    return true;
}

AnimPlayer* AnimLayerMixer::getBasePlayer() const { return dynamic_cast<AnimPlayer*>(baseSource_); }

eve::Result<int> AnimLayerMixer::addPoseLayer(const std::string& name, IAnimPoseSource* source, AnimBoneMask* mask,
                                              const std::string& mode) {
    if (name.empty())
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "animation mixer layer name is empty"));
    if (findLayer(name))
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "animation mixer layer name already exists"));
    if (mode != "override" && mode != "additive")
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "animation mixer layer mode must be \"override\" or \"additive\""));
    if (mask && mask->getSkeleton() != skeleton_)
        return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                "animation mixer layer mask skeleton mismatch"));
    auto attached = attachSource(source, "layer");
    if (!attached) return eve::Result<int>::failure(attached.status());

    layers_.push_back({name, source, mask, 1.f, mode == "additive", true, AnimAdditiveReference::BindPose});
    return eve::Result<int>::success(static_cast<int>(layers_.size()) - 1);
}

int AnimLayerMixer::addLayer(const std::string& name, AnimPlayer* player, AnimBoneMask* mask, const std::string& mode) {
    auto result = addPoseLayer(name, player, mask, mode);
    if (!result) {
        result.ignore("AnimLayerMixer.addLayer compatibility facade");
        return -1;
    }
    return std::move(result).takeValue();
}

int AnimLayerMixer::addGraphLayer(const std::string& name, AnimGraph* graph, AnimBoneMask* mask,
                                  const std::string& mode) {
    auto result = addPoseLayer(name, graph, mask, mode);
    if (!result) {
        result.ignore("AnimLayerMixer.addGraphLayer compatibility facade");
        return -1;
    }
    return std::move(result).takeValue();
}

int AnimLayerMixer::addStateMachineLayer(const std::string& name, AnimStateMachine* stateMachine, AnimBoneMask* mask,
                                         const std::string& mode) {
    auto result = addPoseLayer(name, stateMachine, mask, mode);
    if (!result) {
        result.ignore("AnimLayerMixer.addStateMachineLayer compatibility facade");
        return -1;
    }
    return std::move(result).takeValue();
}

AnimLayerMixer::Layer* AnimLayerMixer::findLayer(const std::string& name) {
    for (auto& layer : layers_)
        if (layer.name == name) return &layer;
    return nullptr;
}

const AnimLayerMixer::Layer* AnimLayerMixer::findLayer(const std::string& name) const {
    for (const auto& layer : layers_)
        if (layer.name == name) return &layer;
    return nullptr;
}

bool AnimLayerMixer::removeLayer(const std::string& name) {
    const auto it =
        std::find_if(layers_.begin(), layers_.end(), [&](const Layer& layer) { return layer.name == name; });
    if (it == layers_.end()) return false;
    layers_.erase(it);
    return true;
}

bool AnimLayerMixer::setLayerWeight(const std::string& name, float weight) {
    Layer* layer = findLayer(name);
    if (!layer) return false;
    layer->weight = clampf(weight, 0.f, 1.f);
    return true;
}

bool AnimLayerMixer::setLayerEnabled(const std::string& name, bool enabled) {
    Layer* layer = findLayer(name);
    if (!layer) return false;
    layer->enabled = enabled;
    return true;
}

eve::Result<void> AnimLayerMixer::setLayerAdditiveReference(const std::string& name, const std::string& reference) {
    Layer* layer = findLayer(name);
    if (!layer)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "animation mixer layer not found"));
    if (!layer->additive)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "animation mixer additive reference requires an additive layer"));
    auto parsed = parseAdditiveReference(reference);
    if (!parsed) return eve::Result<void>::failure(parsed.status());
    layer->additiveReference = std::move(parsed).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool AnimLayerMixer::setLayerAdditiveReferenceCompat(const std::string& name, const std::string& reference) {
    auto result = setLayerAdditiveReference(name, reference);
    if (!result) {
        result.ignore("AnimLayerMixer.setLayerAdditiveReferenceCompat");
        return false;
    }
    return true;
}

std::string AnimLayerMixer::getLayerName(int index) const {
    if (index < 0 || index >= getLayerCount()) return {};
    return layers_[static_cast<size_t>(index)].name;
}

float AnimLayerMixer::getLayerWeight(const std::string& name) const {
    const Layer* layer = findLayer(name);
    return layer ? layer->weight : 0.f;
}

bool AnimLayerMixer::getLayerEnabled(const std::string& name) const {
    const Layer* layer = findLayer(name);
    return layer && layer->enabled;
}

std::string AnimLayerMixer::getLayerMode(const std::string& name) const {
    const Layer* layer = findLayer(name);
    if (!layer) return {};
    return layer->additive ? "additive" : "override";
}

std::string AnimLayerMixer::getLayerAdditiveReference(const std::string& name) const {
    const Layer* layer = findLayer(name);
    if (!layer) return {};
    return additiveReferenceName(layer->additiveReference);
}

void AnimLayerMixer::collectEvents(const std::string& layerName, IAnimPoseSource* source) {
    if (!source) return;
    for (int i = 0; i < source->getEventCount(); ++i)
        events_.push_back({layerName, source->getEventName(i), source->getEventPayload(i)});
}

void AnimLayerMixer::applyOverride(const Layer& layer) {
    AnimPose* layerPose = layer.source->getPose();
    for (int bone = 0; bone < pose_.getBoneCount(); ++bone) {
        const float weight = layerBoneWeight(layer.mask, layer.weight, bone);
        if (weight <= 0.f) continue;
        pose_.local(bone) = blendTRS(pose_.local(bone), layerPose->local(bone), weight);
    }
}

void AnimLayerMixer::applyAdditive(const Layer& layer) {
    AnimPose* layerPose = layer.source->getPose();
    for (int bone = 0; bone < pose_.getBoneCount(); ++bone) {
        const float weight = layerBoneWeight(layer.mask, layer.weight, bone);
        if (weight <= 0.f) continue;
        const TransformTRS& reference = layer.additiveReference == AnimAdditiveReference::Identity
                                            ? TransformTRS::identity()
                                            : skeleton_->bindLocal(bone);
        applyAdditiveTRS(pose_.local(bone), layerPose->local(bone), reference, weight);
    }
}

void AnimLayerMixer::compose() {
    events_.clear();
    if (baseSource_) {
        pose_.copyFrom(baseSource_->getPose());
        collectEvents("base", baseSource_);
    } else {
        skeleton_->applyBindPose(&pose_);
    }
    for (const Layer& layer : layers_) {
        if (!layer.enabled || !layer.source) continue;
        if (layer.weight <= 0.f) continue;
        collectEvents(layer.name, layer.source);
        if (layer.additive)
            applyAdditive(layer);
        else
            applyOverride(layer);
    }
    pose_.computeWorld(skeleton_);
}

eve::Result<void> AnimLayerMixer::advance(const eve::SimulationStep& step) {
    auto seconds = detail::secondsForStep(step, hasLastTick_, lastTick_, "AnimLayerMixer");
    if (!seconds) return eve::Result<void>::failure(seconds.status());
    (void)std::move(seconds).takeValue();

    // The mixer owns the evaluation boundary: every attached source consumes the
    // same injected step (including disabled layers, so their clocks stay in sync).
    if (baseSource_ && baseSource_->hasCurrentTick() && step.tick <= baseSource_->currentTick())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "animation mixer base source already consumed this tick"));
    for (const Layer& layer : layers_) {
        if (layer.source && layer.source->hasCurrentTick() && step.tick <= layer.source->currentTick())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "animation mixer layer source already consumed this tick"));
    }
    if (baseSource_) {
        auto result = baseSource_->advance(step);
        if (!result) return eve::Result<void>::failure(result.status());
    }
    for (const Layer& layer : layers_) {
        if (!layer.source) continue;
        auto result = layer.source->advance(step);
        if (!result) return eve::Result<void>::failure(result.status());
    }
    compose();
    lastTick_    = step.tick;
    hasLastTick_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void AnimLayerMixer::update(float dt) {
    auto step = detail::legacyStep(dt, hasLastTick_, lastTick_, "AnimLayerMixer");
    if (!step) {
        step.ignore("legacy AnimLayerMixer update");
        return;
    }
    advance(std::move(step).takeValue()).ignore("legacy AnimLayerMixer update");
}

std::string AnimLayerMixer::getEventLayer(int index) const {
    if (index < 0 || index >= getEventCount()) return {};
    return events_[static_cast<size_t>(index)].layer;
}

std::string AnimLayerMixer::getEventName(int index) const {
    if (index < 0 || index >= getEventCount()) return {};
    return events_[static_cast<size_t>(index)].name;
}

std::string AnimLayerMixer::getEventPayload(int index) const {
    if (index < 0 || index >= getEventCount()) return {};
    return events_[static_cast<size_t>(index)].payload;
}

}  // namespace eve::animation
