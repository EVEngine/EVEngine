#include <new>
#include <type_traits>
#include <utility>
#include "crowd/CrowdInternal.h"
namespace eve::crowd {
static_assert(std::is_nothrow_move_assignable_v<CrowdField>, "Navigation publication must not allocate or throw");
Crowd::FlowBuildPlan::FlowBuildPlan()  = default;
Crowd::FlowBuildPlan::~FlowBuildPlan() = default;
Crowd::FlowBuildPlan::FlowBuildPlan(FlowBuildPlan&& other) noexcept
    : field_(std::move(other.field_)),
      owner_(std::exchange(other.owner_, 0)),
      revision_(std::exchange(other.revision_, 0)),
      state_(std::exchange(other.state_, FlowBuildState::Cancelled)) {}
Crowd::FlowBuildPlan& Crowd::FlowBuildPlan::operator=(FlowBuildPlan&& other) noexcept {
    if (this == &other) return *this;
    field_    = std::move(other.field_);
    owner_    = std::exchange(other.owner_, 0);
    revision_ = std::exchange(other.revision_, 0);
    state_    = std::exchange(other.state_, FlowBuildState::Cancelled);
    return *this;
}
Result<void> Crowd::FlowBuildPlan::cancel() {
    if (state_ == FlowBuildState::Committed)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Cannot cancel committed navigation", "flowBuild"));
    const bool changed = state_ != FlowBuildState::Cancelled;
    field_.reset();
    state_ = FlowBuildState::Cancelled;
    return Result<void>::success(Status::success(changed ? StatusCode::Applied : StatusCode::NoOp));
}
Result<Crowd::FlowBuildPlan> Crowd::prepareFlowBuild(int gx, int gy) const {
    const FlowGoal goal{gx, gy};
    return prepareFlowBuild(std::span<const FlowGoal>(&goal, 1));
}
Result<Crowd::FlowBuildPlan> Crowd::prepareFlowBuild(std::span<const FlowGoal> goals) const try {
    const auto& field     = impl_->field;
    auto        canonical = canonicalFlowCoordinates(goals, field);
    if (!canonical) return Result<FlowBuildPlan>::failure(canonical.status());
    FlowBuildPlan plan;
    plan.owner_      = impl_->owner;
    plan.revision_   = field.inputRevision();
    plan.field_      = std::make_unique<CrowdField>(field);
    auto coordinates = std::move(canonical).takeValue();
    if (plan.field_->goals_ != coordinates) {
        plan.field_->goals_.swap(coordinates);
        plan.field_->invalidate();
    }
    plan.field_->beginBuildWorking();
    plan.state_ = FlowBuildState::Pending;
    return Result<FlowBuildPlan>::success(std::move(plan));
} catch (const std::exception& error) {
    return Result<FlowBuildPlan>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "flowBuild"));
}
Result<Crowd::FlowBuildProgress> Crowd::advanceFlowBuild(FlowBuildPlan& plan, int maxWork) try {
    if (maxWork < 0 || maxWork > 1000000)
        return Result<FlowBuildProgress>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid navigation work budget", "flowBuild.maxWork"));
    if (!plan.field_ || (plan.state_ != FlowBuildState::Pending && plan.state_ != FlowBuildState::Ready &&
                         plan.state_ != FlowBuildState::Unreachable))
        return Result<FlowBuildProgress>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Navigation request is not advanceable", "flowBuild"));
    if (plan.owner_ != impl_->owner)
        return Result<FlowBuildProgress>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Foreign navigation request", "flowBuild"));
    if (plan.revision_ != impl_->field.inputRevision()) {
        auto cancelled = plan.cancel();
        if (!cancelled) return Result<FlowBuildProgress>::failure(cancelled.status());
        return Result<FlowBuildProgress>::success({FlowBuildState::Cancelled, 0, 0});
    }
    if (plan.state_ != FlowBuildState::Pending || maxWork == 0)
        return Result<FlowBuildProgress>::success({plan.state_, 0, 0}, Status::success(StatusCode::NoOp));
    const auto [pops, cells] = plan.field_->advanceBuildWorking(maxWork);
    if (plan.field_->isBuilt()) {
        bool reachable = false;
        for (size_t index = 0; index + 1 < plan.field_->goals_.size(); index += 2)
            reachable =
                reachable || plan.field_->isReachable(plan.field_->goals_[index], plan.field_->goals_[index + 1]);
        plan.state_ = reachable ? FlowBuildState::Ready : FlowBuildState::Unreachable;
    }
    return Result<FlowBuildProgress>::success({plan.state_, pops, cells});
} catch (const std::exception& error) {
    plan.field_.reset();
    plan.state_ = FlowBuildState::Failed;
    return Result<FlowBuildProgress>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "flowBuild"));
}

Result<void> Crowd::commitFlowBuild(FlowBuildPlan&& plan) {
    if (!plan.field_ || (plan.state_ != FlowBuildState::Ready && plan.state_ != FlowBuildState::Unreachable) ||
        plan.owner_ != impl_->owner || plan.revision_ != impl_->field.inputRevision())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Navigation plan is empty, stale, foreign or cancelled", "flowBuild"));
    if (impl_->field.revision_ == std::numeric_limits<std::uint64_t>::max())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Field publication revision exhausted", "flowBuild"));
    plan.field_->revision_      = impl_->field.revision_ + 1;
    plan.field_->builtRevision_ = plan.field_->revision_;
    impl_->field                = std::move(*plan.field_);
    plan.field_.reset();
    plan.state_ = FlowBuildState::Committed;
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::crowd
