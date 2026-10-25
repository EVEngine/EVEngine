#include "crowd/Crowd.h"
#include "rts/RTSCrowdSurfaceInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::rts {
Result<void> RTS::configureCrowdSurfaceTracking(CrowdSurfaceTracking settings) try {
    if (settings.surface.isZero() || settings.movement.isZero() || settings.movementRevision == 0 ||
        settings.costModelRevision == 0 || !std::isfinite(settings.clearanceRadius) || settings.clearanceRadius < 0.f ||
        settings.maxInflationChecks < 0 || settings.maxInflationChecks > 10000000)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid Crowd surface tracking configuration", "crowdSurfaceTracking"));
    auto staged      = std::make_unique<CrowdSurfaceRuntime>();
    staged->settings = settings;
    if (crowdSurfaceRuntime_ && crowdSurfaceRuntime_->publication.surface == settings.surface)
        staged->publication = crowdSurfaceRuntime_->publication;
    crowdSurfaceRuntime_ = std::move(staged);
    return Result<void>::success();
} catch (const std::exception& error) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "crowdSurfaceTracking"));
}

void RTS::clearCrowdSurfaceTracking() noexcept { crowdSurfaceRuntime_.reset(); }
void RTS::invalidateCrowdSurfaceSource() noexcept {
    if (crowdSurfaceRuntime_) crowdSurfaceRuntime_->source = nullptr;
}

Result<void> RTS::synchronizeCrowdSurface() try {
    if (!crowdSurfaceRuntime_) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (!pathfinder_ || !crowd_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Surface tracking requires map and Crowd providers",
                                                       "crowdSurfaceTracking"));
    auto&      runtime      = *crowdSurfaceRuntime_;
    const auto change       = pathfinder_->getNavigationChangeStamp();
    const auto field        = crowd_->getFieldStatus();
    const bool sameGeometry = runtime.geometry.cellSize == navigationGrid_.cellSize &&
                              runtime.geometry.originX == navigationGrid_.originX &&
                              runtime.geometry.originY == navigationGrid_.originY;
    const bool sameProvider    = runtime.source == pathfinder_;
    const bool samePublication = runtime.captureRevision != 0 && runtime.publication == field.surface;
    if (sameProvider && sameGeometry && samePublication && runtime.change == change)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    auto captured = pathfinder_->captureNavigationGrid();
    if (!captured) return Result<void>::failure(captured.status());
    if (sameProvider && sameGeometry && samePublication && runtime.captureRevision == captured.value().revision()) {
        runtime.change = change;
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
    // Rebinding and geometry changes create a new physical surface revision even when cell content is equal.
    const auto previous =
        std::max(field.surface.surface == runtime.settings.surface ? field.surface.surfaceRevision : 0,
                 runtime.publication.surface == runtime.settings.surface ? runtime.publication.surfaceRevision : 0);
    if (previous == std::numeric_limits<std::uint64_t>::max())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Tracked surface revision exhausted", "crowdSurfaceTracking"));
    const crowd::NavigationSurfaceStamp stamp{
        runtime.settings.surface,          runtime.settings.movement,          previous + 1,
        runtime.settings.movementRevision, runtime.settings.costModelRevision, runtime.settings.clearanceRadius};
    auto projection =
        projectCrowdSurface(captured.value(), stamp, navigationGrid_, runtime.settings.maxInflationChecks);
    if (!projection) return Result<void>::failure(projection.status());
    auto prepared = crowd_->prepareNavigationSurface(projection.value());
    if (!prepared) return Result<void>::failure(prepared.status());
    auto committed = crowd_->commitNavigationSurface(std::move(prepared).takeValue());
    if (!committed) return committed;
    runtime.source          = pathfinder_;
    runtime.geometry        = navigationGrid_;
    runtime.change          = change;
    runtime.publication     = stamp;
    runtime.captureRevision = captured.value().revision();
    return Result<void>::success();
} catch (const std::exception& error) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "crowdSurfaceTracking"));
}
}  // namespace eve::rts
