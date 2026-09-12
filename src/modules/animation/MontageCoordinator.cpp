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

MontageCoordinator::MontageCoordinator(AnimSkeleton& skeleton) : skeleton_(skeleton) {}

void MontageCoordinator::ensureLayer(std::size_t layer) {
    while (layers_.size() <= layer) {
        Layer entry;
        entry.pose = std::make_unique<AnimPose>(skeleton_.getBoneCount());
        skeleton_.applyBindPose(entry.pose.get());
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
    if (previous.player && previous.player->isPlaying()) {
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

Result<std::reference_wrapper<AnimPose>> MontageCoordinator::pose(std::size_t layerIndex) {
    if (layerIndex >= layers_.size())
        return coordinatorError<std::reference_wrapper<AnimPose>>(DiagnosticCode::NotFound,
                                                                  "montage layer does not exist", "layer");
    Layer& layer = layers_[layerIndex];
    skeleton_.applyBindPose(layer.pose.get());
    for (const Slot& slot : layer.slots) {
        if (!slot.player || slot.player->weight() <= 0.0) continue;
        AnimPose base;
        base.copyFrom(layer.pose.get());
        if (layer.boneMask.empty()) {
            layer.pose->blendFrom(&base, &slot.player->pose(), static_cast<float>(slot.player->weight()));
            continue;
        }
        for (int bone = 0; bone < layer.pose->getBoneCount(); ++bone) {
            const float weight = static_cast<float>(slot.player->weight()) *
                                 layer.boneMask[static_cast<std::size_t>(bone)];
            layer.pose->local(bone) = blendTRS(base.local(bone), slot.player->pose().local(bone), weight);
        }
    }
    return Result<std::reference_wrapper<AnimPose>>::success(std::ref(*layer.pose));
}

void MontageCoordinator::collectFinished() noexcept {
    for (Layer& layer : layers_)
        for (Slot& slot : layer.slots)
            if (slot.player && !slot.player->isPlaying()) retireSlot(slot);
}

}  // namespace eve::animation
