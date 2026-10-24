#include "crowd/CrowdInternal.h"

#include <new>

namespace eve::crowd {
Crowd::RestorePlan::RestorePlan()                                         = default;
Crowd::RestorePlan::~RestorePlan()                                        = default;
Crowd::RestorePlan::RestorePlan(RestorePlan&&) noexcept                   = default;
Crowd::RestorePlan& Crowd::RestorePlan::operator=(RestorePlan&&) noexcept = default;

Result<Crowd::Snapshot> Crowd::captureSnapshot() const try {
    if (impl_->budgetActive)
        return Result<Snapshot>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Cannot capture during Crowd advancement", "snapshot"));
    Snapshot snapshot;
    auto     state = std::make_shared<Impl>(*impl_);
    state->workScheduler.reset();
    state->execution = CrowdExecution::Serial;
    snapshot.state_  = std::move(state);
    return Result<Snapshot>::success(std::move(snapshot));
} catch (const std::bad_alloc&) {
    return Result<Snapshot>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "Cannot allocate Crowd checkpoint", "snapshot"));
}

Result<Crowd::RestorePlan> Crowd::prepareRestore(const Snapshot& snapshot) const try {
    if (!snapshot.state_ || impl_->budgetActive)
        return Result<RestorePlan>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Invalid snapshot or active Crowd step", "snapshot"));
    RestorePlan plan;
    plan.state_        = std::make_unique<Impl>();
    const auto owner   = plan.state_->owner;
    *plan.state_       = *snapshot.state_;
    plan.state_->owner = owner;
    for (auto& lease : plan.state_->flows)
        if (lease.field_) lease.owner_ = owner;
    plan.state_->budgetActive = false;
    plan.state_->spatialIndex.clear();
    plan.state_->contactPairs.clear();
    plan.state_->contactDeltas.clear();
    plan.state_->predictionNeighbors.clear();
    plan.state_->preferredNeighbors.clear();
    plan.state_->preferredStarts.clear();
    plan.state_->preferredCounts.clear();
    plan.state_->preferredFlows.clear();
    plan.state_->incidentStarts.clear();
    plan.state_->incidentPairs.clear();
    plan.state_->workReport = {};
    plan.targetOwner_       = impl_->owner;
    return Result<RestorePlan>::success(std::move(plan));
} catch (const std::exception& error) {
    return Result<RestorePlan>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "snapshot"));
}

Result<void> Crowd::commitRestore(RestorePlan&& plan) {
    if (!plan.state_ || plan.targetOwner_ != impl_->owner || impl_->budgetActive)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Empty, foreign or stale Crowd restore plan", "snapshot"));
    plan.state_->execution     = impl_->execution;
    plan.state_->workScheduler = impl_->workScheduler;
    impl_.swap(plan.state_);
    clearNavigationRequests();
    plan.state_.reset();
    plan.targetOwner_ = 0;
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::crowd
