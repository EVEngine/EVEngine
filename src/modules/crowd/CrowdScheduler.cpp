#include "crowd/CrowdInternal.h"

namespace eve::crowd {
Result<void> Crowd::setWorkScheduler(CrowdExecution execution, std::shared_ptr<CrowdWorkScheduler> scheduler) {
    if (impl_->budgetActive)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Cannot configure an active Crowd", "scheduler"));
    if ((execution != CrowdExecution::Serial && execution != CrowdExecution::Parallel) ||
        (execution == CrowdExecution::Parallel && !scheduler))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Parallel execution requires a scheduler lease", "scheduler"));
    impl_->execution     = execution;
    impl_->workScheduler = execution == CrowdExecution::Parallel ? std::move(scheduler) : nullptr;
    return Result<void>::success();
}

void Crowd::Impl::dispatchWork(std::size_t items, const std::function<void(std::size_t, std::size_t)>& body) {
    constexpr std::size_t grain      = 256;
    const int             partitions = int(items / grain + (items % grain != 0));
    const auto            partition  = [&](int index) {
        const auto first = std::size_t(index) * grain;
        body(first, std::min(items, first + grain));
    };
    if (execution == CrowdExecution::Serial) {
        for (int index = 0; index < partitions; ++index) partition(index);
        return;
    }
    auto result = workScheduler->execute(partitions, partition);
    if (!result) throw SchedulerFailure{result.status()};
}
}  // namespace eve::crowd
