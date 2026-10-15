#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <vector>
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/mechanics/Coupling3DInternal.h"

namespace eve::physics {
namespace {
Result<void> rejected(const char* message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message, "physics.coupling3d.step"));
}
struct PhaseGuard {
    bool& active;
    explicit PhaseGuard(bool& value) : active(value) { active = true; }
    ~PhaseGuard() { active = false; }
};
struct Velocity {
    b3BodyId  body;
    b3Vec3    originalLinear, originalAngular, linear, angular;
    b3Matrix3 inverseInertia;
    float     inverseMass;
    bool      dynamic, awake, changed = false;
};
bool same(b3Vec3 a, b3Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
struct VelocityTransaction {
    std::vector<Velocity> values;
    bool                  applied = false, committed = false;
    void                  apply() {
        applied = true;
        for (auto& v : values)
            if (v.dynamic && (!same(v.linear, v.originalLinear) || !same(v.angular, v.originalAngular))) {
                v.changed = true;
                b3Body_SetAwake(v.body, true);
                b3Body_SetLinearVelocity(v.body, v.linear);
                b3Body_SetAngularVelocity(v.body, v.angular);
            }
    }
    ~VelocityTransaction() {
        if (!applied || committed) return;
        for (const auto& v : values)
            if (v.changed && b3Body_IsValid(v.body)) {
                b3Body_SetLinearVelocity(v.body, v.originalLinear);
                b3Body_SetAngularVelocity(v.body, v.originalAngular);
                b3Body_SetAwake(v.body, v.awake);
            }
    }
};
}  // namespace
Result<void> World3D::stepCoupledBackend(const SimulationStep& step, const SimulationSettings& settings) {
    PhaseGuard   phase(couplingStepActive_);
    const double dt = step.delta.seconds();
    if (couplings3D_.empty() || dt == 0) return simulation_->step(step, settings);
    struct Prepared {
        Coupling3D*                owner;
        coupling_detail::Row       row;
        std::array<std::size_t, 4> index{};
        double                     response = 0, impulse = 0, maximum = 0, bias = 0;
        bool                       tensionOnly = false;
    };
    std::vector<Prepared>                              rows;
    VelocityTransaction                                transaction;
    std::unordered_map<PhysicsBodyHandle, std::size_t> indices;
    rows.reserve(couplings3D_.size());
    for (auto [id, coupling] : couplings3D_) {
        auto& data   = *coupling->data_;
        auto  result = data.row();
        if (!result) return Result<void>::failure(result.status());
        Prepared p;
        p.owner           = coupling;
        p.row             = std::move(result).takeValue();
        p.maximum         = data.limits().maxForce * dt;
        p.tensionOnly     = !data.gear && data.pulleySettings.tensionOnly;
        const double gain = p.tensionOnly && p.row.error < 0 ? 1.0 : data.limits().positionCorrection;
        p.bias            = std::clamp(gain * p.row.error / dt, -static_cast<double>(data.limits().maxCorrectionSpeed),
                                       static_cast<double>(data.limits().maxCorrectionSpeed));
        for (int i = 0; i < p.row.count; ++i) {
            const auto& term  = p.row.terms[i];
            auto        found = indices.find(term.body);
            if (found == indices.end()) {
                auto* body = findBody(term.body);
                if (!body || !body->isValid()) return rejected("Coupling body became stale during preparation");
                const auto  raw    = eve::physics::box3d::Access::bodyId(*body);
                const auto  linear = b3Body_GetLinearVelocity(raw), angular = b3Body_GetAngularVelocity(raw);
                const auto  inertia = b3Body_GetWorldInverseRotationalInertia(raw);
                const float mass    = b3Body_GetInverseMass(raw);
                if (!b3IsValidVec3(linear) || !b3IsValidVec3(angular) || !std::isfinite(mass) || mass < 0 ||
                    !b3IsValidVec3(inertia.cx) || !b3IsValidVec3(inertia.cy) || !b3IsValidVec3(inertia.cz))
                    return rejected("Non-finite coupling body state");
                const auto index = transaction.values.size();
                transaction.values.push_back({raw, linear, angular, linear, angular, inertia, mass,
                                              b3Body_GetType(raw) == b3_dynamicBody, b3Body_IsAwake(raw)});
                found = indices.emplace(term.body, index).first;
            }
            p.index[i]    = found->second;
            const auto& v = transaction.values[found->second];
            if (v.dynamic)
                p.response += v.inverseMass * b3Dot(term.linear, term.linear) +
                              b3Dot(term.angular, b3MulMV(v.inverseInertia, term.angular));
        }
        if (p.row.active && (!std::isfinite(p.response) || p.response <= 0))
            return rejected("Coupling has no finite movable degree of freedom");
        rows.push_back(std::move(p));
    }
    // Project all transmissions together. Shared endpoints accumulate one Jacobian before effective-mass evaluation.
    for (int iteration = 0; iteration < settings.velocityIterations; ++iteration)
        for (auto& p : rows) {
            if (!p.row.active) continue;
            double speed = 0;
            for (int i = 0; i < p.row.count; ++i) {
                const auto& v = transaction.values[p.index[i]];
                const auto& t = p.row.terms[i];
                speed += b3Dot(t.linear, v.linear) + b3Dot(t.angular, v.angular);
            }
            const double next =
                std::clamp(p.impulse - (speed + p.bias) / p.response, -p.maximum, p.tensionOnly ? 0.0 : p.maximum);
            const float impulse = static_cast<float>(next - p.impulse);
            if (!std::isfinite(impulse)) return rejected("Coupling impulse overflow");
            p.impulse = next;
            for (int i = 0; i < p.row.count; ++i) {
                auto&       v = transaction.values[p.index[i]];
                const auto& t = p.row.terms[i];
                if (!v.dynamic) continue;
                v.linear  = b3Add(v.linear, b3MulSV(impulse * v.inverseMass, t.linear));
                v.angular = b3Add(v.angular, b3MulMV(v.inverseInertia, b3MulSV(impulse, t.angular)));
                if (!b3IsValidVec3(v.linear) || !b3IsValidVec3(v.angular))
                    return rejected("Coupling velocity overflow");
            }
        }
    for (const auto& p : rows) {
        const auto& d = *p.owner->data_;
        if (!d.gear) continue;
        for (int i = 0; i < 2; ++i)
            if (d.revolute[i]) {
                const auto& a = transaction.values[indices.at(d.bodies[2 * i])];
                const auto& b = transaction.values[indices.at(d.bodies[2 * i + 1])];
                if (b3Length(b3Sub(b.angular, a.angular)) * dt >= std::numbers::pi)
                    return rejected("Revolute coupling requires less than pi radians of relative rotation per step");
            }
    }
    transaction.apply();
    auto result = simulation_->step(step, settings);
    if (!result) return result;
    transaction.committed = true;
    for (const auto& p : rows) {
        auto& d       = *p.owner->data_;
        d.previous    = p.row.wrapped;
        d.coordinate  = p.row.coordinate;
        d.lastImpulse = p.impulse;
        d.saturated   = p.row.active && std::abs(p.impulse) >= p.maximum;
    }
    return result;
}
}  // namespace eve::physics
