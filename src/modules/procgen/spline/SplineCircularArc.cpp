#include "procgen/spline/SplineCircularArc.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include "common/Assert.h"

namespace eve::procgen::detail {
Result<std::array<float, 3>> scaleCircularTangentToChord(const CircularArcSampleXZ& tangent, double heightDerivative,
                                                         const std::array<double, 3>& chord) {
    const double magnitude = std::hypot(tangent.dx, heightDerivative, tangent.dz);
    const double distance  = std::hypot(chord[0], chord[1], chord[2]);
    if (!std::isfinite(magnitude) || !std::isfinite(distance) || magnitude <= 1e-12 || distance <= 1e-12 ||
        distance > std::numeric_limits<float>::max())
        return Result<std::array<float, 3>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "circular neighbor requires a finite representable chord and nonzero tangent", "circularNeighbor"));
    const double factor = distance / magnitude;
    return Result<std::array<float, 3>>::success({static_cast<float>(tangent.dx * factor),
                                                  static_cast<float>(heightDerivative * factor),
                                                  static_cast<float>(tangent.dz * factor)});
}
Result<CircularArcXZ> makeCircularArcXZ(double ax, double az, double bx, double bz, double dx, double dz,
                                        CircularArcReference reference) {
    auto invalid = [] {
        return Result<CircularArcXZ>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "circular span requires finite separated XZ endpoints and a finite realizable tangent", "circularArc"));
    };
    if (reference != CircularArcReference::Start && reference != CircularArcReference::End) return invalid();
    if (!std::isfinite(ax) || !std::isfinite(az) || !std::isfinite(bx) || !std::isfinite(bz) || !std::isfinite(dx) ||
        !std::isfinite(dz))
        return invalid();
    const double x = bx - ax, z = bz - az, chord = std::hypot(x, z), direction = std::hypot(dx, dz);
    if (chord < 1e-7 || direction < 1e-7) return invalid();
    double cross = dx * z - dz * x;
    if (reference == CircularArcReference::End) cross = -cross;
    const double theta = std::atan2(cross, dx * x + dz * z);
    if (std::abs(theta) > std::numbers::pi - 1e-8) return invalid();
    CircularArcXZ arc;
    arc.ax = ax;
    arc.az = az;
    arc.bx = bx;
    arc.bz = bz;
    if (std::abs(theta) < 1e-8) return Result<CircularArcXZ>::success(arc);
    const double bulge = std::tan(theta * .5), offset = (1 - bulge * bulge) / (4 * bulge);
    arc.line             = false;
    arc.cx               = (ax + bx) * .5 - z * offset;
    arc.cz               = (az + bz) * .5 + x * offset;
    arc.radius           = chord * (1 + bulge * bulge) / (4 * std::abs(bulge));
    arc.angle            = std::atan2(az - arc.cz, ax - arc.cx);
    arc.sweep            = 4 * std::atan(bulge);
    const double maximum = std::numeric_limits<float>::max();
    if (!std::isfinite(arc.radius) || arc.radius * std::abs(arc.sweep) > maximum ||
        std::abs(arc.cx) + arc.radius > maximum || std::abs(arc.cz) + arc.radius > maximum)
        return invalid();
    return Result<CircularArcXZ>::success(arc);
}
Result<CircularArcXZ> makeCircularLeadArcXZ(double ax, double az, double bx, double bz, double dx, double dz,
                                            double endDx, double endDz) {
    auto base = makeCircularArcXZ(ax, az, bx, bz, dx, dz, CircularArcReference::Start);
    if (!base.ok()) return base;
    const double startMagnitude = std::hypot(dx, dz), endMagnitude = std::hypot(endDx, endDz);
    if (std::isfinite(startMagnitude) && std::isfinite(endMagnitude) && startMagnitude > 1e-7 && endMagnitude > 1e-7) {
        dx /= startMagnitude;
        dz /= startMagnitude;
        endDx /= endMagnitude;
        endDz /= endMagnitude;
        const double nx = -dz + endDz, nz = dx - endDx;
        const double determinant = dx * nz - dz * nx;
        if (std::abs(determinant) > 1e-8) {
            const double lead = ((bx - ax) * nz - (bz - az) * nx) / determinant;
            if (std::isfinite(lead) && lead < 0)
                return makeCircularArcXZ(ax, az, bx, bz, bx - ax, bz - az, CircularArcReference::Start);
            if (std::isfinite(lead) && lead >= 0 && lead <= std::numeric_limits<float>::max()) {
                auto result =
                    makeCircularArcXZ(ax + lead * dx, az + lead * dz, bx, bz, dx, dz, CircularArcReference::Start);
                if (!result.ok()) return result;
                result.value().leadX      = dx;
                result.value().leadZ      = dz;
                result.value().leadLength = lead;
                return result;
            }
        }
    }
    return base;
}
CircularArcSampleXZ sampleCircularArcXZ(const CircularArcXZ& arc, double u) {
    EV_PARAM_CHECK(std::isfinite(u), "arc parameter must be finite");
    u = std::clamp(u, 0., 1.);
    if (arc.reversed) {
        auto forward     = arc;
        forward.reversed = false;
        auto result      = sampleCircularArcXZ(forward, 1 - u);
        result.dx        = -result.dx;
        result.dz        = -result.dz;
        return result;
    }
    if (arc.leadLength > 0) {
        const double curveLength =
            arc.line ? std::hypot(arc.bx - arc.ax, arc.bz - arc.az) : arc.radius * std::abs(arc.sweep);
        const double total = arc.leadLength + curveLength, distance = u * total;
        if (distance < arc.leadLength)
            return {arc.ax - (arc.leadLength - distance) * arc.leadX, arc.az - (arc.leadLength - distance) * arc.leadZ,
                    total * arc.leadX, total * arc.leadZ};
        auto curve       = arc;
        curve.leadLength = 0;
        auto result      = sampleCircularArcXZ(curve, (distance - arc.leadLength) / curveLength);
        result.dx *= total / curveLength;
        result.dz *= total / curveLength;
        return result;
    }
    if (arc.line) return {std::lerp(arc.ax, arc.bx, u), std::lerp(arc.az, arc.bz, u), arc.bx - arc.ax, arc.bz - arc.az};
    const double angle = arc.angle + arc.sweep * u, c = std::cos(angle), s = std::sin(angle),
                 speed = arc.radius * arc.sweep;
    CircularArcSampleXZ result{arc.cx + arc.radius * c, arc.cz + arc.radius * s, -speed * s, speed * c};
    if (u == 0) {
        result.x = arc.ax;
        result.z = arc.az;
    }
    if (u == 1) {
        result.x = arc.bx;
        result.z = arc.bz;
    }
    return result;
}
}  // namespace eve::procgen::detail
