#include "animation/MontageCoordinator.h"

#include "animation/AnimLayerMixer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::animation {
namespace {

template <class T>
Result<T> coordinatorError(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

MontageCoordinator::MontageCoordinator(AnimSkeleton& skeleton)
    : skeleton_(skeleton), composedPose_(std::make_unique<AnimPose>(skeleton.getBoneCount())),
      upperBodyOrientation_(skeleton) {
    skeleton_.applyBindPose(composedPose_.get());
}

void MontageCoordinator::ensureLayer(std::size_t layer) {
    while (layers_.size() <= layer) {
        Layer entry;
        entry.pose         = std::make_unique<AnimPose>(skeleton_.getBoneCount());
        entry.rawPose      = std::make_unique<AnimPose>(skeleton_.getBoneCount());
        entry.blendScratch = std::make_unique<AnimPose>(skeleton_.getBoneCount());
        skeleton_.applyBindPose(entry.pose.get());
        skeleton_.applyBindPose(entry.rawPose.get());
        skeleton_.applyBindPose(entry.blendScratch.get());
        layers_.push_back(std::move(entry));
    }
}

void MontageCoordinator::retireSlot(Slot& slot) noexcept {
    if (slot.player) slot.player->stop();
    slot.live = false;
    auto next = MontageHandle::nextGeneration(slot.generation);
    if (next)
        slot.generation = *next;
    else
        slot.retired = true;
}

Result<MontageHandle> MontageCoordinator::play(std::size_t layer, action::ActionTimeline timeline,
                                               std::vector<MontageClipAsset>&& clips,
                                               action::ActionExecutionId executionId, SimulationTick tick) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return Result<MontageHandle>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer"));
    ensureLayer(layer);
    Layer&            entry     = layers_[layer];
    const std::size_t nextIndex = 1U - entry.active;
    Slot&             next      = entry.slots[nextIndex];
    if (next.retired)
        return Result<MontageHandle>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "montage slot generation is exhausted", "slot"));
    if (next.player) retireSlot(next);
    if (next.retired)
        return Result<MontageHandle>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "montage slot generation is exhausted", "slot"));

    auto           candidate = std::make_unique<MontagePlayer>(skeleton_);
    if (entry.rootMotionReceiver) candidate->setRootMotionReceiver(*entry.rootMotionReceiver);
    if (entry.rootMotionPolicy) {
        auto applied = candidate->setRootMotionPolicy(*entry.rootMotionPolicy);
        if (!applied) return Result<MontageHandle>::failure(applied.status());
    }
    const Duration blendOut  = timeline.montage.defaultBlendIn;
    auto           prepared  = candidate->prepare(std::move(timeline), std::move(clips));
    if (!prepared) return Result<MontageHandle>::failure(prepared.status());
    next.player = std::move(candidate);
    auto started = next.player->play(executionId);
    if (!started) return Result<MontageHandle>::failure(started.status());
    Slot& previous = entry.slots[entry.active];
    if (previous.live && (previous.player->isPlaying() || previous.player->isFinished())) {
        auto fading = previous.player->beginBlendOut(blendOut, tick);
        if (!fading) return Result<MontageHandle>::failure(fading.status());
    }
    next.live    = true;
    entry.active = nextIndex;
    return Result<MontageHandle>::success(
        MontageHandle(static_cast<MontageHandle::index_type>(handleIndex(layer, nextIndex)), next.generation),
        Status::success(StatusCode::Applied));
}

MontageCoordinator::Slot* MontageCoordinator::slotFor(MontageHandle handle) noexcept {
    if (!handle.isValid()) return nullptr;
    const std::size_t layer = handle.index() / 2U;
    const std::size_t slot  = handle.index() % 2U;
    if (layer >= layers_.size()) return nullptr;
    Slot& candidate = layers_[layer].slots[slot];
    return candidate.live && candidate.player && candidate.generation == handle.generation() ? &candidate : nullptr;
}

const MontageCoordinator::Slot* MontageCoordinator::slotFor(MontageHandle handle) const noexcept {
    return const_cast<MontageCoordinator*>(this)->slotFor(handle);
}

Result<std::reference_wrapper<MontagePlayer>> MontageCoordinator::resolve(MontageHandle handle) {
    Slot* slot = slotFor(handle);
    if (!slot)
        return Result<std::reference_wrapper<MontagePlayer>>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "montage handle is invalid or stale", "handle"));
    return Result<std::reference_wrapper<MontagePlayer>>::success(std::ref(*slot->player));
}

Result<std::reference_wrapper<const MontagePlayer>> MontageCoordinator::resolve(MontageHandle handle) const {
    const Slot* slot = slotFor(handle);
    if (!slot)
        return Result<std::reference_wrapper<const MontagePlayer>>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "montage handle is invalid or stale", "handle"));
    return Result<std::reference_wrapper<const MontagePlayer>>::success(std::cref(*slot->player));
}

Result<MontageAdvance> MontageCoordinator::present(MontageHandle handle, const action::ActionAdvance& advance,
                                                   SimulationTick tick) {
    auto player = resolve(handle);
    if (!player) return Result<MontageAdvance>::failure(player.status());
    return player.value().get().present(advance, tick);
}

Result<MontageAdvance> MontageCoordinator::present(MontageHandle handle, action::ActionAdvance&& advance,
                                                   SimulationTick tick) {
    auto player = resolve(handle);
    if (!player) return Result<MontageAdvance>::failure(player.status());
    return player.value().get().present(std::move(advance), tick);
}

Result<void> MontageCoordinator::advanceBlendOuts(Duration delta, SimulationTick tick, MontageHandle except) {
    for (Layer& layer : layers_) {
        for (Slot& slot : layer.slots) {
            if (!slot.live || !slot.player || !slot.player->isBlendingOut()) continue;
            const MontageHandle handle(static_cast<MontageHandle::index_type>(&layer - layers_.data()) * 2U +
                                           static_cast<MontageHandle::index_type>(&slot - layer.slots),
                                       slot.generation);
            if (handle == except) continue;
            auto advanced = slot.player->advanceBlendOut(delta, tick);
            if (!advanced) return Result<void>::failure(advanced.status());
        }
    }
    collectFinished();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<MontageAdvance> MontageCoordinator::stop(MontageHandle handle, Duration blendOut, SimulationTick tick) {
    auto player = resolve(handle);
    if (!player) return Result<MontageAdvance>::failure(player.status());
    return player.value().get().beginBlendOut(blendOut, tick);
}

Result<void> MontageCoordinator::setLayerBoneMask(std::size_t layer, std::vector<float> weights) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer"));
    if (weights.size() != static_cast<std::size_t>(skeleton_.getBoneCount()))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "montage layer mask must contain one weight per skeleton bone",
                                                       "weights"));
    if (std::any_of(weights.begin(), weights.end(), [](float weight) {
            return !std::isfinite(weight) || weight < 0.0f || weight > 1.0f;
        }))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "montage layer mask weights must be finite and within [0,1]", "weights"));
    ensureLayer(layer);
    layers_[layer].boneMask       = std::move(weights);
    layers_[layer].boneMaskSource = MontageBoneMaskSource::Custom;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontageCoordinator::setLayerBoneMask(std::size_t layer, const AnimBoneMask& mask) {
    if (mask.getSkeleton() != &skeleton_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "montage layer mask belongs to a different skeleton", "mask"));
    std::vector<float> weights;
    weights.reserve(static_cast<std::size_t>(mask.getBoneCount()));
    for (int bone = 0; bone < mask.getBoneCount(); ++bone) weights.push_back(mask.getBoneWeight(bone));
    return setLayerBoneMask(layer, std::move(weights));
}

Result<void> MontageCoordinator::clearLayerBoneMask(std::size_t layer) {
    if (layer >= layers_.size())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "montage layer does not exist", "layer"));
    if (layers_[layer].boneMask.empty()) return Result<void>::success(Status::success(StatusCode::NoOp));
    layers_[layer].boneMask.clear();
    layers_[layer].boneMaskSource = MontageBoneMaskSource::None;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontageCoordinator::setLayerProceduralUpperBodyMask(std::size_t layer, const AnimBoneMask& mask) {
    auto configured = setLayerBoneMask(layer, mask);
    if (!configured) return configured;
    layers_[layer].boneMaskSource = MontageBoneMaskSource::ProceduralUpperBody;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<MontageBoneMaskSource> MontageCoordinator::layerBoneMaskSource(std::size_t layer) const {
    if (layer >= layers_.size())
        return coordinatorError<MontageBoneMaskSource>(DiagnosticCode::NotFound, "montage layer does not exist",
                                                       "layer");
    return Result<MontageBoneMaskSource>::success(layers_[layer].boneMaskSource);
}

Result<void> MontageCoordinator::setLayerRole(std::size_t layer, MontageLayerRole role) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return coordinatorError<void>(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer");
    if (role == MontageLayerRole::UpperBody || role == MontageLayerRole::FullBody) {
        for (std::size_t index = 0; index < layers_.size(); ++index) {
            if (index != layer && layers_[index].role == role)
                return coordinatorError<void>(DiagnosticCode::Conflict,
                                              "montage coordinator already has a layer with this unique role",
                                              "role");
        }
    }
    ensureLayer(layer);
    if (layers_[layer].role == role) return Result<void>::success(Status::success(StatusCode::NoOp));
    layers_[layer].role = role;
    if (role == MontageLayerRole::Additive)
        layers_[layer].additive = true;
    else if (role == MontageLayerRole::UpperBody || role == MontageLayerRole::FullBody)
        layers_[layer].additive = false;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<MontageLayerRole> MontageCoordinator::layerRole(std::size_t layer) const {
    if (layer >= layers_.size())
        return coordinatorError<MontageLayerRole>(DiagnosticCode::NotFound, "montage layer does not exist", "layer");
    return Result<MontageLayerRole>::success(layers_[layer].role);
}

Result<void> MontageCoordinator::configureUpperBodyOrientation(const UpperBodyOrientationProfile& profile) {
    auto configured = upperBodyOrientation_.configure(profile);
    if (!configured) return configured;
    upperBodyOrientationStatus_ = UpperBodyOrientationStatus::Inactive;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void MontageCoordinator::clearUpperBodyOrientation() noexcept {
    upperBodyOrientation_.clear();
    upperBodyOrientationStatus_ = UpperBodyOrientationStatus::Unconfigured;
}

Result<std::vector<float>> MontageCoordinator::layerBoneMask(std::size_t layer) const {
    if (layer >= layers_.size())
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "montage layer does not exist", "layer"));
    return Result<std::vector<float>>::success(layers_[layer].boneMask);
}

Result<void> MontageCoordinator::setLayerWeight(std::size_t layer, float weight) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer"));
    if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "montage layer weight must be finite and within [0,1]", "weight"));
    ensureLayer(layer);
    if (layers_[layer].weight == weight) return Result<void>::success(Status::success(StatusCode::NoOp));
    layers_[layer].weight = weight;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<float> MontageCoordinator::layerWeight(std::size_t layer) const {
    if (layer >= layers_.size())
        return Result<float>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "montage layer does not exist", "layer"));
    return Result<float>::success(layers_[layer].weight);
}

Result<void> MontageCoordinator::setLayerAdditive(std::size_t layer, bool additive) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer"));
    ensureLayer(layer);
    if (layers_[layer].additive == additive) return Result<void>::success(Status::success(StatusCode::NoOp));
    layers_[layer].additive = additive;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<bool> MontageCoordinator::layerAdditive(std::size_t layer) const {
    if (layer >= layers_.size())
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "montage layer does not exist", "layer"));
    return Result<bool>::success(layers_[layer].additive);
}

Result<void> MontageCoordinator::setLayerRootMotionReceiver(std::size_t layer,
                                                            IMontageRootMotionReceiver& receiver) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer"));
    ensureLayer(layer);
    Layer& entry = layers_[layer];
    entry.rootMotionReceiver = &receiver;
    for (Slot& slot : entry.slots)
        if (slot.player) slot.player->setRootMotionReceiver(receiver);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontageCoordinator::clearLayerRootMotionReceiver(std::size_t layer) {
    if (layer >= layers_.size())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "montage layer does not exist", "layer"));
    Layer& entry = layers_[layer];
    if (!entry.rootMotionReceiver) return Result<void>::success(Status::success(StatusCode::NoOp));
    for (Slot& slot : entry.slots)
        if (slot.player) slot.player->clearRootMotionReceiver();
    entry.rootMotionReceiver = nullptr;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontageCoordinator::setLayerRootMotionPolicy(std::size_t layer, const RootMotionPolicy& policy) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer"));
    auto validated = validateRootMotionPolicy(policy);
    if (!validated) return validated;
    ensureLayer(layer);
    Layer& entry = layers_[layer];
    for (Slot& slot : entry.slots) {
        if (!slot.player) continue;
        auto applied = slot.player->setRootMotionPolicy(policy);
        if (!applied) return applied;
    }
    entry.rootMotionPolicy = policy;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontageCoordinator::clearLayerRootMotionPolicy(std::size_t layer) {
    if (layer >= layers_.size())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "montage layer does not exist", "layer"));
    Layer& entry = layers_[layer];
    if (!entry.rootMotionPolicy) return Result<void>::success(Status::success(StatusCode::NoOp));
    entry.rootMotionPolicy.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

double MontageCoordinator::evaluateLayerPose(Layer& layer) const {
    const Slot* first  = nullptr;
    const Slot* second = nullptr;
    double      total  = 0.0;
    for (const Slot& slot : layer.slots) {
        if (!slot.live || !slot.player || slot.player->weight() <= 0.0) continue;
        total += slot.player->weight();
        if (!first) first = &slot;
        else second = &slot;
    }
    if (!first) {
        skeleton_.applyBindPose(layer.rawPose.get());
        return 0.0;
    }

    layer.rawPose->copyFrom(&first->player->pose());
    if (second) {
        if (total > 0.0) {
            layer.blendScratch->copyFrom(layer.rawPose.get());
            layer.rawPose->blendFrom(layer.blendScratch.get(), &second->player->pose(),
                                     static_cast<float>(second->player->weight() / total));
        }
    }
    return total;
}

Result<std::reference_wrapper<AnimPose>> MontageCoordinator::pose(std::size_t layerIndex) {
    if (layerIndex >= layers_.size())
        return Result<std::reference_wrapper<AnimPose>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "montage layer does not exist", "layer"));
    Layer& layer = layers_[layerIndex];
    (void)evaluateLayerPose(layer);
    skeleton_.applyBindPose(layer.pose.get());
    if (layer.boneMask.empty()) {
        layer.pose->copyFrom(layer.rawPose.get());
    } else {
        for (int bone = 0; bone < layer.pose->getBoneCount(); ++bone) {
            const float weight = layer.boneMask[static_cast<std::size_t>(bone)];
            layer.pose->local(bone) = blendTRS(layer.pose->local(bone), layer.rawPose->local(bone), weight);
        }
    }
    return Result<std::reference_wrapper<AnimPose>>::success(std::ref(*layer.pose));
}

Result<std::reference_wrapper<AnimPose>> MontageCoordinator::compose(const AnimPose& basePose) {
    if (basePose.getBoneCount() != skeleton_.getBoneCount())
        return Result<std::reference_wrapper<AnimPose>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "montage base pose must match the coordinator skeleton", "basePose"));
    composedPose_->copyFrom(&basePose);
    upperBodyOrientationStatus_ = upperBodyOrientation_.isConfigured() ? UpperBodyOrientationStatus::Inactive
                                                                       : UpperBodyOrientationStatus::Unconfigured;
    Layer* upperBody = nullptr;
    float upperBodyWeight = 0.0f;
    bool fullBodyDominant = false;
    for (Layer& layer : layers_) {
        const double rawWeight   = evaluateLayerPose(layer);
        const float  layerWeight = clampf(static_cast<float>(rawWeight), 0.0f, 1.0f) * layer.weight;
        if (layerWeight <= 0.0f) continue;
        if (!layer.additive && layer.role == MontageLayerRole::FullBody) fullBodyDominant = true;
        if (!layer.additive && layer.role == MontageLayerRole::UpperBody) {
            upperBody = &layer;
            upperBodyWeight = layerWeight;
        }
        if (layer.additive) continue;
        for (int bone = 0; bone < composedPose_->getBoneCount(); ++bone) {
            const float maskWeight = layer.boneMask.empty() ? 1.0f : layer.boneMask[static_cast<std::size_t>(bone)];
            const float weight     = clampf(layerWeight * maskWeight, 0.0f, 1.0f);
            if (weight <= 0.0f) continue;
            composedPose_->local(bone) = blendTRS(composedPose_->local(bone), layer.rawPose->local(bone), weight);
        }
    }
    if (upperBody && upperBodyOrientation_.isConfigured()) {
        if (fullBodyDominant) {
            upperBodyOrientationStatus_ = UpperBodyOrientationStatus::BypassedFullBody;
        } else if (upperBody->boneMaskSource == MontageBoneMaskSource::Custom) {
            upperBodyOrientationStatus_ = UpperBodyOrientationStatus::BypassedCustomMask;
        } else {
            auto corrected = upperBodyOrientation_.apply(*composedPose_, *upperBody->rawPose, upperBodyWeight);
            if (!corrected) return Result<std::reference_wrapper<AnimPose>>::failure(corrected.status());
            upperBodyOrientationStatus_ = corrected.status().code() == StatusCode::NoOp
                                              ? UpperBodyOrientationStatus::Inactive
                                              : UpperBodyOrientationStatus::Applied;
        }
    }
    for (Layer& layer : layers_) {
        if (!layer.additive) continue;
        const double rawWeight   = evaluateLayerPose(layer);
        const float  layerWeight = clampf(static_cast<float>(rawWeight), 0.0f, 1.0f) * layer.weight;
        if (layerWeight <= 0.0f) continue;
        for (int bone = 0; bone < composedPose_->getBoneCount(); ++bone) {
            const float maskWeight = layer.boneMask.empty() ? 1.0f : layer.boneMask[static_cast<std::size_t>(bone)];
            const float weight     = clampf(layerWeight * maskWeight, 0.0f, 1.0f);
            if (weight <= 0.0f) continue;
            applyAdditiveTRS(composedPose_->local(bone), layer.rawPose->local(bone), skeleton_.bindLocal(bone),
                             weight);
        }
    }
    composedPose_->computeWorld(&skeleton_);
    return Result<std::reference_wrapper<AnimPose>>::success(std::ref(*composedPose_));
}

void MontageCoordinator::collectFinished() noexcept {
    for (Layer& layer : layers_)
        for (Slot& slot : layer.slots)
            if (slot.live && slot.player && !slot.player->isPlaying() && !slot.player->isBlendingOut() &&
                slot.player->weight() <= 0.0)
                retireSlot(slot);
}

}  // namespace eve::animation
