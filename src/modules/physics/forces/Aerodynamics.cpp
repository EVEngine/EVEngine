#include "physics/forces/ForceMath.h"

#include <algorithm>
#include <numbers>

namespace eve::physics {
using namespace force_detail;
namespace {
Result<double> angularComponent(double coefficient, double velocity, double inertia, double dt) {
    const double rate = coefficient / inertia * dt;
    if (!isFinite(rate)) return invalid<double>();
    const double torque = -velocity * (coefficient / (1 + rate));
    if (!isFinite(torque)) return invalid<double>();
    return Result<double>::success(torque);
}
}  // namespace
Result<double> ForceModels::angularDamping2D(const AngularDampingModel& m, double velocity, double inertia, double dt) {
    if (!isNonnegative(m.linear) || !isNonnegative(m.quadratic) || !isFinite(velocity) || !isPositive(inertia) ||
        !isPositive(dt))
        return invalid<double>();
    const double coefficient = m.linear + m.quadratic * std::abs(velocity);
    if (!isFinite(coefficient)) return invalid<double>();
    return angularComponent(coefficient, velocity, inertia, dt);
}
Result<ForceVector> ForceModels::angularDamping3D(const AngularDampingModel& m, ForceVector velocity,
                                                  ForceVector inertia, double dt) {
    if (!isNonnegative(m.linear) || !isNonnegative(m.quadratic) || !isFinite(velocity) || !isPositive(inertia.x) ||
        !isPositive(inertia.y) || !isPositive(inertia.z) || !isPositive(dt))
        return invalid<ForceVector>();
    const double coefficient = m.linear + m.quadratic * length(velocity);
    if (!isFinite(coefficient)) return invalid<ForceVector>();
    auto x = angularComponent(coefficient, velocity.x, inertia.x, dt);
    if (!x) return Result<ForceVector>::failure(x.status());
    auto y = angularComponent(coefficient, velocity.y, inertia.y, dt);
    if (!y) return Result<ForceVector>::failure(y.status());
    auto z = angularComponent(coefficient, velocity.z, inertia.z, dt);
    if (!z) return Result<ForceVector>::failure(z.status());
    return checked({x.value(), y.value(), z.value()});
}
Result<WingSample> ForceModels::wing(const WingModel& m, ForceVector velocity) {
    constexpr double pi = std::numbers::pi;
    if (!isFinite(velocity) || !isFinite(m.chord) || !isFinite(m.normal) || std::abs(length(m.chord) - 1) > 1e-5 ||
        std::abs(length(m.normal) - 1) > 1e-5 || std::abs(dot(m.chord, m.normal)) > 1e-5 || !isNonnegative(m.density) ||
        !isPositive(m.area) || !isNonnegative(m.liftSlope) || !isFinite(m.zeroLiftAngle) ||
        std::abs(m.zeroLiftAngle) >= pi / 2 || !isPositive(m.stallAngle) || m.stallAngle >= pi / 2 ||
        !isNonnegative(m.baseDrag) || !isPositive(m.aspectRatio) || !isPositive(m.efficiency) || m.efficiency > 1)
        return invalid<WingSample>();
    // Section model: spanwise flow creates no section lift/drag.
    const double forward = dot(velocity, m.chord), up = dot(velocity, m.normal);
    const auto   sectionVelocity = add(scale(m.chord, forward), scale(m.normal, up));
    const double speed           = std::hypot(forward, up);
    WingSample   out;
    if (speed == 0) return Result<WingSample>::success(out);
    out.angleOfAttack      = std::atan2(-up, forward);
    const double alpha     = std::remainder(out.angleOfAttack - m.zeroLiftAngle, 2 * pi);
    const double magnitude = std::abs(alpha);
    // Continuous falloff after stall, zero lift at 90 degrees and in reverse flow.
    const double lift      = magnitude <= m.stallAngle ? m.liftSlope * magnitude
                                                       : m.liftSlope * m.stallAngle *
                                                        std::max(0.0, (pi / 2 - magnitude) / (pi / 2 - m.stallAngle));
    out.liftCoefficient    = std::copysign(lift, alpha);
    const double stallDrag = magnitude > m.stallAngle ? 2 * std::pow(std::sin(alpha), 2) : 0;
    // Blend added pressure drag from zero at the stall threshold.
    const double blend  = std::clamp((magnitude - m.stallAngle) / (pi / 2 - m.stallAngle), 0.0, 1.0);
    out.dragCoefficient = m.baseDrag + out.liftCoefficient * out.liftCoefficient / (pi * m.aspectRatio * m.efficiency) +
                          stallDrag * blend;
    const auto   flow          = scale(sectionVelocity, 1 / speed);
    const auto   liftDirection = add(scale(m.normal, forward / speed), scale(m.chord, -up / speed));
    const double qArea         = 0.5 * m.density * speed * speed * m.area;
    out.force = add(scale(liftDirection, qArea * out.liftCoefficient), scale(flow, -qArea * out.dragCoefficient));
    if (!isFinite(out.force) || !isFinite(out.dragCoefficient) || !isFinite(out.liftCoefficient))
        return invalid<WingSample>();
    return Result<WingSample>::success(out);
}
}  // namespace eve::physics
