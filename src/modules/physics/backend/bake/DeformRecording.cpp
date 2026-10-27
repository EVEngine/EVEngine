#include "physics/backend/bake/DeformRecording.h"
#include <new>
#include <utility>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/DeformTrackInternal.h"
namespace eve::physics {
struct DeformRecording::Impl {
    BakeRecipe               recipe;
    std::size_t              budget;
    std::size_t              samples = 0;
    std::vector<DeformFrame> frames;
    Impl(const BakeRecipe& value, std::size_t limit) : recipe(value), budget(limit) {}
};
namespace {
Diagnostic allocationFailure() {
    return Diagnostic::error(DiagnosticCode::Failed, "deformation recording allocation failed", "deformRecording");
}
}  // namespace
DeformRecording::DeformRecording(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
DeformRecording::~DeformRecording()                                            = default;
DeformRecording::DeformRecording(DeformRecording&&) noexcept                   = default;
DeformRecording&        DeformRecording::operator=(DeformRecording&&) noexcept = default;
Result<DeformRecording> DeformRecording::prepare(const BakeRecipe& recipe, std::size_t sampleBudget) {
    if (sampleBudget == 0 || sampleBudget > 2000000)
        return Result<DeformRecording>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "deformation recording sample budget must be in [1,2000000]", "deformRecording"));
    try {
        auto encoded = recipe.encode();
        if (!encoded) return Result<DeformRecording>::failure(encoded.status());
        return Result<DeformRecording>::success(DeformRecording(std::make_unique<Impl>(recipe, sampleBudget)));
    } catch (const std::bad_alloc&) {
        return Result<DeformRecording>::failure(allocationFailure());
    }
}
Result<void> DeformRecording::append(DeformFrame frame) {
    if (frame.positions.size() > impl_->budget - impl_->samples || impl_->frames.size() >= 2000000)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "deformation recording sample budget exceeded", "deformRecording"));
    const auto topology = impl_->frames.empty() ? frame.topology : impl_->frames.front().topology;
    auto       valid    = bake_detail::validateDeformFrame(frame, topology, impl_->recipe,
                                                           static_cast<std::uint32_t>(impl_->frames.size()),
                                                  impl_->frames.empty() ? -1 : impl_->frames.back().seconds);
    if (!valid) return valid;
    const auto count = frame.positions.size();
    frame.topology   = topology;
    try {
        impl_->frames.push_back(std::move(frame));
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(allocationFailure());
    }
    impl_->samples += count;
    return Result<void>::success();
}
Result<DeformTrack> DeformRecording::freeze() const {
    try {
        return DeformTrack::prepare(impl_->frames, impl_->recipe);
    } catch (const std::bad_alloc&) {
        return Result<DeformTrack>::failure(allocationFailure());
    }
}
std::size_t DeformRecording::frameCount() const noexcept { return impl_->frames.size(); }
std::size_t DeformRecording::sampleCount() const noexcept { return impl_->samples; }
}  // namespace eve::physics
