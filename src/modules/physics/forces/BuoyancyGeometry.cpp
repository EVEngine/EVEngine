#include "physics/forces/BuoyancyGeometry.h"
#include <algorithm>
#include <numbers>
#include "physics/forces/ForceMath.h"

namespace eve::physics {
namespace {
using namespace force_detail;
constexpr double        pi = std::numbers::pi;
Result<ImmersionSample> finish(ForceVector center, double total, double fraction, double offsetY) {
    if (!isPositive(total) || !isNonnegative(fraction) || fraction > 1 || !isFinite(offsetY))
        return invalid<ImmersionSample>();
    const double volume = total * fraction;
    if (!isNonnegative(volume)) return invalid<ImmersionSample>();
    if (volume == 0) return Result<ImmersionSample>::success({0, 0, center});
    center.y += offsetY;
    if (!isFinite(center)) return invalid<ImmersionSample>();
    return Result<ImmersionSample>::success({volume, fraction, center});
}
bool inputs(ForceVector center, double surfaceY) {
    return isFinite(center) && isFinite(surfaceY) && isFinite(surfaceY - center.y);
}
}  // namespace
Result<ImmersionSample> BuoyancyGeometry::sphere3D(ForceVector center, double radius, double surfaceY) {
    if (!inputs(center, surfaceY) || !isPositive(radius)) return invalid<ImmersionSample>();
    const double total = (4 * pi / 3) * radius * radius * radius;
    const double h     = std::clamp((surfaceY - center.y) / radius + 1, 0.0, 2.0);
    // h is cap height/radius. Integrating pi*(r^2-y^2) gives volume and first moment.
    const double fraction = h * h * (3 - h) / 4;
    const double offset   = -radius * (3 * (2 - h) * (2 - h) / (4 * (3 - h)));
    return finish(center, total, fraction, offset);
}
Result<ImmersionSample> BuoyancyGeometry::circle2D(ForceVector center, double radius, double thickness,
                                                   double surfaceY) {
    if (!inputs(center, surfaceY) || center.z != 0 || !isPositive(radius) || !isPositive(thickness))
        return invalid<ImmersionSample>();
    const double total = pi * radius * radius * thickness;
    const double h     = std::clamp((surfaceY - center.y) / radius + 1, 0.0, 2.0);
    if (h == 0 || h == 2) return finish(center, total, h / 2, 0);
    // Evaluate the smaller segment to avoid cancellation near full immersion.
    const double small = std::min(h, 2 - h);
    const double theta = 2 * std::asin(std::sqrt(small / 2));
    const double sine  = std::sqrt(small * (2 - small));
    double       area  = theta - (1 - small) * sine;
    if (theta < .01) {
        const double q = theta * theta;
        area           = (2.0 / 3) * theta * q * (1 - q / 5 + 2 * q * q / 105 - q * q * q / 945);
    }
    const double wetArea = h <= 1 ? area : pi - area;
    if (wetArea == 0) return finish(center, total, 0, 0);
    const double offset = -radius * ((2.0 / 3) * sine * sine * sine / wetArea);
    return finish(center, total, wetArea / pi, offset);
}
Result<ImmersionSample> BuoyancyGeometry::box3D(ForceVector center, ForceVector size, double surfaceY) {
    if (!inputs(center, surfaceY) || !isPositive(size.x) || !isPositive(size.y) || !isPositive(size.z))
        return invalid<ImmersionSample>();
    const double total    = size.x * size.y * size.z;
    const double fraction = std::clamp((surfaceY - center.y) / size.y + .5, 0.0, 1.0);
    return finish(center, total, fraction, -size.y * .5 * (1 - fraction));
}
Result<ImmersionSample> BuoyancyGeometry::rectangle2D(ForceVector center, double width, double height, double thickness,
                                                      double surfaceY) {
    if (center.z != 0) return invalid<ImmersionSample>();
    return box3D(center, {width, height, thickness}, surfaceY);
}
}  // namespace eve::physics
