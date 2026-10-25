// eve-hot-path
#include "physics/backend/bake/DeformPlayback.h"
#include <algorithm>
#include <new>
#include <utility>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/RigidPoseTrack.h"
namespace eve::physics {
DeformPlayback::DeformPlayback(DeformTrack track, std::vector<DeformPoint> scratch)
    : track_(std::move(track)), scratch_(std::move(scratch)) {}
Result<DeformPlayback> DeformPlayback::prepare(DeformTrack track) {
    try {
        std::vector<DeformPoint> scratch(track.topology().vertexCount());
        return Result<DeformPlayback>::success(DeformPlayback(std::move(track), std::move(scratch)));
    } catch (const std::bad_alloc&) {
        return Result<DeformPlayback>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "deformation player buffer allocation failed", "deformPlayback"));
    }
}
Result<void> DeformPlayback::advance(double seconds) { return clock_.advance(seconds, track_.duration()); }
Result<void> DeformPlayback::seek(double seconds) { return clock_.seek(seconds, track_.duration()); }
Result<void> DeformPlayback::setRate(double rate) { return clock_.setRate(rate); }
Result<void> DeformPlayback::setWrap(BakePlaybackWrap wrap) { return clock_.setWrap(wrap); }
void         DeformPlayback::setPaused(bool paused) noexcept { clock_.setPaused(paused); }
Result<void> DeformPlayback::setPlacement(BakeRigidPlacement placement) {
    if (track_.recipe()->coordinates() == BakeCoordinates::PlanarPixels &&
        (placement.z != 0 || placement.rotationX != 0 || placement.rotationY != 0))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "planar deformation placement leaves XY plane", "deformPlayback"));
    return transform_.set(placement);
}
BakeRigidPlacement DeformPlayback::placement() const noexcept { return transform_.placement(); }
double             DeformPlayback::time() const noexcept { return clock_.time(); }
double             DeformPlayback::rate() const noexcept { return clock_.rate(); }
bool               DeformPlayback::paused() const noexcept { return clock_.paused(); }
DeformTrack        DeformPlayback::track() const { return track_; }
Result<void>       DeformPlayback::sample(std::span<DeformPoint> output) {
    if (output.size() != scratch_.size())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                             "incorrect deformation output extent", "deformPlayback"));
    auto sampled = track_.sample(static_cast<float>(clock_.time()), scratch_);
    if (!sampled) return sampled;
    for (auto& point : scratch_) {
        auto placed = transform_.apply({point.x, point.y, point.z, 0, 0, 0, 1});
        if (!placed) return Result<void>::failure(placed.status());
        const auto value = placed.value();
        point            = {value.x, value.y, value.z};
    }
    std::copy(scratch_.begin(), scratch_.end(), output.begin());
    return Result<void>::success();
}
}  // namespace eve::physics
