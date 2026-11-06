// eve-hot-path
#include "graphics/multiscale/AngularView.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "graphics/multiscale/AngularProjectionInternal.h"
#include "graphics/multiscale/DirectedRounding.h"
#include "spatial/multiscale/CellPosition.h"

namespace eve::graphics::multiscale {
namespace {
constexpr double epsilon = std::numeric_limits<double>::epsilon();
double           down(double x) { return detail::roundDown(x); }
double           up(double x) { return detail::roundUp(x); }
Diagnostic       failure(DiagnosticCode code, const char* message) {
    return Diagnostic::error(code, message, "graphics.multiscale");
}
AngularBounds uncertain() { return {AngularVisibility::IntersectsCameraPlane, {-1, -1}, {1, 1}}; }
}  // namespace
namespace detail {
Result<PreparedAngularView> prepareAngularView(const AngularView& view) {
    if (view.cellExponent < -900 || view.cellExponent > 900 || !std::isfinite(view.tanHalfVerticalFov) ||
        view.tanHalfVerticalFov <= 0 || !std::isfinite(view.aspect) || view.aspect <= 0)
        return Result<PreparedAngularView>::failure(
            failure(DiagnosticCode::InvalidArgument, "Invalid sphere or perspective parameters"));
    const std::array<std::array<double, 3>, 3> basis{view.right, view.up, view.forward};
    std::array<double, 3>                      norms{};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = i; j < 3; ++j) {
            double dot = 0;
            for (unsigned k = 0; k < 3; ++k) {
                if (!std::isfinite(basis[i][k]) || !std::isfinite(basis[j][k]))
                    return Result<PreparedAngularView>::failure(
                        failure(DiagnosticCode::InvalidArgument, "Nonfinite view basis"));
                dot += basis[i][k] * basis[j][k];
            }
            if (!std::isfinite(dot) || std::abs(dot - (i == j ? 1.0 : 0.0)) > 1e-12)
                return Result<PreparedAngularView>::failure(
                    failure(DiagnosticCode::InvalidArgument, "View basis is not orthonormal"));
            if (i == j) norms[i] = up(std::sqrt(up(dot + 16 * epsilon)));
        }
    }
    return Result<PreparedAngularView>::success({view, basis, norms});
}
Result<ProjectedSphere> projectPreparedSphere(const spatial::CellPosition& center, const spatial::CellPosition& camera,
                                              double radiusMantissa, int radiusExponent,
                                              const PreparedAngularView& prepared) {
    if (!std::isfinite(radiusMantissa) || radiusMantissa <= 0 || radiusExponent < -900 || radiusExponent > 900)
        return Result<ProjectedSphere>::failure(
            failure(DiagnosticCode::InvalidArgument, "Invalid sphere or perspective parameters"));
    const auto& view  = prepared.view;
    const auto& basis = prepared.basis;
    const auto& norms = prepared.norms;
    // Project in lattice units, so even a 2^900 physical cell never becomes a float position.
    auto relative = spatial::projectCellRelative(center, camera, {view.cellExponent, view.cellExponent, 1e6});
    if (!relative.ok()) return Result<ProjectedSphere>::failure(relative.status());
    const double    radius = std::ldexp(radiusMantissa, radiusExponent - view.cellExponent);
    ProjectedSphere result;
    result.radius = radius;
    if (!std::isfinite(radius)) {
        result.bounds = uncertain();
        return Result<ProjectedSphere>::success(result);
    }
    const double          radiusUpper = up(radius);
    std::array<double, 3> lo{}, hi{};
    for (unsigned i = 0; i < 3; ++i) {
        double coordinate = 0, magnitude = 0, error = 0;
        for (unsigned k = 0; k < 3; ++k) {
            const double term = basis[i][k] * relative.value().offset[k];
            coordinate += term;
            magnitude = up(magnitude + std::abs(term));
            error     = up(error + up(std::abs(basis[i][k]) * relative.value().absoluteErrorBound[k]));
        }
        result.center[i]    = coordinate;
        error               = up(error + up(16 * epsilon * magnitude));
        const double extent = up(up(radiusUpper * norms[i]) + error);
        lo[i]               = down(coordinate - extent);
        hi[i]               = up(coordinate + extent);
    }
    if (hi[2] < 0) return Result<ProjectedSphere>::success(result);
    if (lo[2] <= 0 || !std::isfinite(hi[2])) {
        result.bounds = uncertain();
        return Result<ProjectedSphere>::success(result);
    }
    const double horizontal = view.tanHalfVerticalFov * view.aspect;
    if (!std::isfinite(horizontal) || horizontal <= 0)
        return Result<ProjectedSphere>::failure(
            failure(DiagnosticCode::Unsupported, "Perspective scale exceeds numerical range"));
    const std::array<double, 2> tangents{horizontal, view.tanHalfVerticalFov};
    auto&                       output = result.bounds;
    output.visibility                  = AngularVisibility::Projected;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const double denominatorLo = down(lo[2] * down(tangents[axis]));
        const double denominatorHi = up(hi[2] * up(tangents[axis]));
        if (denominatorLo <= 0 || !std::isfinite(denominatorHi))
            return Result<ProjectedSphere>::failure(
                failure(DiagnosticCode::Unsupported, "Projected denominator exceeds numerical range"));
        const std::array<double, 4> ratios{lo[axis] / denominatorLo, lo[axis] / denominatorHi, hi[axis] / denominatorLo,
                                           hi[axis] / denominatorHi};
        output.minimum[axis] = down(*std::min_element(ratios.begin(), ratios.end()));
        output.maximum[axis] = up(*std::max_element(ratios.begin(), ratios.end()));
        if (!std::isfinite(output.minimum[axis]) || !std::isfinite(output.maximum[axis]))
            return Result<ProjectedSphere>::failure(
                failure(DiagnosticCode::Unsupported, "Projected rectangle exceeds numerical range"));
    }
    if (output.maximum[0] < -1 || output.minimum[0] > 1 || output.maximum[1] < -1 || output.minimum[1] > 1)
        output.visibility = AngularVisibility::Outside;
    return Result<ProjectedSphere>::success(result);
}
}  // namespace detail
Result<AngularBounds> projectSphere(const spatial::CellPosition& center, const spatial::CellPosition& camera,
                                    double radiusMantissa, int radiusExponent, const AngularView& view) {
    if (!std::isfinite(radiusMantissa) || radiusMantissa <= 0 || radiusExponent < -900 || radiusExponent > 900)
        return Result<AngularBounds>::failure(
            failure(DiagnosticCode::InvalidArgument, "Invalid sphere or perspective parameters"));
    auto prepared = detail::prepareAngularView(view);
    if (!prepared.ok()) return Result<AngularBounds>::failure(prepared.status());
    auto projected = detail::projectPreparedSphere(center, camera, radiusMantissa, radiusExponent, prepared.value());
    if (!projected.ok()) return Result<AngularBounds>::failure(projected.status());
    return Result<AngularBounds>::success(projected.value().bounds);
}
}  // namespace eve::graphics::multiscale
