// eve-hot-path
#include <algorithm>
#include <cmath>
#include <utility>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/ParticleTrackInternal.h"
namespace eve::physics {
namespace {
Diagnostic invalidParticle(const char* text) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, text, "particleTrack");
}
bool finitePose(const BakedParticlePose& p) {
    for (float v : {p.x, p.y, p.w, p.h, p.rotation, p.r, p.g, p.b, p.a, p.u0, p.v0, p.u1, p.v1})
        if (!std::isfinite(v)) return false;
    return true;
}
}  // namespace
ParticleTrack::ParticleTrack(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
Result<ParticleTrack> ParticleTrack::prepare(float duration, std::vector<ParticleTrajectory> trajectories,
                                             const BakeRecipe& recipe) {
    auto encoded = recipe.encode();
    if (!encoded) return Result<ParticleTrack>::failure(encoded.status());
    if (!std::isfinite(duration) || duration < 0 || trajectories.size() > 65536 ||
        recipe.coordinates() != BakeCoordinates::PlanarPixels)
        return Result<ParticleTrack>::failure(invalidParticle("invalid duration, birth budget or coordinates"));
    std::sort(trajectories.begin(), trajectories.end(), [](const auto& a, const auto& b) {
        return a.emitter == b.emitter ? a.birthSerial < b.birthSerial : a.emitter < b.emitter;
    });
    std::size_t total = 0;
    for (std::size_t i = 0; i < trajectories.size(); ++i) {
        const auto& t = trajectories[i];
        if (t.emitter.isNil() || t.birthSerial == 0 || t.keys.size() < 2 || t.keys.size() > 2000000 - total ||
            (i && trajectories[i - 1].emitter == t.emitter && trajectories[i - 1].birthSerial == t.birthSerial))
            return Result<ParticleTrack>::failure(
                invalidParticle("invalid or duplicate birth, or key budget exceeded"));
        total += t.keys.size();
        float previous = -1;
        for (const auto& key : t.keys) {
            if (!std::isfinite(key.seconds) || key.seconds < 0 || key.seconds <= previous || key.seconds > duration ||
                !finitePose(key.pose))
                return Result<ParticleTrack>::failure(invalidParticle("invalid particle event time or presentation"));
            previous = key.seconds;
        }
    }
    return Result<ParticleTrack>::success(ParticleTrack(std::make_shared<const Data>(
        Data{duration, std::move(trajectories), std::make_shared<const BakeRecipe>(recipe)})));
}
Result<std::size_t> ParticleTrack::sample(float seconds, std::span<BakedParticleSample> output) const {
    if (!std::isfinite(seconds) || output.size() < capacity())
        return Result<std::size_t>::failure(invalidParticle("invalid sample time or insufficient output capacity"));
    seconds           = std::clamp(seconds, 0.f, data_->duration);
    std::size_t count = 0;
    for (const auto& track : data_->trajectories) {
        if (seconds < track.keys.front().seconds || seconds >= track.keys.back().seconds) continue;
        auto        upper = std::upper_bound(track.keys.begin(), track.keys.end(), seconds,
                                             [](float time, const ParticleTrackKey& key) { return time < key.seconds; });
        const auto& a     = *(upper - 1);
        const auto& b     = *upper;
        double      f     = (double(seconds) - a.seconds) / (double(b.seconds) - a.seconds);
        auto        blend = [f](float x, float y) { return static_cast<float>((1 - f) * double(x) + f * double(y)); };
        auto        pose  = a.pose;
        pose.x            = blend(a.pose.x, b.pose.x);
        pose.y            = blend(a.pose.y, b.pose.y);
        pose.w            = blend(a.pose.w, b.pose.w);
        pose.h            = blend(a.pose.h, b.pose.h);
        pose.rotation     = blend(a.pose.rotation, b.pose.rotation);
        pose.r            = blend(a.pose.r, b.pose.r);
        pose.g            = blend(a.pose.g, b.pose.g);
        pose.b            = blend(a.pose.b, b.pose.b);
        pose.a            = blend(a.pose.a, b.pose.a);
        output[count++]   = {track.emitter, track.birthSerial, pose};
    }
    return Result<std::size_t>::success(count);
}
std::size_t                       ParticleTrack::capacity() const noexcept { return data_->trajectories.size(); }
float                             ParticleTrack::duration() const noexcept { return data_->duration; }
std::shared_ptr<const BakeRecipe> ParticleTrack::recipe() const noexcept { return data_->recipe; }
}  // namespace eve::physics
