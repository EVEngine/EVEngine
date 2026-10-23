#include "physics/forces/ForceMath.h"

#include <algorithm>

namespace eve::physics {
using namespace force_detail;
namespace {
bool validRegion(const FieldRegion& r) {
    if (!isFinite(r.center) || !isFinite(r.radius) || !isNonnegative(r.edgeWidth) || !isFinite(r.halfExtents))
        return false;
    switch (r.kind) {
        case FieldRegionKind::Global: return true;
        case FieldRegionKind::Sphere: return r.radius < 0 || r.edgeWidth <= r.radius;
        case FieldRegionKind::RectangleXY:
            return isPositive(r.halfExtents.x) && isPositive(r.halfExtents.y) && r.halfExtents.z == 0 &&
                   r.edgeWidth <= std::min(r.halfExtents.x, r.halfExtents.y);
        case FieldRegionKind::Box:
            return isPositive(r.halfExtents.x) && isPositive(r.halfExtents.y) && isPositive(r.halfExtents.z) &&
                   r.edgeWidth <= std::min({r.halfExtents.x, r.halfExtents.y, r.halfExtents.z});
    }
    return false;
}
double weight(const FieldRegion& r, ForceVector position) {
    if (r.kind == FieldRegionKind::Global || (r.kind == FieldRegionKind::Sphere && r.radius < 0)) return 1;
    const auto delta    = add(position, scale(r.center, -1));
    double     interior = 0;
    if (r.kind == FieldRegionKind::Sphere)
        interior = r.radius - length(delta);
    else {
        interior = std::min(r.halfExtents.x - std::abs(delta.x), r.halfExtents.y - std::abs(delta.y));
        if (r.kind == FieldRegionKind::Box) interior = std::min(interior, r.halfExtents.z - std::abs(delta.z));
    }
    if (interior <= 0) return 0;
    if (r.edgeWidth == 0) return 1;
    return std::clamp(interior / r.edgeWidth, 0.0, 1.0);
}
}  // namespace
Result<ForceVector> ForceModels::directional(const DirectionalField& m, ForceVector p, double mass) {
    if (!validRegion(m.region) || !isFinite(m.acceleration) || !isFinite(p) || !isPositive(mass))
        return invalid<ForceVector>();
    return checked(scale(m.acceleration, mass * weight(m.region, p)));
}
Result<ForceVector> ForceModels::directionalForce(const DirectionalForceField& m, ForceVector p) {
    if (!validRegion(m.region) || !isFinite(m.force) || !isFinite(p)) return invalid<ForceVector>();
    return checked(scale(m.force, weight(m.region, p)));
}
Result<ForceVector> ForceModels::radial(const RadialField& m, ForceVector p, double mass) {
    if (!validRegion(m.region) || !isFinite(m.strength) || !isPositive(m.softening) || !isFinite(p) ||
        !isPositive(mass) || !isNonnegative(m.maxAcceleration) || !isNonnegative(m.maxForce))
        return invalid<ForceVector>();
    const auto   delta    = add(m.region.center, scale(p, -1));
    const double distance = length(delta), radius = std::hypot(distance, m.softening);
    if (!isFinite(delta) || !isFinite(distance) || !isFinite(radius)) return invalid<ForceVector>();
    const double fade = weight(m.region, p);
    if (distance == 0 || m.strength == 0 || fade == 0 || m.maxAcceleration == 0 || m.maxForce == 0) return checked({});
    // Compare magnitudes in log space so tiny softened cores cannot overflow before the caps apply.
    const double logMass = std::log(mass);
    const double logForce =
        std::log(std::abs(m.strength)) + std::log(distance) - 3 * std::log(radius) + logMass + std::log(fade);
    const double cappedLog = std::min({logForce, std::log(m.maxAcceleration) + logMass, std::log(m.maxForce)});
    const double magnitude = std::min(std::exp(cappedLog), m.maxForce);
    const double sign      = m.strength < 0 ? -1 : 1;
    // Divide components separately: reciprocal distance may overflow for subnormal positions.
    return checked({sign * (delta.x / distance) * magnitude, sign * (delta.y / distance) * magnitude,
                    sign * (delta.z / distance) * magnitude});
}
Result<ForceVector> ForceModels::buoyancy(const BuoyancyModel& m) {
    if (!isFinite(m.gravity) || !isNonnegative(m.fluidDensity) || !isNonnegative(m.displacedVolume))
        return invalid<ForceVector>();
    return checked(scale(m.gravity, -m.fluidDensity * m.displacedVolume));
}
Result<ForceVector> ForceModels::damping(const DampingModel& m, ForceVector velocity, double mass, double dt) {
    if (!isNonnegative(m.linear) || !isNonnegative(m.quadratic) || !isFinite(velocity) || !isPositive(mass) ||
        !isPositive(dt))
        return invalid<ForceVector>();
    const double coefficient = m.linear + m.quadratic * length(velocity);
    const double rate        = coefficient / mass * dt;
    if (!isFinite(coefficient) || !isFinite(rate)) return invalid<ForceVector>();
    return checked(scale(velocity, -coefficient / (1 + rate)));
}
Result<ForceVector> ForceModels::friction(const FrictionModel& m, ForceVector velocity, ForceVector demand,
                                          double load) {
    if (!isNonnegative(m.kineticCoefficient) || !isFinite(m.staticCoefficient) ||
        m.staticCoefficient < m.kineticCoefficient || !isFinite(velocity) || !isFinite(demand) || !isNonnegative(load))
        return invalid<ForceVector>();
    const double speed = length(velocity), required = length(demand);
    if (!isFinite(speed) || !isFinite(required) || !isFinite(m.staticCoefficient * load)) return invalid<ForceVector>();
    if (speed > 0) return checked(scale(velocity, -m.kineticCoefficient * load / speed));
    if (required <= m.staticCoefficient * load) return checked(scale(demand, -1));
    return checked(scale(demand, -m.kineticCoefficient * load / required));
}
}  // namespace eve::physics
