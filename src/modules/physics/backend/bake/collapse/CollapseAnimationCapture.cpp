#include <new>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/collapse/CollapseAnimation.h"
#include "physics/backend/bake/collapse/CollapseAnimationInternal.h"
#include "physics/backend/bake/collapse/CollapseCaptureSource.h"
#include "physics/backend/bake/collapse/FractureGeometry.h"

namespace eve::physics {
using namespace collapse_detail;
CollapseCaptureSource::~CollapseCaptureSource() = default;
Result<void> CollapseAnimation::capture(const CollapseCaptureSource& source, float seconds) try {
    const auto sourceGeometry = source.fragmentGeometry();
    if (!frames_.empty() && sourceGeometry != geometry_)
        return Result<void>::failure(invalid("recording source geometry changed"));
    const int count = source.boneCount();
    if (!std::isfinite(seconds) || (frames_.empty() ? seconds != 0 : seconds <= duration()) || count <= 0 ||
        (!frames_.empty() && count != boneCount()) || (frames_.size() + 1) * static_cast<size_t>(count) > MaxSamples)
        return Result<void>::failure(invalid("invalid capture time, topology or sample budget"));
    if (recipe_) {
        auto timing = recipe_->validateSampleTime(static_cast<std::uint32_t>(frames_.size()), seconds);
        if (!timing) return timing;
    }
    if (frames_.empty() && sourceGeometry) {
        if (sourceGeometry->fragments.size() != static_cast<std::size_t>(count))
            return Result<void>::failure(invalid("geometry fragment count does not match source"));
        auto geometryValid = sourceGeometry->validate();
        if (!geometryValid) return geometryValid;
    }
    Frame frame{seconds, {}};
    frame.bones.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto pose = source.bonePresentation(i);
        if (!pose) return Result<void>::failure(pose.status());
        if (!valid(pose.value())) return Result<void>::failure(invalid("invalid fragment pose"));
        if (!frames_.empty()) {
            const auto& a = frames_.front().bones[static_cast<size_t>(i)];
            const auto& b = pose.value();
            if (a.halfExtentX != b.halfExtentX || a.halfExtentY != b.halfExtentY || a.halfExtentZ != b.halfExtentZ ||
                a.clusterId != b.clusterId || a.fractureLevel != b.fractureLevel)
                return Result<void>::failure(invalid("fragment topology changed while recording"));
        }
        frame.bones.push_back(pose.value());
    }
    frames_.push_back(std::move(frame));
    if (frames_.size() == 1) geometry_ = sourceGeometry;
    return Result<void>::success();
} catch (const std::bad_alloc&) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "collapse capture allocation failed", "collapseAnimation"));
}
}  // namespace eve::physics
