#include <algorithm>
#include <cmath>
#include <utility>
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/RigidPoseTrackInternal.h"

namespace eve::physics {
namespace {
Diagnostic invalidTrack(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "rigidPoseTrack");
}
}  // namespace
Result<void> bake_detail::validateRigidFrame(RigidTrackSpace space, RigidTrackFrame& frame,
                                             std::span<const RigidTrackSample> roster, float previous) {
    if (space != RigidTrackSpace::PlanarPixels && space != RigidTrackSpace::Metres3D)
        return Result<void>::failure(invalidTrack("unknown coordinate space"));
    if (!std::isfinite(frame.seconds) || frame.seconds < 0 || frame.seconds <= previous ||
        (roster.empty() && frame.seconds != 0) || frame.samples.empty() || frame.samples.size() > 16384 ||
        (!roster.empty() && frame.samples.size() != roster.size()))
        return Result<void>::failure(invalidTrack("invalid timestamp or changed roster size"));
    std::sort(frame.samples.begin(), frame.samples.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (std::size_t i = 0; i < frame.samples.size(); ++i) {
        auto& sample = frame.samples[i];
        if (sample.id.isNil() || (i && sample.id == frame.samples[i - 1].id) ||
            (!roster.empty() && sample.id != roster[i].id))
            return Result<void>::failure(invalidTrack("nil, duplicate or changed stable identity"));
        auto& p = sample.pose;
        for (float v : {p.x, p.y, p.z, p.qx, p.qy, p.qz, p.qw})
            if (!std::isfinite(v)) return Result<void>::failure(invalidTrack("nonfinite pose"));
        if (space == RigidTrackSpace::PlanarPixels && (p.z != 0 || p.qx != 0 || p.qy != 0))
            return Result<void>::failure(invalidTrack("out-of-plane planar pose"));
        const double x = p.qx, y = p.qy, z = p.qz, w = p.qw;
        const double norm = std::sqrt(x * x + y * y + z * z + w * w);
        if (norm == 0) return Result<void>::failure(invalidTrack("zero rotation quaternion"));
        p.qx = static_cast<float>(x / norm);
        p.qy = static_cast<float>(y / norm);
        p.qz = static_cast<float>(z / norm);
        p.qw = static_cast<float>(w / norm);
    }
    return Result<void>::success();
}
RigidPoseTrack::RigidPoseTrack(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
Result<RigidPoseTrack> RigidPoseTrack::prepare(RigidTrackSpace space, std::vector<RigidTrackFrame> frames) {
    return prepareValidated(space, std::move(frames), {});
}
Result<RigidPoseTrack> RigidPoseTrack::prepare(RigidTrackSpace space, std::vector<RigidTrackFrame> frames,
                                               const BakeRecipe& recipe) {
    const auto coordinates =
        space == RigidTrackSpace::PlanarPixels ? BakeCoordinates::PlanarPixels : BakeCoordinates::MetresRightHandedYUp;
    if (recipe.coordinates() != coordinates)
        return Result<RigidPoseTrack>::failure(invalidTrack("recipe coordinate mismatch"));
    auto encoded = recipe.encode();
    if (!encoded) return Result<RigidPoseTrack>::failure(encoded.status());
    if (frames.size() > 2000000) return Result<RigidPoseTrack>::failure(invalidTrack("frame budget exceeded"));
    for (std::size_t i = 0; i < frames.size(); ++i) {
        auto valid = recipe.validateSampleTime(static_cast<std::uint32_t>(i), frames[i].seconds);
        if (!valid) return Result<RigidPoseTrack>::failure(valid.status());
    }
    return prepareValidated(space, std::move(frames), std::make_shared<const BakeRecipe>(recipe));
}
std::shared_ptr<const BakeRecipe> RigidPoseTrack::recipe() const noexcept { return data_->recipe; }
Result<RigidPoseTrack> RigidPoseTrack::prepareValidated(RigidTrackSpace space, std::vector<RigidTrackFrame> frames,
                                                        std::shared_ptr<const BakeRecipe> recipe) {
    constexpr std::size_t maxSamples = 2000000, maxTracks = 16384;
    if (space != RigidTrackSpace::PlanarPixels && space != RigidTrackSpace::Metres3D)
        return Result<RigidPoseTrack>::failure(invalidTrack("unknown coordinate space"));
    if (frames.empty() || frames.size() > maxSamples || frames.front().samples.empty() ||
        frames.front().samples.size() > maxTracks)
        return Result<RigidPoseTrack>::failure(invalidTrack("empty or oversized recording"));
    const auto tracks = frames.front().samples.size();
    if (frames.size() > maxSamples / tracks)
        return Result<RigidPoseTrack>::failure(invalidTrack("sample budget exceeded"));
    float previous = -1;
    for (auto& frame : frames) {
        const std::span<const RigidTrackSample> roster =
            &frame == &frames.front() ? std::span<const RigidTrackSample>{} : frames.front().samples;
        auto valid = bake_detail::validateRigidFrame(space, frame, roster, previous);
        if (!valid) return Result<RigidPoseTrack>::failure(valid.status());
        previous = frame.seconds;
    }
    return Result<RigidPoseTrack>::success(
        RigidPoseTrack(std::make_shared<const Data>(Data{space, std::move(recipe), std::move(frames)})));
}
Result<RigidTrackPose> RigidPoseTrack::sample(float seconds, PersistentId id) const {
    if (!std::isfinite(seconds)) return Result<RigidTrackPose>::failure(invalidTrack("nonfinite sample time"));
    const auto& roster = data_->frames.front().samples;
    const auto  found  = std::lower_bound(roster.begin(), roster.end(), id,
                                          [](const auto& sample, PersistentId wanted) { return sample.id < wanted; });
    if (found == roster.end() || found->id != id)
        return Result<RigidTrackPose>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown track identity", "rigidPoseTrack"));
    const auto index = static_cast<std::size_t>(found - roster.begin());
    auto       upper = std::upper_bound(data_->frames.begin(), data_->frames.end(), seconds,
                                        [](float t, const auto& frame) { return t < frame.seconds; });
    if (upper == data_->frames.begin()) return Result<RigidTrackPose>::success(upper->samples[index].pose);
    const auto& a = *(upper - 1);
    if (upper == data_->frames.end()) return Result<RigidTrackPose>::success(a.samples[index].pose);
    auto        pose = a.samples[index].pose;
    const auto& b    = upper->samples[index].pose;
    const float t    = (seconds - a.seconds) / (upper->seconds - a.seconds);
    pose.x           = std::lerp(pose.x, b.x, t);
    pose.y           = std::lerp(pose.y, b.y, t);
    pose.z           = std::lerp(pose.z, b.z, t);
    const float sign = pose.qx * b.qx + pose.qy * b.qy + pose.qz * b.qz + pose.qw * b.qw < 0 ? -1.f : 1.f;
    pose.qx          = std::lerp(pose.qx, sign * b.qx, t);
    pose.qy          = std::lerp(pose.qy, sign * b.qy, t);
    pose.qz          = std::lerp(pose.qz, sign * b.qz, t);
    pose.qw          = std::lerp(pose.qw, sign * b.qw, t);
    const float norm = std::sqrt(pose.qx * pose.qx + pose.qy * pose.qy + pose.qz * pose.qz + pose.qw * pose.qw);
    pose.qx /= norm;
    pose.qy /= norm;
    pose.qz /= norm;
    pose.qw /= norm;
    return Result<RigidTrackPose>::success(pose);
}
RigidTrackSpace      RigidPoseTrack::space() const noexcept { return data_->space; }
float                RigidPoseTrack::duration() const noexcept { return data_->frames.back().seconds; }
std::size_t          RigidPoseTrack::trackCount() const noexcept { return data_->frames.front().samples.size(); }
std::size_t          RigidPoseTrack::frameCount() const noexcept { return data_->frames.size(); }
Result<PersistentId> RigidPoseTrack::trackId(std::size_t index) const {
    if (index >= trackCount()) return Result<PersistentId>::failure(invalidTrack("track index outside roster"));
    return Result<PersistentId>::success(data_->frames.front().samples[index].id);
}
}  // namespace eve::physics
