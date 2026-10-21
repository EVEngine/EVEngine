#include "physics/bake/RigidRecording.h"
#include <new>
#include <utility>
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/RigidPoseTrackInternal.h"
namespace eve::physics {
struct RigidRecording::Impl {
    BakeRecipe                   recipe;
    RigidTrackSpace              space;
    std::size_t                  budget;
    std::size_t                  samples = 0;
    std::vector<RigidTrackFrame> frames;
    Impl(const BakeRecipe& value, std::size_t limit)
        : recipe(value),
          space(value.coordinates() == BakeCoordinates::PlanarPixels ? RigidTrackSpace::PlanarPixels
                                                                     : RigidTrackSpace::Metres3D),
          budget(limit) {}
};
namespace {
Diagnostic allocationFailure() {
    return Diagnostic::error(DiagnosticCode::Failed, "rigid recording allocation failed", "rigidRecording");
}
}  // namespace
RigidRecording::RigidRecording(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RigidRecording::~RigidRecording()                                           = default;
RigidRecording::RigidRecording(RigidRecording&&) noexcept                   = default;
RigidRecording&        RigidRecording::operator=(RigidRecording&&) noexcept = default;
Result<RigidRecording> RigidRecording::prepare(const BakeRecipe& recipe, std::size_t sampleBudget) {
    if (sampleBudget == 0 || sampleBudget > 2000000)
        return Result<RigidRecording>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "rigid recording sample budget must be in [1,2000000]", "rigidRecording"));
    try {
        auto encoded = recipe.encode();
        if (!encoded) return Result<RigidRecording>::failure(encoded.status());
        return Result<RigidRecording>::success(RigidRecording(std::make_unique<Impl>(recipe, sampleBudget)));
    } catch (const std::bad_alloc&) {
        return Result<RigidRecording>::failure(allocationFailure());
    }
}
Result<void> RigidRecording::append(RigidTrackFrame frame) {
    if (frame.samples.size() > impl_->budget - impl_->samples || impl_->frames.size() >= 2000000)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "rigid recording sample budget exceeded", "rigidRecording"));
    auto timing = impl_->recipe.validateSampleTime(static_cast<std::uint32_t>(impl_->frames.size()), frame.seconds);
    if (!timing) return timing;
    const std::span<const RigidTrackSample> roster =
        impl_->frames.empty() ? std::span<const RigidTrackSample>{} : impl_->frames.front().samples;
    auto valid = bake_detail::validateRigidFrame(impl_->space, frame, roster,
                                                 impl_->frames.empty() ? -1 : impl_->frames.back().seconds);
    if (!valid) return valid;
    const auto count = frame.samples.size();
    try {
        impl_->frames.push_back(std::move(frame));
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(allocationFailure());
    }
    impl_->samples += count;
    return Result<void>::success();
}
Result<RigidPoseTrack> RigidRecording::freeze() const {
    try {
        return RigidPoseTrack::prepare(impl_->space, impl_->frames, impl_->recipe);
    } catch (const std::bad_alloc&) {
        return Result<RigidPoseTrack>::failure(allocationFailure());
    }
}
RigidTrackSpace RigidRecording::space() const noexcept { return impl_->space; }
std::size_t     RigidRecording::frameCount() const noexcept { return impl_->frames.size(); }
std::size_t     RigidRecording::sampleCount() const noexcept { return impl_->samples; }
}  // namespace eve::physics
