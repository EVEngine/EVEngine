#include "physics/forces/ForceMath.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"

#include <limits>

namespace eve::physics {
using namespace force_detail;
namespace {
bool fits(ForceVector v) {
    constexpr double limit = std::numeric_limits<float>::max();
    return isFinite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit;
}
}  // namespace
Result<void> applyForceSI(Body& body, ForceVector force, ForceVector point) {
    if (!body.isValid()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Dead 2D body"));
    if (!body.isActive() || body.getType() != "dynamic" || force.z != 0 || point.z != 0 || !fits(force) || !fits(point))
        return invalid<void>();
    const double meter = body.getWorld()->getMeter();
    force              = scale(force, meter);
    point              = scale(point, meter);
    if (!isPositive(meter) || !fits(force) || !fits(point)) return invalid<void>();
    body.applyForceAt(static_cast<float>(force.x), static_cast<float>(force.y), static_cast<float>(point.x),
                      static_cast<float>(point.y));
    return Result<void>::success();
}
Result<void> applyForceSI(Body3D& body, ForceVector force, ForceVector point) {
    if (!body.isValid()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Dead 3D body"));
    if (!body.isActive() || body.getType() != "dynamic" || !fits(force) || !fits(point)) return invalid<void>();
    body.applyForceAt(static_cast<float>(force.x), static_cast<float>(force.y), static_cast<float>(force.z),
                      static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z));
    return Result<void>::success();
}
}  // namespace eve::physics
