#include "animation/AnimLayerMixer.h"

#include "animation/AnimationTime.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "common/Exception.h"

#include <algorithm>
#include <cmath>

namespace eve::animation {
namespace {

float layerBoneWeight(const AnimBoneMask* mask, float layerWeight, int boneIndex) {
    const float maskWeight = mask ? mask->getBoneWeight(boneIndex) : 1.f;
    return clampf(layerWeight * maskWeight, 0.f, 1.f);
}

void multiplyQuat(float ax, float ay, float az, float aw, float bx, float by, float bz, float bw, float& ox, float& oy,
                  float& oz, float& ow) {
    ow = aw * bw - ax * bx - ay * by - az * bz;
    ox = aw * bx + ax * bw + ay * bz - az * by;
    oy = aw * by - ax * bz + ay * bw + az * bx;
    oz = aw * bz + ax * by - ay * bx + az * bw;
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

bool AnimLayerMixer::setBasePlayer(AnimPlayer* player) {
    if (player && player->getSkeleton() != skeleton_) return false;
    for (const Layer& layer : layers_)
        if (layer.player == player) return false;
    basePlayer_ = player;
    return true;
}

int AnimLayerMixer::addLayer(const std::string& name, AnimPlayer* player, AnimBoneMask* mask, const std::string& mode) {
    if (name.empty() || !player || player == basePlayer_ || player->getSkeleton() != skeleton_ || findLayer(name))
        return -1;
    for (const Layer& layer : layers_)
        if (layer.player == player) return -1;
    if (mask && mask->getSkeleton() != skeleton_) return -1;
    if (mode != "override" && mode != "additive") return -1;
    layers_.push_back({name, player, mask, 1.f, mode == "additive", true});
    return static_cast<int>(layers_.size()) - 1;
}

AnimLayerMixer::Layer* AnimLayerMixer::findLayer(const std::string& name) {
    for (auto& layer : layers_)
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

std::string AnimLayerMixer::getLayerName(int index) const {
    if (index < 0 || index >= getLayerCount()) return {};
    return layers_[static_cast<size_t>(index)].name;
}

void AnimLayerMixer::collectEvents(const std::string& layerName, AnimPlayer* player) {
    if (!player) return;
    for (int i = 0; i < player->getEventCount(); ++i)
        events_.push_back({layerName, player->getEventName(i), player->getEventPayload(i)});
}

void AnimLayerMixer::applyOverride(const Layer& layer) {
    AnimPose* layerPose = layer.player->getPose();
    for (int bone = 0; bone < pose_.getBoneCount(); ++bone) {
        const float weight = layerBoneWeight(layer.mask, layer.weight, bone);
        if (weight <= 0.f) continue;
        pose_.local(bone) = blendTRS(pose_.local(bone), layerPose->local(bone), weight);
    }
}

void AnimLayerMixer::applyAdditive(const Layer& layer) {
    AnimPose* layerPose = layer.player->getPose();
    for (int bone = 0; bone < pose_.getBoneCount(); ++bone) {
        const float weight = layerBoneWeight(layer.mask, layer.weight, bone);
        if (weight <= 0.f) continue;
        TransformTRS&       base      = pose_.local(bone);
        const TransformTRS& sample    = layerPose->local(bone);
        const TransformTRS& reference = skeleton_->bindLocal(bone);
        base.px += (sample.px - reference.px) * weight;
        base.py += (sample.py - reference.py) * weight;
        base.pz += (sample.pz - reference.pz) * weight;
        base.sx *= lerpf(1.f, std::fabs(reference.sx) > 1e-8f ? sample.sx / reference.sx : 1.f, weight);
        base.sy *= lerpf(1.f, std::fabs(reference.sy) > 1e-8f ? sample.sy / reference.sy : 1.f, weight);
        base.sz *= lerpf(1.f, std::fabs(reference.sz) > 1e-8f ? sample.sz / reference.sz : 1.f, weight);

        float dx, dy, dz, dw;
        multiplyQuat(sample.qx, sample.qy, sample.qz, sample.qw, -reference.qx, -reference.qy, -reference.qz,
                     reference.qw, dx, dy, dz, dw);
        float ax, ay, az, aw;
        slerpQuat(0.f, 0.f, 0.f, 1.f, dx, dy, dz, dw, weight, ax, ay, az, aw);
        float qx, qy, qz, qw;
        multiplyQuat(base.qx, base.qy, base.qz, base.qw, ax, ay, az, aw, qx, qy, qz, qw);
        base.qx = qx;
        base.qy = qy;
        base.qz = qz;
        base.qw = qw;
        base.normalizeRotation();
    }
}

void AnimLayerMixer::compose() {
    events_.clear();
    if (basePlayer_) {
        pose_.copyFrom(basePlayer_->getPose());
        collectEvents("base", basePlayer_);
    } else {
        skeleton_->applyBindPose(&pose_);
    }
    for (const Layer& layer : layers_) {
        if (!layer.enabled || !layer.player) continue;
        if (layer.weight <= 0.f) continue;
        collectEvents(layer.name, layer.player);
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

    // The mixer owns the evaluation boundary: all referenced players consume
    // the same injected step before their poses are combined.
    if (basePlayer_ && basePlayer_->hasCurrentTick() && step.tick <= basePlayer_->currentTick())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "animation mixer base player already consumed this tick"));
    for (const Layer& layer : layers_) {
        if (layer.enabled && layer.player && layer.player->hasCurrentTick() &&
            step.tick <= layer.player->currentTick())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "animation mixer layer player already consumed this tick"));
    }
    if (basePlayer_) {
        auto result = basePlayer_->advance(step);
        if (!result) return eve::Result<void>::failure(result.status());
    }
    for (const Layer& layer : layers_) {
        if (!layer.enabled || !layer.player) continue;
        auto result = layer.player->advance(step);
        if (!result) return eve::Result<void>::failure(result.status());
    }
    compose();
    lastTick_ = step.tick;
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
