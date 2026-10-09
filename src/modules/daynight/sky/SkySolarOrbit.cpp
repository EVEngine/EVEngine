#include "daynight/sky/SkySolarOrbit.h"
#include <cmath>
#include <numbers>

namespace eve::daynight {
namespace {
Diagnostic invalidOrbit(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "sky.solar-orbit");
}
}  // namespace
SkySolarOrbit::SkySolarOrbit(double pitch, double yaw)
    : sinPitch_(std::sin(pitch)), cosPitch_(std::cos(pitch)), sinYaw_(std::sin(yaw)), cosYaw_(std::cos(yaw)) {}

Result<SkySolarOrbit> SkySolarOrbit::create(double pitchDegrees, double yawDegrees) {
    if (!std::isfinite(pitchDegrees) || std::abs(pitchDegrees) > 90 || !std::isfinite(yawDegrees) ||
        std::abs(yawDegrees) > 360)
        return Result<SkySolarOrbit>::failure(invalidOrbit("Invalid solar orbit pitch or yaw"));
    constexpr double radians = std::numbers::pi / 180;
    return Result<SkySolarOrbit>::success(SkySolarOrbit(pitchDegrees * radians, yawDegrees * radians));
}

Result<std::array<double, 3>> SkySolarOrbit::direction(double hour) const {
    using Direction = Result<std::array<double, 3>>;
    if (!std::isfinite(hour) || hour < 0 || hour >= 24)
        return Direction::failure(invalidOrbit("Solar hour must be finite and in [0,24)"));
    const double phase = (hour - 12) * (std::numbers::pi / 12);
    const double noon  = std::cos(phase);
    const double x     = -sinPitch_ * noon;
    const double z     = -std::sin(phase);
    return Direction::success({cosYaw_ * x - sinYaw_ * z, cosPitch_ * noon, sinYaw_ * x + cosYaw_ * z});
}
}  // namespace eve::daynight
