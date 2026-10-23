#include "common/Profile.h"
#include "crowd/CrowdInternal.h"

#include <chrono>

namespace eve::crowd {

Result<StepReport> Crowd::advanceWorking(float dt, const AdvanceBudget& budget) {
    EV_PROFILE_MODULE("crowd", "Crowd::advance");
    if (!std::isfinite(dt) || dt < 0.f)
        return Result<StepReport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Crowd dt must be finite and non-negative", "dt"));
    StepReport report;
    report.navigation                = getFieldStatus();
    report.navigationClassMismatches = impl_->navigationMismatchCount();
    if (dt == 0.f || impl_->xs.empty()) return Result<StepReport>::success(report, Status::success(StatusCode::NoOp));

    auto& d                = *impl_;
    d.avoidanceChecks      = 0;
    d.avoidanceTruncations = 0;
    double maxStep         = 1.0 / 60.0;
    float  travelScale     = d.field.valid() ? d.field.getCellSize() : 0.f;
    float  maxSpeed        = 0.f;
    for (size_t i = 0; i < d.xs.size(); ++i) {
        if (d.radii[i] > 0.f) travelScale = travelScale > 0.f ? std::min(travelScale, d.radii[i]) : d.radii[i];
        maxSpeed = std::max(maxSpeed, d.maxSpeeds[i]);
    }
    if (travelScale > 0.f && maxSpeed > 0.f) maxStep = std::min(maxStep, 0.5 * double(travelScale) / double(maxSpeed));
    // Float dt=1/60 rounds slightly upward. Do not create an extra substep for
    // representation error at an otherwise exact boundary.
    const double count = std::ceil(double(dt) / maxStep - 1e-6);
    if (!std::isfinite(count) || count > budget.maxSubsteps)
        return Result<StepReport>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Crowd step exceeds its substep budget; reduce dt or speed", "dt"));
    report.substeps    = std::max(1, static_cast<int>(count));
    d.workReport       = {};
    const auto measure = [](std::int64_t& duration, auto&& work) {
        const auto start = std::chrono::steady_clock::now();
        work();
        duration +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    };
    const float substep = dt / float(report.substeps);
    for (int sub = 0; sub < report.substeps; ++sub) {
        d.simTime += substep;
        measure(report.gridNanoseconds, [&] { d.rebuildGrid(); });
        measure(report.motionNanoseconds, [&] { d.stepAgents(substep); });
        measure(report.wallNanoseconds, [&] { d.resolveWalls(); });
        if (d.resolveOverlaps) {
            for (int pass = 0; pass < budget.maxContactPasses; ++pass) {
                measure(report.gridNanoseconds, [&] { d.rebuildGrid(); });
                float penetration = 0.f;
                measure(report.contactNanoseconds, [&] { penetration = d.resolveOverlapsPass(); });
                measure(report.wallNanoseconds, [&] { d.resolveWalls(); });
                if (penetration < 1e-4f) break;
            }
        }
    }

    measure(report.gridNanoseconds, [&] { d.rebuildGrid(); });
    const auto  residualStart = std::chrono::steady_clock::now();
    const float maxRadius     = *std::max_element(d.radii.begin(), d.radii.end());
    for (size_t i = 0; i < d.xs.size(); ++i) {
        d.forEachNeighbor(d.xs[i], d.ys[i], d.radii[i] + maxRadius, [&](int neighbor) {
            const size_t j = size_t(neighbor);
            if (j <= i || !d.canContact(i, j)) return;
            if (report.residualPairsChecked >= budget.maxContactChecks - d.workReport.contactPairsChecked)
                throw WorkLimit{"Crowd final contact inspection exceeds its contact budget"};
            ++report.residualPairsChecked;
            const float depth     = d.radii[i] + d.radii[j] - std::hypot(d.xs[j] - d.xs[i], d.ys[j] - d.ys[i]);
            report.maxPenetration = std::max(report.maxPenetration, depth);
            if (depth > 0.001f) ++report.unresolvedContacts;
        });
        if (d.field.valid()) {
            float x = d.xs[i], y = d.ys[i];
            d.field.resolvePenetration(x, y, d.radii[i]);
            bool outside = false;
            if (d.clampToField) {
                outside = d.xs[i] - d.radii[i] < d.field.getOriginX() - 0.001f ||
                          d.ys[i] - d.radii[i] < d.field.getOriginY() - 0.001f ||
                          d.xs[i] + d.radii[i] >
                              d.field.getOriginX() + float(d.field.getWidth()) * d.field.getCellSize() + 0.001f ||
                          d.ys[i] + d.radii[i] >
                              d.field.getOriginY() + float(d.field.getHeight()) * d.field.getCellSize() + 0.001f;
            }
            if (outside || std::hypot(x - d.xs[i], y - d.ys[i]) > 0.001f) ++report.unresolvedWalls;
        }
    }
    report.residualNanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - residualStart).count();
    report.gridBuilds             = d.workReport.gridBuilds;
    report.gridCellsPeak          = d.workReport.gridCellsPeak;
    report.cellsVisited           = d.workReport.cellsVisited;
    report.gridEntriesPeak        = d.workReport.gridEntriesPeak;
    report.gridSortComparisons    = d.workReport.gridSortComparisons;
    report.spatialVisits          = d.workReport.spatialVisits;
    report.contactPairsPeak       = d.workReport.contactPairsPeak;
    report.pairVisits             = d.workReport.pairVisits;
    report.preferredVisits        = d.workReport.preferredVisits;
    report.preferredNeighborsPeak = d.workReport.preferredNeighborsPeak;
    report.candidatesVisited      = d.workReport.candidatesVisited;
    report.contactPairsChecked    = d.workReport.contactPairsChecked;
    report.contactPasses          = d.workReport.contactPasses;
    report.avoidanceChecks        = d.avoidanceChecks;
    report.avoidanceTruncations   = d.avoidanceTruncations;
    std::uint32_t quality         = 0;
    if (report.avoidanceTruncations > 0) quality |= static_cast<std::uint32_t>(SolveQuality::PredictionTruncated);
    if (report.unresolvedContacts > 0) quality |= static_cast<std::uint32_t>(SolveQuality::ContactResidual);
    if (report.unresolvedWalls > 0) quality |= static_cast<std::uint32_t>(SolveQuality::WallResidual);
    report.quality = static_cast<SolveQuality>(quality);
    return Result<StepReport>::success(report, Status::success(StatusCode::Applied));
}

void Crowd::step(float dt) { advance(dt).expect("Crowd step rejected"); }

}  // namespace eve::crowd
