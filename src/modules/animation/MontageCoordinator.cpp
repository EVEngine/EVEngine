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
    : skeleton_(skeleton), composedPose_(std::make_unique<AnimPose>(skeleton.getBoneCount())) {
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
    slot.player.reset();
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
        return coordinatorError<MontageHandle>(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer");
    ensureLayer(layer);
    Layer&            entry     = layers_[layer];
    const std::size_t nextIndex = 1U - entry.active;
    Slot&             next      = entry.slots[nextIndex];
    if (next.retired)
        return coordinatorError<MontageHandle>(DiagnosticCode::Failed, "montage slot generation is exhausted", "slot");
    if (next.player) retireSlot(next);
    if (next.retired)
        return coordinatorError<MontageHandle>(DiagnosticCode::Failed, "montage slot generation is exhausted", "slot");

    auto           candidate = std::make_unique<MontagePlayer>(skeleton_);
    if (entry.rootMotionReceiver) candidate->setRootMotionReceiver(*entry.rootMotionReceiver);
    const Duration blendOut  = timeline.montage.defaultBlendIn;
    auto           prepared  = candidate->prepare(std::move(timeline), std::move(clips));
    if (!prepared) return Result<MontageHandle>::failure(prepared.status());
    auto started = candidate->play(executionId);
    if (!started) return Result<MontageHandle>::failure(started.status());
    Slot& previous = entry.slots[entry.active];
    if (previous.player && (previous.player->isPlaying() || previous.player->isFinished())) {
        auto fading = previous.player->beginBlendOut(blendOut, tick);
        if (!fading) return Result<MontageHandle>::failure(fading.status());
    }
    next.player  = std::move(candidate);
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
    return candidate.player && candidate.generation == handle.generation() ? &candidate : nullptr;
}

const MontageCoordinator::Slot* MontageCoordinator::slotFor(MontageHandle handle) const noexcept {
    return const_cast<MontageCoordinator*>(this)->slotFor(handle);
}

Result<std::reference_wrapper<MontagePlayer>> MontageCoordinator::resolve(MontageHandle handle) {
    Slot* slot = slotFor(handle);
    if (!slot)
        return coordinatorError<std::reference_wrapper<MontagePlayer>>(DiagnosticCode::StaleHandle,
                                                                       "montage handle is invalid or stale", "handle");
    return Result<std::reference_wrapper<MontagePlayer>>::success(std::ref(*slot->player));
}

Result<std::reference_wrapper<const MontagePlayer>> MontageCoordinator::resolve(MontageHandle handle) const {
    const Slot* slot = slotFor(handle);
    if (!slot)
        return coordinatorError<std::reference_wrapper<const MontagePlayer>>(
            DiagnosticCode::StaleHandle, "montage handle is invalid or stale", "handle");
    return Result<std::reference_wrapper<const MontagePlayer>>::success(std::cref(*slot->player));
}

Result<MontageAdvance> MontageCoordinator::present(MontageHandle handle, const action::ActionAdvance& advance,
                                                   SimulationTick tick) {
    auto player = resolve(handle);
    if (!player) return Result<MontageAdvance>::failure(player.status());
    return player.value().get().present(advance, tick);
}

Result<void> MontageCoordinator::advanceBlendOuts(Duration delta, SimulationTick tick, MontageHandle except) {
    for (Layer& layer : layers_) {
        for (Slot& slot : layer.slots) {
            if (!slot.player || !slot.player->isBlendingOut()) continue;
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
        return coordinatorError<void>(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer");
    if (weights.size() != static_cast<std::size_t>(skeleton_.getBoneCount()))
        return coordinatorError<void>(DiagnosticCode::InvalidArgument,
                                      "montage layer mask must contain one weight per skeleton bone", "weights");
    if (std::any_of(weights.begin(), weights.end(), [](float weight) {
            return !std::isfinite(weight) || weight < 0.0f || weight > 1.0f;
        }))
        return coordinatorError<void>(DiagnosticCode::InvalidArgument,
                                      "montage layer mask weights must be finite and within [0,1]", "weights");
    ensureLayer(layer);
    layers_[layer].boneMask = std::move(weights);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontageCoordinator::setLayerBoneMask(std::size_t layer, const AnimBoneMask& mask) {
    if (mask.getSkeleton() != &skeleton_)
        return coordinatorError<void>(DiagnosticCode::InvalidArgument,
                                      "montage layer mask belongs to a different skeleton", "mask");
    std::vector<float> weights;
    weights.reserve(static_cast<std::size_t>(mask.getBoneCount()));
    for (int bone = 0; bone < mask.getBoneCount(); ++bone) weights.push_back(mask.getBoneWeight(bone));
    return setLayerBoneMask(layer, std::move(weights));
}

Result<void> MontageCoordinator::clearLayerBoneMask(std::size_t layer) {
    if (layer >= layers_.size())
        return coordinatorError<void>(DiagnosticCode::NotFound, "montage layer does not exist", "layer");
    if (layers_[layer].boneMask.empty()) return Result<void>::success(Status::success(StatusCode::NoOp));
    layers_[layer].boneMask.clear();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::vector<float>> MontageCoordinator::layerBoneMask(std::size_t layer) const {
    if (layer >= layers_.size())
        return coordinatorError<std::vector<float>>(DiagnosticCode::NotFound,
                                                    "montage layer does not exist", "layer");
    return Result<std::vector<float>>::success(layers_[layer].boneMask);
}

Result<void> MontageCoordinator::setLayerWeight(std::size_t layer, float weight) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return coordinatorError<void>(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer");
    if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f)
        return coordinatorError<void>(DiagnosticCode::InvalidArgument,
                                      "montage layer weight must be finite and within [0,1]", "weight");
    ensureLayer(layer);
    if (layers_[layer].weight == weight) return Result<void>::success(Status::success(StatusCode::NoOp));
    layers_[layer].weight = weight;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<float> MontageCoordinator::layerWeight(std::size_t layer) const {
    if (layer >= layers_.size())
        return coordinatorError<float>(DiagnosticCode::NotFound, "montage layer does not exist", "layer");
    return Result<float>::success(layers_[layer].weight);
}

Result<void> MontageCoordinator::setLayerAdditive(std::size_t layer, bool additive) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return coordinatorError<void>(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer");
    ensureLayer(layer);
    if (layers_[layer].additive == additive) return Result<void>::success(Status::success(StatusCode::NoOp));
    layers_[layer].additive = additive;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<bool> MontageCoordinator::layerAdditive(std::size_t layer) const {
    if (layer >= layers_.size())
        return coordinatorError<bool>(DiagnosticCode::NotFound, "montage layer does not exist", "layer");
    return Result<bool>::success(layers_[layer].additive);
}

Result<void> MontageCoordinator::setLayerRootMotionReceiver(std::size_t layer,
                                                            IMontageRootMotionReceiver& receiver) {
    if (layer > static_cast<std::size_t>(std::numeric_limits<MontageHandle::index_type>::max() / 2U))
        return coordinatorError<void>(DiagnosticCode::InvalidArgument, "montage layer is too large", "layer");
    ensureLayer(layer);
    Layer& entry = layers_[layer];
    entry.rootMotionReceiver = &receiver;
    for (Slot& slot : entry.slots)
        if (slot.player) slot.player->setRootMotionReceiver(receiver);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontageCoordinator::clearLayerRootMotionReceiver(std::size_t layer) {
    if (layer >= layers_.size())
        return coordinatorError<void>(DiagnosticCode::NotFound, "montage layer does not exist", "layer");
    Layer& entry = layers_[layer];
    if (!entry.rootMotionReceiver) return Result<void>::success(Status::success(StatusCode::NoOp));
    for (Slot& slot : entry.slots)
        if (slot.player) slot.player->clearRootMotionReceiver();
    entry.rootMotionReceiver = nullptr;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

double MontageCoordinator::evaluateLayerPose(Layer& layer) const {
    const Slot* first  = nullptr;
    const Slot* second = nullptr;
    double      total  = 0.0;
    for (const Slot& slot : layer.slots) {
        if (!slot.player || slot.player->weight() <= 0.0) continue;
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
        return coordinatorError<std::reference_wrapper<AnimPose>>(DiagnosticCode::NotFound,
                                                                  "montage layer does not exist", "layer");
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
        return coordinatorError<std::reference_wrapper<AnimPose>>(
            DiagnosticCode::InvalidArgument, "montage base pose must match the coordinator skeleton", "basePose");
    composedPose_->copyFrom(&basePose);
    for (Layer& layer : layers_) {
        const double rawWeight   = evaluateLayerPose(layer);
        const float  layerWeight = clampf(static_cast<float>(rawWeight), 0.0f, 1.0f) * layer.weight;
        if (layerWeight <= 0.0f) continue;
        for (int bone = 0; bone < composedPose_->getBoneCount(); ++bone) {
            const float maskWeight = layer.boneMask.empty() ? 1.0f : layer.boneMask[static_cast<std::size_t>(bone)];
            const float weight     = clampf(layerWeight * maskWeight, 0.0f, 1.0f);
            if (weight <= 0.0f) continue;
            if (layer.additive) {
                applyAdditiveTRS(composedPose_->local(bone), layer.rawPose->local(bone), skeleton_.bindLocal(bone),
                                 weight);
            } else {
                composedPose_->local(bone) = blendTRS(composedPose_->local(bone), layer.rawPose->local(bone), weight);
            }
        }
    }
    composedPose_->computeWorld(&skeleton_);
    return Result<std::reference_wrapper<AnimPose>>::success(std::ref(*composedPose_));
}

void MontageCoordinator::collectFinished() noexcept {
    for (Layer& layer : layers_)
        for (Slot& slot : layer.slots)
            if (slot.player && !slot.player->isPlaying() && !slot.player->isBlendingOut() &&
                slot.player->weight() <= 0.0)
                retireSlot(slot);
}

}  // namespace eve::animation
