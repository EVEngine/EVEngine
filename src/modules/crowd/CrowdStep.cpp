#include "common/Profile.h"
#include "crowd/CrowdInternal.h"

namespace eve::crowd {

Result<StepReport> Crowd::advance(float dt) {
    EV_PROFILE_MODULE("crowd", "Crowd::advance");
    if (!std::isfinite(dt) || dt < 0.f)
        return Result<StepReport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Crowd dt must be finite and non-negative", "dt"));
    StepReport report;
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
    if (!std::isfinite(count) || count > 1024.0)
        return Result<StepReport>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Crowd step exceeds 1024 substeps; reduce dt or speed", "dt"));
    report.substeps     = std::max(1, static_cast<int>(count));
    const float substep = dt / float(report.substeps);
    for (int sub = 0; sub < report.substeps; ++sub) {
        d.simTime += substep;
        d.rebuildGrid();
        d.stepAgents(substep);
        d.resolveWalls();
        if (d.resolveOverlaps) {
            for (int pass = 0; pass < 12; ++pass) {
                d.rebuildGrid();
                const float penetration = d.resolveOverlapsPass();
                d.resolveWalls();
                if (penetration < 1e-4f) break;
            }
        }
    }

    d.rebuildGrid();
    const float maxRadius = *std::max_element(d.radii.begin(), d.radii.end());
    for (size_t i = 0; i < d.xs.size(); ++i) {
        d.forEachNeighbor(d.xs[i], d.ys[i], d.radii[i] + maxRadius, [&](int neighbor) {
            const size_t j = size_t(neighbor);
            if (j <= i || !d.canInteract(i, j)) return;
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
    report.avoidanceChecks      = d.avoidanceChecks;
    report.avoidanceTruncations = d.avoidanceTruncations;
    return Result<StepReport>::success(report, Status::success(StatusCode::Applied));
}

void Crowd::step(float dt) { advance(dt).expect("Crowd step rejected"); }

}  // namespace eve::crowd
