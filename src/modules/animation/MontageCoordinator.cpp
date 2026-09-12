#include "animation/MontageCoordinator.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <algorithm>
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
        layer.pose->blendFrom(&base, &slot.player->pose(), static_cast<float>(slot.player->weight()));
    }
    return Result<std::reference_wrapper<AnimPose>>::success(std::ref(*layer.pose));
}

void MontageCoordinator::collectFinished() noexcept {
    for (Layer& layer : layers_)
        for (Slot& slot : layer.slots)
            if (slot.player && !slot.player->isPlaying()) retireSlot(slot);
}

}  // namespace eve::animation
