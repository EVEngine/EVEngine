#include <utility>
#include "crowd/CrowdInternal.h"
namespace eve::crowd {
Crowd::SurfacePlan::SurfacePlan()  = default;
Crowd::SurfacePlan::~SurfacePlan() = default;
Crowd::SurfacePlan::SurfacePlan(SurfacePlan&& other) noexcept
    : field_(std::move(other.field_)),
      stamp_(other.stamp_),
      owner_(std::exchange(other.owner_, 0)),
      revision_(std::exchange(other.revision_, 0)),
      unchanged_(std::exchange(other.unchanged_, false)) {}
Crowd::SurfacePlan& Crowd::SurfacePlan::operator=(SurfacePlan&& other) noexcept {
    if (this == &other) return *this;
    field_     = std::move(other.field_);
    stamp_     = other.stamp_;
    owner_     = std::exchange(other.owner_, 0);
    revision_  = std::exchange(other.revision_, 0);
    unchanged_ = std::exchange(other.unchanged_, false);
    return *this;
}
Result<Crowd::SurfacePlan> Crowd::prepareNavigationSurface(const NavigationSurfaceSnapshot& snapshot) const try {
    const auto fail = [](DiagnosticCode code, const char* message) {
        return Result<SurfacePlan>::failure(Diagnostic::error(code, message, "navigationSurface"));
    };
    const auto count = std::int64_t(snapshot.width) * snapshot.height;
    if (snapshot.width <= 0 || snapshot.height <= 0 || count > 4 * 1024 * 1024 || !std::isfinite(snapshot.cellSize) ||
        snapshot.cellSize <= 0.f || !std::isfinite(snapshot.originX) || !std::isfinite(snapshot.originY) ||
        snapshot.stamp.surface.isZero() || snapshot.stamp.movement.isZero() || snapshot.stamp.surfaceRevision == 0 ||
        snapshot.stamp.movementRevision == 0 || snapshot.stamp.costModelRevision == 0 ||
        !std::isfinite(snapshot.stamp.clearanceRadius) || snapshot.stamp.clearanceRadius < 0.f ||
        snapshot.terrainCosts.size() != static_cast<std::size_t>(count) ||
        snapshot.navigationCosts.size() != static_cast<std::size_t>(count))
        return fail(DiagnosticCode::InvalidArgument, "Invalid surface geometry, identity, revision or array size");
    for (std::size_t index = 0; index < snapshot.terrainCosts.size(); ++index) {
        const float physical = snapshot.terrainCosts[index], navigation = snapshot.navigationCosts[index];
        const auto  valid = [](float value) { return std::isfinite(value) && (value == 0.f || value >= 1.f); };
        if (!valid(physical) || !valid(navigation) || (physical == 0.f && navigation != 0.f))
            return fail(DiagnosticCode::InvalidArgument, "Navigation costs cannot unblock physical walls");
    }
    SurfacePlan plan;
    plan.owner_       = impl_->owner;
    plan.revision_    = impl_->field.revision();
    plan.stamp_       = snapshot.stamp;
    const auto& old   = impl_->surfaceStamp;
    const auto& field = impl_->field;
    if (!field.navigationCost_.empty() && old.surface == snapshot.stamp.surface) {
        if (snapshot.stamp.surfaceRevision < old.surfaceRevision)
            return fail(DiagnosticCode::Conflict, "Surface revision cannot move backwards");
        if (snapshot.stamp.surfaceRevision == old.surfaceRevision &&
            (field.getWidth() != snapshot.width || field.getHeight() != snapshot.height ||
             field.getCellSize() != snapshot.cellSize || field.getOriginX() != snapshot.originX ||
             field.getOriginY() != snapshot.originY || field.cost_ != snapshot.terrainCosts))
            return fail(DiagnosticCode::Conflict, "Surface identity/revision has conflicting physical geometry");
    }
    if (!field.navigationCost_.empty() && old.movement == snapshot.stamp.movement &&
        old.movementRevision == snapshot.stamp.movementRevision &&
        old.clearanceRadius != snapshot.stamp.clearanceRadius)
        return fail(DiagnosticCode::Conflict, "Movement identity/revision has conflicting clearance");
    if (!field.navigationCost_.empty() && old.surface == snapshot.stamp.surface &&
        old.movement == snapshot.stamp.movement) {
        if (snapshot.stamp.surfaceRevision < old.surfaceRevision ||
            snapshot.stamp.movementRevision < old.movementRevision ||
            snapshot.stamp.costModelRevision < old.costModelRevision)
            return fail(DiagnosticCode::Conflict, "Surface provenance cannot move backwards");
        if (old == snapshot.stamp) {
            if (field.navigationDiagonal_ != snapshot.diagonal ||
                field.destinationEntryCost_ != snapshot.destinationEntryCost)
                return fail(DiagnosticCode::Conflict, "Identical cost-model provenance has conflicting rules");
            if (field.getWidth() != snapshot.width || field.getHeight() != snapshot.height ||
                field.getCellSize() != snapshot.cellSize || field.getOriginX() != snapshot.originX ||
                field.getOriginY() != snapshot.originY || field.cost_ != snapshot.terrainCosts ||
                field.navigationCost_ != snapshot.navigationCosts)
                return fail(DiagnosticCode::Conflict, "Identical surface provenance has conflicting content");
            plan.unchanged_ = true;
            return Result<SurfacePlan>::success(std::move(plan), Status::success(StatusCode::NoOp));
        }
    }
    plan.field_ = std::make_unique<CrowdField>(field);
    plan.field_->resize(snapshot.width, snapshot.height, snapshot.cellSize, snapshot.originX, snapshot.originY);
    plan.field_->cost_                 = snapshot.terrainCosts;
    plan.field_->navigationCost_       = snapshot.navigationCosts;
    plan.field_->navigationDiagonal_   = snapshot.diagonal;
    plan.field_->destinationEntryCost_ = snapshot.destinationEntryCost;
    plan.field_->invalidateInputs();
    return Result<SurfacePlan>::success(std::move(plan));
} catch (const std::exception& error) {
    return Result<SurfacePlan>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "navigationSurface"));
}
Result<void> Crowd::commitNavigationSurface(SurfacePlan&& plan) {
    if (plan.owner_ != impl_->owner || plan.revision_ != impl_->field.revision() || (!plan.field_ && !plan.unchanged_))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Empty, stale or foreign surface publication", "navigationSurface"));
    const bool unchanged = plan.unchanged_;
    if (!unchanged) {
        impl_->field        = std::move(*plan.field_);
        impl_->surfaceStamp = plan.stamp_;
    }
    plan.field_.reset();
    plan.owner_     = 0;
    plan.unchanged_ = false;
    return Result<void>::success(Status::success(unchanged ? StatusCode::NoOp : StatusCode::Applied));
}
}  // namespace eve::crowd
