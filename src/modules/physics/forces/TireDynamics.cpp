#include <algorithm>
#include "physics/forces/ForceMath.h"

namespace eve::physics {
using namespace force_detail;
Result<TireSample> ForceModels::tire(const TireModel& m, const TireInput& in, double dt) {
    if (!isPositive(m.radius) || !isPositive(m.inertia) || !isNonnegative(m.friction) ||
        !isNonnegative(m.longitudinalStiffness) || !isNonnegative(m.corneringStiffness) ||
        !isNonnegative(m.rollingResistance) || !isPositive(m.minimumSlipSpeed) || !isPositive(dt) ||
        !isFinite(in.longitudinalVelocity) || !isFinite(in.lateralVelocity) || !isFinite(in.angularVelocity) ||
        !isNonnegative(in.normalLoad) || !isFinite(in.driveTorque) || !isNonnegative(in.brakeTorque) ||
        !isNonnegative(in.longitudinalInverseMass) || !isNonnegative(in.lateralInverseMass))
        return invalid<TireSample>();
    const double freeOmega    = in.angularVelocity + in.driveTorque / m.inertia * dt;
    const double surfaceSpeed = m.radius * freeOmega;
    const double denominator =
        std::max({m.minimumSlipSpeed, std::abs(in.longitudinalVelocity), std::abs(surfaceSpeed)});
    const double kx           = m.longitudinalStiffness / denominator;
    const double lateralSpeed = std::max(std::abs(in.longitudinalVelocity), m.minimumSlipSpeed);
    const double ky           = in.lateralVelocity == 0
                                    ? m.corneringStiffness / lateralSpeed
                                    : m.corneringStiffness * std::atan2(in.lateralVelocity, lateralSpeed) / in.lateralVelocity;
    const double effectiveX   = in.longitudinalInverseMass + m.radius * m.radius / m.inertia;
    const double dx = 1 + dt * kx * effectiveX, dy = 1 + dt * ky * in.lateralInverseMass;
    const double gainX = dt * kx / dx, gainY = dt * ky / dy;
    const double capacity   = m.friction * in.normalLoad * dt;
    const double resistance = (in.brakeTorque + m.rollingResistance * in.normalLoad * m.radius) * dt;
    if (!isFinite(freeOmega) || !isFinite(surfaceSpeed) || !isFinite(kx) || !isFinite(ky) || !isPositive(effectiveX) ||
        !isFinite(dx) || !isFinite(dy) || !isFinite(gainX) || !isFinite(gainY) || !isFinite(capacity) ||
        !isFinite(resistance))
        return invalid<TireSample>();
    struct Contact {
        double x, y, omega;
    };
    auto solve = [&](double brakeImpulse) {
        double       x    = gainX * (surfaceSpeed - in.longitudinalVelocity + m.radius * brakeImpulse / m.inertia);
        double       y    = -gainY * in.lateralVelocity;
        const double norm = std::hypot(x, y);
        if (norm > capacity) {
            x *= capacity / norm;
            y *= capacity / norm;
        }
        return Contact{x, y, freeOmega + (brakeImpulse - m.radius * x) / m.inertia};
    };
    double  brake   = 0;
    Contact contact = solve(0);
    if (!isFinite(contact.x) || !isFinite(contact.y) || !isFinite(contact.omega)) return invalid<TireSample>();
    if (resistance > 0 && contact.omega != 0) {
        // A locking solution cannot need more than all wheel momentum plus maximum contact angular impulse.
        const double bound = std::abs(m.inertia * freeOmega) + m.radius * capacity;
        if (!isFinite(bound)) return invalid<TireSample>();
        brake              = -std::copysign(std::min(resistance, bound), contact.omega);
        const auto limited = solve(brake);
        if (!isFinite(limited.omega)) return invalid<TireSample>();
        if (contact.omega * limited.omega > 0)
            contact = limited;
        else {
            double low = std::min(0.0, brake), high = std::max(0.0, brake);
            for (int i = 0; i < 64; ++i) {
                brake   = low * .5 + high * .5;
                contact = solve(brake);
                if (!isFinite(contact.omega)) return invalid<TireSample>();
                if (contact.omega > 0)
                    high = brake;
                else
                    low = brake;
            }
            const double tolerance =
                1e-10 * std::max({1.0, std::abs(freeOmega), std::abs(m.radius * contact.x / m.inertia)});
            if (std::abs(contact.omega) > tolerance) return invalid<TireSample>();
            contact.omega = 0;
        }
    }
    TireSample out;
    out.longitudinalForce = contact.x / dt;
    out.lateralForce      = contact.y / dt;
    out.angularVelocity   = contact.omega;
    out.reactionTorque    = -out.longitudinalForce * m.radius;
    out.resistanceTorque  = brake / dt;
    const double vx       = in.longitudinalVelocity + contact.x * in.longitudinalInverseMass;
    const double vy       = in.lateralVelocity + contact.y * in.lateralInverseMass;
    out.slipRatio         = (m.radius * out.angularVelocity - vx) / denominator;
    out.slipAngle         = std::atan2(vy, std::max(std::abs(vx), m.minimumSlipSpeed));
    if (!isFinite(out.longitudinalForce) || !isFinite(out.lateralForce) || !isFinite(out.angularVelocity) ||
        !isFinite(out.reactionTorque) || !isFinite(out.resistanceTorque) || !isFinite(out.slipRatio) ||
        !isFinite(out.slipAngle) || !isFinite(vx) || !isFinite(vy))
        return invalid<TireSample>();
    return Result<TireSample>::success(out);
}
}  // namespace eve::physics
