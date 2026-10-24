#include "physics/bake/RigidPlayback.h"
#include <utility>
namespace eve::physics {
RigidPlayback::RigidPlayback(RigidPoseTrack track) : track_(std::move(track)) {}
Result<void> RigidPlayback::advance(double seconds) { return clock_.advance(seconds, track_.duration()); }
Result<void> RigidPlayback::seek(double seconds) { return clock_.seek(seconds, track_.duration()); }
Result<void> RigidPlayback::setRate(double rate) { return clock_.setRate(rate); }
Result<void> RigidPlayback::setWrap(BakePlaybackWrap wrap) { return clock_.setWrap(wrap); }
Result<void> RigidPlayback::setPlacement(BakeRigidPlacement placement) {
    if (track_.space() == RigidTrackSpace::PlanarPixels &&
        (placement.z != 0 || placement.rotationX != 0 || placement.rotationY != 0))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "planar playback placement leaves the XY plane", "rigidPlayback"));
    return transform_.set(placement);
}
BakeRigidPlacement     RigidPlayback::placement() const noexcept { return transform_.placement(); }
void                   RigidPlayback::setPaused(bool paused) noexcept { clock_.setPaused(paused); }
double                 RigidPlayback::time() const noexcept { return clock_.time(); }
double                 RigidPlayback::rate() const noexcept { return clock_.rate(); }
bool                   RigidPlayback::paused() const noexcept { return clock_.paused(); }
float                  RigidPlayback::duration() const noexcept { return track_.duration(); }
RigidPoseTrack         RigidPlayback::track() const { return track_; }
Result<RigidTrackPose> RigidPlayback::sample(PersistentId id) const {
    auto sampled = track_.sample(static_cast<float>(clock_.time()), id);
    if (!sampled) return sampled;
    return transform_.apply(sampled.value());
}
}  // namespace eve::physics
