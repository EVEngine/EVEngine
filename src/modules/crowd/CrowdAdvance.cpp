#include "crowd/CrowdInternal.h"

#include <new>

namespace eve::crowd {

struct Crowd::Impl::AdvanceGuard {
    Impl&        data;
    float        oldTime;
    StepReport   oldWork;
    std::int64_t oldPredictions;
    int          oldTruncations;
    bool         committed = false;

    AdvanceGuard(Impl& value, const AdvanceBudget& budget)
        : data(value),
          oldTime(value.simTime),
          oldWork(value.workReport),
          oldPredictions(value.avoidanceChecks),
          oldTruncations(value.avoidanceTruncations) {
        auto& saved = data.rollback;
        saved.xs.assign(data.xs.begin(), data.xs.end());
        saved.ys.assign(data.ys.begin(), data.ys.end());
        saved.headings.assign(data.headings.begin(), data.headings.end());
        saved.vxs.assign(data.vxs.begin(), data.vxs.end());
        saved.vys.assign(data.vys.begin(), data.vys.end());
        saved.speeds.assign(data.speeds.begin(), data.speeds.end());
        saved.wanderPhases.assign(data.wanderPhases.begin(), data.wanderPhases.end());
        data.activeBudget = budget;
        data.budgetActive = true;
    }
    ~AdvanceGuard() {
        data.budgetActive = false;
        if (committed) return;
        auto& saved = data.rollback;
        data.xs.swap(saved.xs);
        data.ys.swap(saved.ys);
        data.headings.swap(saved.headings);
        data.vxs.swap(saved.vxs);
        data.vys.swap(saved.vys);
        data.speeds.swap(saved.speeds);
        data.wanderPhases.swap(saved.wanderPhases);
        data.simTime              = oldTime;
        data.workReport           = oldWork;
        data.avoidanceChecks      = oldPredictions;
        data.avoidanceTruncations = oldTruncations;
        data.spatialIndex.clear();
        data.contactPairs.clear();
        data.contactDeltas.clear();
        data.predictionNeighbors.clear();
        data.preferredNeighbors.clear();
        data.preferredStarts.clear();
        data.preferredCounts.clear();
        data.preferredFlows.clear();
        data.incidentStarts.clear();
        data.incidentPairs.clear();
    }
};

Result<StepReport> Crowd::advance(float dt) {
    if (!std::isfinite(dt) || dt < 0.f)
        return Result<StepReport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Crowd dt must be finite and nonnegative", "dt"));
    auto duration = Duration::fromSeconds(dt);
    if (!duration) return Result<StepReport>::failure(duration.status());
    SimulationTick tick = impl_->tick;
    if (!duration.value().isZero()) {
        const auto next = tick.incremented();
        if (!next)
            return Result<StepReport>::failure(
                Diagnostic::error(DiagnosticCode::PreconditionViolation, "Crowd tick exhausted", "tick"));
        tick = *next;
    }
    return advanceInternal({tick, duration.value(), {}}, ClockAuthority::Compatibility);
}

Result<StepReport> Crowd::advance(const CrowdStep& step) { return advanceInternal(step, ClockAuthority::External); }

Result<void> Crowd::checkAdvanceClock(const CrowdStep& step) const {
    return checkClock(step, ClockAuthority::External);
}

Result<void> Crowd::checkClock(const CrowdStep& step, ClockAuthority authority) const {
    const auto& d    = *impl_;
    const auto  fail = [](const char* message) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, message, "crowdStep.tick"));
    };
    if (d.budgetActive) return fail("Crowd advancement cannot reenter");
    if (step.delta.nanoseconds() < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Negative Crowd duration", "crowdStep.delta"));
    if (step.delta.isZero()) {
        if (step.tick != d.tick) return fail("Zero-duration step must reconcile the current tick");
        return Result<void>::success();
    }
    const auto next = d.tick.incremented();
    if (!next || step.tick != *next) return fail("Crowd step must consume the next contiguous tick");
    if (d.clock && *d.clock != authority) return fail("Cannot mix compatibility and external Crowd clocks");
    auto elapsed = d.elapsed.tryAdd(step.delta);
    if (!elapsed) return Result<void>::failure(elapsed.status());
    return Result<void>::success();
}

Result<StepReport> Crowd::advanceInternal(const CrowdStep& step, ClockAuthority authority) try {
    const auto failure = [](DiagnosticCode code, const char* message) {
        return Result<StepReport>::failure(Diagnostic::error(code, message, "crowdStep"));
    };
    auto& d        = *impl_;
    auto  admitted = checkClock(step, authority);
    if (!admitted) return Result<StepReport>::failure(admitted.status());
    const auto& budget = step.budget;
    if (step.delta.nanoseconds() < 0 || budget.maxSubsteps < 1 || budget.maxSubsteps > 1024 ||
        budget.maxContactPasses < 1 || budget.maxContactPasses > 12 || budget.maxGridCells < 1 ||
        budget.maxNeighborVisits < 0 || budget.maxPredictionChecks < 0 || budget.maxContactChecks < 0 ||
        budget.maxSpatialVisits < 0 || budget.maxContactPairs < 0 || budget.maxContactPairs > 10000000 ||
        budget.maxPairVisits < 0 || budget.maxPreferredVisits < 0 || budget.maxPreferredNeighbors < 0 ||
        budget.maxPreferredNeighbors > 10000000)
        return failure(DiagnosticCode::InvalidArgument, "Invalid duration or work budget");
    if (step.delta.isZero()) {
        StepReport report;
        report.navigation                = getFieldStatus();
        report.navigationClassMismatches = d.navigationMismatchCount();
        report.tick                      = d.tick;
        report.elapsed                   = d.elapsed;
        return Result<StepReport>::success(report, Status::success(StatusCode::NoOp));
    }
    auto elapsed = d.elapsed.tryAdd(step.delta);
    if (!elapsed) return Result<StepReport>::failure(elapsed.status());
    for (const float parameter : {d.arriveRadius, d.sepRadius, d.perceiveRadius, d.sepWeight, d.alignWeight,
                                  d.cohesionWeight, d.wanderWeight, d.goalWeight})
        if (!std::isfinite(parameter))
            return failure(DiagnosticCode::PreconditionViolation, "Crowd steering parameters are nonfinite");
    for (std::size_t index = 0; index < d.xs.size(); ++index) {
        if (!std::isfinite(d.xs[index]) || !std::isfinite(d.ys[index]) || !std::isfinite(d.vxs[index]) ||
            !std::isfinite(d.vys[index]) || !std::isfinite(d.headings[index]) ||
            !std::isfinite(d.wanderPhases[index]) || !std::isfinite(d.radii[index]) || d.radii[index] < 0.f ||
            !std::isfinite(d.maxSpeeds[index]) || d.maxSpeeds[index] < 0.f || !std::isfinite(d.maxAccels[index]) ||
            d.maxAccels[index] < 0.f || !std::isfinite(d.turnRates[index]) || d.turnRates[index] < 0.f)
            return failure(DiagnosticCode::PreconditionViolation, "Crowd geometry or motion limits are invalid");
    }
    Impl::AdvanceGuard guard(d, budget);
    auto               advanced = advanceWorking(static_cast<float>(step.delta.seconds()), budget);
    if (!advanced) return Result<StepReport>::failure(advanced.status());
    for (std::size_t index = 0; index < d.xs.size(); ++index)
        if (!std::isfinite(d.xs[index]) || !std::isfinite(d.ys[index]) || !std::isfinite(d.vxs[index]) ||
            !std::isfinite(d.vys[index]) || !std::isfinite(d.headings[index]) || !std::isfinite(d.speeds[index]) ||
            !std::isfinite(d.wanderPhases[index]))
            return failure(DiagnosticCode::Failed, "Crowd solver produced nonfinite motion");
    auto report     = std::move(advanced).takeValue();
    report.tick     = step.tick;
    report.elapsed  = elapsed.value();
    d.tick          = step.tick;
    d.elapsed       = elapsed.value();
    d.clock         = authority;
    d.simTime       = static_cast<float>(d.elapsed.seconds());
    guard.committed = true;
    return Result<StepReport>::success(report, Status::success(d.xs.empty() ? StatusCode::NoOp : StatusCode::Applied));
} catch (const Impl::SchedulerFailure& failure) {
    return Result<StepReport>::failure(failure.status);
} catch (const WorkLimit& limit) {
    return Result<StepReport>::failure(
        Diagnostic::error(DiagnosticCode::PreconditionViolation, limit.message, "crowdStep.budget"));
} catch (const std::bad_alloc&) {
    return Result<StepReport>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "Cannot allocate Crowd step workspace", "crowdStep"));
}

}  // namespace eve::crowd
