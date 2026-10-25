#include "physics/backend/bake/collapse/CollapsePlayback.h"
#include <new>
#include "physics/backend/bake/RigidPoseTrack.h"
#include "physics/backend/bake/collapse/CollapseAnimationInternal.h"

namespace eve::physics {
CollapsePlayback::CollapsePlayback(std::shared_ptr<const CollapseAnimation> animation)
    : animation_(std::move(animation)) {}
Result<CollapsePlayback> CollapsePlayback::prepare(CollapseAnimation animation) try {
    if (animation.frameCount() == 0)
        return Result<CollapsePlayback>::failure(collapse_detail::invalid("cannot play an empty recording"));
    return Result<CollapsePlayback>::success(
        CollapsePlayback(std::make_shared<const CollapseAnimation>(std::move(animation))));
} catch (const std::bad_alloc&) {
    return Result<CollapsePlayback>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "collapse playback allocation failed", "collapsePlayback"));
}
Result<void>      CollapsePlayback::advance(double seconds) { return clock_.advance(seconds, animation_->duration()); }
Result<void>      CollapsePlayback::seek(double seconds) { return clock_.seek(seconds, animation_->duration()); }
Result<void>      CollapsePlayback::setRate(double rate) { return clock_.setRate(rate); }
void              CollapsePlayback::setPaused(bool paused) noexcept { clock_.setPaused(paused); }
Result<void>      CollapsePlayback::setWrap(CollapsePlaybackWrap wrap) { return clock_.setWrap(wrap); }
Result<void>      CollapsePlayback::setPlacement(CollapsePlacement placement) { return transform_.set(placement); }
CollapsePlacement CollapsePlayback::placement() const noexcept { return transform_.placement(); }
double            CollapsePlayback::time() const noexcept { return clock_.time(); }
double            CollapsePlayback::rate() const noexcept { return clock_.rate(); }
bool              CollapsePlayback::paused() const noexcept { return clock_.paused(); }
Result<BonePresentation> CollapsePlayback::sample(int bone) const {
    auto sampled = animation_->sample(static_cast<float>(clock_.time()), bone);
    if (!sampled) return sampled;
    auto pose = sampled.value();
    auto transformed =
        transform_.apply({pose.worldX, pose.worldY, pose.worldZ, pose.rotX, pose.rotY, pose.rotZ, pose.rotW});
    if (!transformed) return Result<BonePresentation>::failure(transformed.status());
    const auto placed = transformed.value();
    pose.worldX       = placed.x;
    pose.worldY       = placed.y;
    pose.worldZ       = placed.z;
    pose.rotX         = placed.qx;
    pose.rotY         = placed.qy;
    pose.rotZ         = placed.qz;
    pose.rotW         = placed.qw;
    return Result<BonePresentation>::success(pose);
}

std::shared_ptr<const CollapseAnimation> CollapsePlayback::animation() const noexcept { return animation_; }
}  // namespace eve::physics
