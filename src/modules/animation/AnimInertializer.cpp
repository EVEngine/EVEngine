#include "animation/AnimInertializer.h"
#include <cmath>
#include "animation/PoseInertiaInternal.h"

namespace eve::animation {
struct AnimInertializer::Impl {
    detail::PoseInertia inertia;
    AnimPose            output;
    std::vector<float>  factors;
};
namespace {
bool finitePose(const AnimPose& pose, int count) {
    if (count <= 0 || pose.getBoneCount() != count) return false;
    for (int i = 0; i < count; ++i) {
        const auto& t = pose.local(i);
        for (float v : {t.px, t.py, t.pz, t.qx, t.qy, t.qz, t.qw, t.sx, t.sy, t.sz})
            if (!std::isfinite(v)) return false;
        const double norm = double(t.qx) * t.qx + double(t.qy) * t.qy + double(t.qz) * t.qz + double(t.qw) * t.qw;
        if (norm < 1e-12 || norm > 1e12) return false;
    }
    return true;
}
}  // namespace
AnimInertializer::AnimInertializer() : impl_(std::make_unique<Impl>()) {}
AnimInertializer::~AnimInertializer() = default;

eve::Result<void> AnimInertializer::begin(const AnimPose& source, const AnimPose& previousSource,
                                          const AnimPose& target, const AnimPose& previousTarget, float historySeconds,
                                          float durationSeconds, std::span<const float> boneTimeFactors) {
    const int count = source.getBoneCount();
    if (!std::isfinite(historySeconds) || historySeconds <= 0.f || historySeconds > 1.f ||
        !std::isfinite(durationSeconds) || durationSeconds < 0.f || durationSeconds > 10.f ||
        !finitePose(source, count) || !finitePose(previousSource, count) || !finitePose(target, count) ||
        !finitePose(previousTarget, count))
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "transition requires matching finite poses and valid history/duration", "poseTransition", {}, "animation"));
    if (!boneTimeFactors.empty() && boneTimeFactors.size() != static_cast<std::size_t>(count))
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "time factors must be empty or match the bone count", "poseTransition", {}, "animation"));
    for (float factor : boneTimeFactors)
        if (!std::isfinite(factor) || factor < 0.f || factor > 1.f)
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "bone time factors must be finite and between zero and one", "poseTransition", {}, "animation"));
    auto next = std::make_unique<Impl>();
    next->factors.assign(boneTimeFactors.begin(), boneTimeFactors.end());
    next->inertia.remember(previousSource, historySeconds);
    next->inertia.begin(source, target, previousTarget, durationSeconds);
    for (const auto& values : next->inertia.velocities)
        for (float v : values)
            if (!std::isfinite(v)) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "transition history overflows velocity range", "poseTransition", {}, "animation"));
    next->inertia.apply(target, 0.f, next->output, next->factors);
    if (!finitePose(next->output, count)) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "transition output is not finite", "poseTransition", {}, "animation"));
    impl_.swap(next);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> AnimInertializer::evaluate(const AnimPose& target, float elapsedSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.f || !finitePose(target, impl_->output.getBoneCount()))
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "evaluation requires an initialized transition, matching finite target and elapsed time", "poseTransition", {}, "animation"));
    AnimPose next;
    impl_->inertia.apply(target, elapsedSeconds, next, impl_->factors);
    if (!finitePose(next, target.getBoneCount())) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "transition output is not finite", "poseTransition", {}, "animation"));
    impl_->output = std::move(next);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
const AnimPose& AnimInertializer::pose() const noexcept { return impl_->output; }
}  // namespace eve::animation
