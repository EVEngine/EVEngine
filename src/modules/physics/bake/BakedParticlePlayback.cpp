#include "physics/bake/BakedParticlePlayback.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/RigidPoseTrack.h"
namespace eve::physics {
BakedParticlePlayback::BakedParticlePlayback(ParticleTrack track, std::vector<BakedParticleSample> scratch)
    : track_(std::move(track)), scratch_(std::move(scratch)) {}
Result<BakedParticlePlayback> BakedParticlePlayback::prepare(ParticleTrack track) {
    try {
        std::vector<BakedParticleSample> scratch(track.capacity());
        return Result<BakedParticlePlayback>::success(BakedParticlePlayback(std::move(track), std::move(scratch)));
    } catch (const std::bad_alloc&) {
        return Result<BakedParticlePlayback>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "particle player buffer allocation failed", "bakedParticlePlayback"));
    }
}
Result<void> BakedParticlePlayback::advance(double seconds) { return clock_.advance(seconds, track_.duration()); }
Result<void> BakedParticlePlayback::seek(double seconds) { return clock_.seek(seconds, track_.duration()); }
Result<void> BakedParticlePlayback::setRate(double rate) { return clock_.setRate(rate); }
Result<void> BakedParticlePlayback::setWrap(BakePlaybackWrap wrap) { return clock_.setWrap(wrap); }
void         BakedParticlePlayback::setPaused(bool paused) noexcept { clock_.setPaused(paused); }
Result<void> BakedParticlePlayback::setPlacement(BakeRigidPlacement placement) {
    if (track_.recipe()->coordinates() == BakeCoordinates::PlanarPixels &&
        (placement.z != 0 || placement.rotationX != 0 || placement.rotationY != 0))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "planar particle placement leaves XY plane", "bakedParticlePlayback"));
    return transform_.set(placement);
}
BakeRigidPlacement  BakedParticlePlayback::placement() const noexcept { return transform_.placement(); }
double              BakedParticlePlayback::time() const noexcept { return clock_.time(); }
double              BakedParticlePlayback::rate() const noexcept { return clock_.rate(); }
bool                BakedParticlePlayback::paused() const noexcept { return clock_.paused(); }
ParticleTrack       BakedParticlePlayback::track() const { return track_; }
Result<std::size_t> BakedParticlePlayback::sample(std::span<BakedParticleSample> output) {
    if (output.size() < scratch_.size())
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "insufficient particle output extent", "bakedParticlePlayback"));
    auto sampled = track_.sample(static_cast<float>(clock_.time()), scratch_);
    if (!sampled) return Result<std::size_t>::failure(sampled.status());
    const auto       count = sampled.value();
    const auto       p     = transform_.placement();
    const double     angle = 2 * std::atan2(double(p.rotationZ), double(p.rotationW));
    const double     c = std::cos(angle), s = std::sin(angle);
    constexpr double radiansToDegrees = 180. / 3.14159265358979323846;
    for (std::size_t i = 0; i < count; ++i) {
        auto&        value    = scratch_[i].pose;
        const double cx       = double(value.x) + double(value.w) * .5;
        const double cy       = double(value.y) + double(value.h) * .5;
        const double x        = c * cx - s * cy + p.x - double(value.w) * .5;
        const double y        = s * cx + c * cy + p.y - double(value.h) * .5;
        const double rotation = double(value.rotation) + angle * radiansToDegrees;
        for (double candidate : {x, y, rotation})
            if (!std::isfinite(candidate) || std::abs(candidate) > std::numeric_limits<float>::max())
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "particle placement overflow", "bakedParticlePlayback"));
        value.x        = static_cast<float>(x);
        value.y        = static_cast<float>(y);
        value.rotation = static_cast<float>(rotation);
    }
    std::copy_n(scratch_.begin(), count, output.begin());
    return Result<std::size_t>::success(count);
}
}  // namespace eve::physics
