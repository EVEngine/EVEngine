#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include "common/Assert.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/mechanics/Coupling3DInternal.h"

namespace eve::physics {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "physics.coupling3d"));
}
bool finite(CouplingPoint3D p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool valid(const CouplingLimits3D& s) {
    return std::isfinite(s.positionCorrection) && s.positionCorrection >= 0 && s.positionCorrection <= 1 &&
           std::isfinite(s.maxCorrectionSpeed) && s.maxCorrectionSpeed > 0 && std::isfinite(s.maxForce) &&
           s.maxForce > 0;
}
b3Vec3 vec(CouplingPoint3D p) { return {p.x, p.y, p.z}; }
b3Pos  pos(CouplingPoint3D p) { return {p.x, p.y, p.z}; }
}  // namespace
void coupling_detail::Row::add(PhysicsBodyHandle body, b3Vec3 linear, b3Vec3 angular, float scale) {
    for (int i = 0; i < count; ++i)
        if (terms[i].body == body) {
            terms[i].linear  = b3Add(terms[i].linear, b3MulSV(scale, linear));
            terms[i].angular = b3Add(terms[i].angular, b3MulSV(scale, angular));
            return;
        }
    terms[count++] = {body, b3MulSV(scale, linear), b3MulSV(scale, angular)};
}
Result<void> coupling_detail::validateResponse(World3D& world, const Row& row) {
    if (!row.active) return Result<void>::success();
    double response = 0;
    for (int i = 0; i < row.count; ++i) {
        const auto& t    = row.terms[i];
        auto*       body = world.findBody(t.body);
        if (!body || !body->isValid()) return fail<void>(DiagnosticCode::StaleHandle, "Coupling endpoint is stale");
        if (b3Body_GetType(eve::physics::box3d::Access::bodyId(*body)) != b3_dynamicBody) continue;
        response += b3Body_GetInverseMass(eve::physics::box3d::Access::bodyId(*body)) * b3Dot(t.linear, t.linear) +
                    b3Dot(t.angular,
                          b3MulMV(b3Body_GetWorldInverseRotationalInertia(eve::physics::box3d::Access::bodyId(*body)),
                                  t.angular));
    }
    if (!std::isfinite(response) || response <= 0)
        return fail<void>(DiagnosticCode::InvalidArgument, "Coupling has no finite movable degree of freedom");
    return Result<void>::success();
}
Result<coupling_detail::Row> Coupling3D::Data::row() const {
    using R = coupling_detail::Row;
    if (!world || !world->isValid()) return fail<R>(DiagnosticCode::StaleHandle, "Coupling world is dead");
    R out;
    out.active = enabled;
    std::array<b3BodyId, 4> raw{};
    const int               count = gear ? 4 : 2;
    for (int i = 0; i < count; ++i) {
        auto* body = world->findBody(bodies[i]);
        if (!body || !body->isValid()) return fail<R>(DiagnosticCode::StaleHandle, "Coupling body is dead");
        raw[i]     = eve::physics::box3d::Access::bodyId(*body);
        out.active = out.active && b3Body_IsEnabled(raw[i]);
    }
    if (gear) {
        for (int i = 0; i < 2; ++i) {
            auto* joint = world->findJoint(sources[i]);
            if (!joint || !joint->isValid()) return fail<R>(DiagnosticCode::StaleHandle, "Gear source is dead");
            const auto   a = raw[2 * i], b = raw[2 * i + 1];
            const auto   frame     = b3Joint_GetLocalFrameA(eve::physics::box3d::Access::jointId(*joint));
            const auto   transform = b3Body_GetTransform(a);
            const b3Vec3 axis =
                b3RotateVector(transform.q, b3RotateVector(frame.q, revolute[i] ? b3Vec3_axisZ : b3Vec3_axisX));
            const float scale = i == 0 ? 1.f : gearSettings.ratio;
            if (revolute[i]) {
                const float angle = b3RevoluteJoint_GetAngle(eve::physics::box3d::Access::jointId(*joint));
                out.wrapped[i]    = angle;
                out.coordinate[i] =
                    coordinate[i] + std::remainder(static_cast<double>(angle) - previous[i], 2 * std::numbers::pi);
                out.add(bodies[2 * i], {}, b3Neg(axis), scale);
                out.add(bodies[2 * i + 1], {}, axis, scale);
            } else {
                const auto anchorA = b3Body_GetWorldPoint(a, frame.p);
                const auto anchorB =
                    b3Body_GetWorldPoint(b, b3Joint_GetLocalFrameB(eve::physics::box3d::Access::jointId(*joint)).p);
                const auto d      = b3SubPos(anchorB, anchorA);
                const auto rA     = b3SubPos(anchorA, b3Body_GetWorldCenterOfMass(a));
                const auto rB     = b3SubPos(anchorB, b3Body_GetWorldCenterOfMass(b));
                out.coordinate[i] = b3Dot(axis, d);
                out.add(bodies[2 * i], b3Neg(axis), b3Neg(b3Cross(b3Add(rA, d), axis)), scale);
                out.add(bodies[2 * i + 1], axis, b3Cross(rB, axis), scale);
            }
        }
        out.error = out.coordinate[0] + gearSettings.ratio * out.coordinate[1] - reference;
    } else {
        const std::array<CouplingPoint3D, 2> local{pulleySettings.localAnchorA, pulleySettings.localAnchorB};
        const std::array<CouplingPoint3D, 2> guide{pulleySettings.groundA, pulleySettings.groundB};
        for (int i = 0; i < 2; ++i) {
            const auto  anchor = b3Body_GetWorldPoint(raw[i], vec(local[i]));
            const auto  d      = b3SubPos(anchor, pos(guide[i]));
            const float length = b3Length(d);
            out.coordinate[i]  = length;
            if (!std::isfinite(length) || (out.active && length <= 1e-6f))
                return fail<R>(DiagnosticCode::InvalidArgument, "Pulley segment is singular or non-finite");
            const auto direction = length > 1e-6f ? b3MulSV(1.f / length, d) : b3Vec3{};
            const auto offset    = b3SubPos(anchor, b3Body_GetWorldCenterOfMass(raw[i]));
            out.add(bodies[i], direction, b3Cross(offset, direction), i == 0 ? 1.f : pulleySettings.ratio);
        }
        out.error = out.coordinate[0] + pulleySettings.ratio * out.coordinate[1] - pulleySettings.length;
    }
    if (!std::isfinite(out.error)) return fail<R>(DiagnosticCode::InvalidArgument, "Coupling coordinate overflow");
    for (int i = 0; i < out.count; ++i)
        if (!b3IsValidVec3(out.terms[i].linear) || !b3IsValidVec3(out.terms[i].angular))
            return fail<R>(DiagnosticCode::InvalidArgument, "Coupling Jacobian overflow");
    return Result<R>::success(out);
}
Coupling3D::Coupling3D(std::unique_ptr<Data> data) : data_(std::move(data)) {}
Coupling3D::~Coupling3D() {
    EV_ASSERT((!data_->world || !data_->world->couplingStepActive_),
              "Release coupling wrappers outside simulation callbacks");
    release();
}
void Coupling3D::release() noexcept {
    if (data_->world) data_->world->couplings3D_.erase(data_->id);
    data_->world = nullptr;
    lifetime_.reset();
}
bool Coupling3D::isValid() const noexcept {
    if (!data_->world || !data_->world->isValid()) return false;
    for (int i = 0; i < (data_->gear ? 4 : 2); ++i) {
        auto* body = data_->world->findBody(data_->bodies[i]);
        if (!body || !body->isValid()) return false;
    }
    if (data_->gear)
        for (auto handle : data_->sources) {
            auto* joint = data_->world->findJoint(handle);
            if (!joint || !joint->isValid()) return false;
        }
    return true;
}
int          Coupling3D::getId() const noexcept { return data_->id; }
Result<void> Coupling3D::destroy() {
    if (data_->world && data_->world->couplingStepActive_)
        return fail<void>(DiagnosticCode::PreconditionViolation, "World is stepping");
    release();
    return Result<void>::success();
}
Result<void> Coupling3D::setEnabled(bool enabled) {
    if (!isValid()) return fail<void>(DiagnosticCode::StaleHandle, "Coupling is dead");
    if (data_->world->couplingStepActive_)
        return fail<void>(DiagnosticCode::PreconditionViolation, "World is stepping");
    data_->enabled = enabled;
    return Result<void>::success();
}
Result<Coupling3DState> Coupling3D::sample() const {
    auto r = data_->row();
    if (!r) return Result<Coupling3DState>::failure(r.status());
    return Result<Coupling3DState>::success({r.value().coordinate[0], r.value().coordinate[1], r.value().error,
                                             data_->lastImpulse, r.value().active, data_->saturated});
}
Result<std::unique_ptr<Coupling3D>> Coupling3D::createGear(World3D& world, Joint3D& first, Joint3D& second,
                                                           const GearCoupling3DSettings& settings) {
    using R = std::unique_ptr<Coupling3D>;
    if (world.rigidBackendKind() != RigidBackend3D::Box3D)
        return fail<R>(DiagnosticCode::Unsupported, "Transmission projection currently requires the Box3D backend");
    if (!world.isValid() || !first.isValid() || !second.isValid() || &first == &second || first.getWorld() != &world ||
        second.getWorld() != &world || !valid(settings.limits) || !std::isfinite(settings.ratio) || settings.ratio == 0)
        return fail<R>(DiagnosticCode::InvalidArgument, "Live same-world sources and finite gear settings required");
    if (world.couplingStepActive_) return fail<R>(DiagnosticCode::PreconditionViolation, "World is stepping");
    if (world.nextCoupling3DId_ == std::numeric_limits<int>::max())
        return fail<R>(DiagnosticCode::Failed, "Coupling id space exhausted");
    auto d          = std::make_unique<Data>();
    d->world        = &world;
    d->gear         = true;
    d->gearSettings = settings;
    const std::array<Joint3D*, 2> sources{&first, &second};
    for (int i = 0; i < 2; ++i) {
        const auto type = b3Joint_GetType(eve::physics::box3d::Access::jointId(*sources[i]));
        if (type != b3_revoluteJoint && type != b3_prismaticJoint)
            return fail<R>(DiagnosticCode::Unsupported, "Gear requires revolute/prismatic sources");
        d->revolute[i] = type == b3_revoluteJoint;
        d->sources[i]  = sources[i]->runtimeHandle();
        auto* a        = world.findBodyById(sources[i]->getBodyAId());
        auto* b        = world.findBodyById(sources[i]->getBodyBId());
        if (!a || !b) return fail<R>(DiagnosticCode::StaleHandle, "Gear body is missing");
        d->bodies[2 * i]     = a->runtimeHandle();
        d->bodies[2 * i + 1] = b->runtimeHandle();
    }
    auto row = d->row();
    if (!row) return Result<R>::failure(row.status());
    auto movable = coupling_detail::validateResponse(world, row.value());
    if (!movable) return Result<R>::failure(movable.status());
    d->reference  = row.value().coordinate[0] + settings.ratio * row.value().coordinate[1];
    d->previous   = row.value().wrapped;
    d->coordinate = row.value().coordinate;
    d->id         = world.nextCoupling3DId_++;
    R owned(new Coupling3D(std::move(d)));
    world.couplings3D_.emplace(owned->getId(), owned.get());
    return Result<R>::success(std::move(owned));
}
Result<std::unique_ptr<Coupling3D>> Coupling3D::createPulley(World3D& world, Body3D& a, Body3D& b,
                                                             const PulleyCoupling3DSettings& settings) {
    using R = std::unique_ptr<Coupling3D>;
    if (world.rigidBackendKind() != RigidBackend3D::Box3D)
        return fail<R>(DiagnosticCode::Unsupported, "Transmission projection currently requires the Box3D backend");
    if (!world.isValid() || !a.isValid() || !b.isValid() || &a == &b || a.getWorld() != &world ||
        b.getWorld() != &world || !valid(settings.limits) || !finite(settings.localAnchorA) ||
        !finite(settings.localAnchorB) || !finite(settings.groundA) || !finite(settings.groundB) ||
        !std::isfinite(settings.ratio) || settings.ratio <= 0 || !std::isfinite(settings.length) ||
        settings.length <= 0)
        return fail<R>(DiagnosticCode::InvalidArgument,
                       "Live distinct same-world bodies and finite pulley settings required");
    if (world.couplingStepActive_) return fail<R>(DiagnosticCode::PreconditionViolation, "World is stepping");
    if (world.nextCoupling3DId_ == std::numeric_limits<int>::max())
        return fail<R>(DiagnosticCode::Failed, "Coupling id space exhausted");
    auto d            = std::make_unique<Data>();
    d->world          = &world;
    d->pulleySettings = settings;
    d->bodies[0]      = a.runtimeHandle();
    d->bodies[1]      = b.runtimeHandle();
    auto row          = d->row();
    if (!row) return Result<R>::failure(row.status());
    auto movable = coupling_detail::validateResponse(world, row.value());
    if (!movable) return Result<R>::failure(movable.status());
    d->id = world.nextCoupling3DId_++;
    R owned(new Coupling3D(std::move(d)));
    world.couplings3D_.emplace(owned->getId(), owned.get());
    return Result<R>::success(std::move(owned));
}
void World3D::invalidateCouplings3D() noexcept {
    while (!couplings3D_.empty()) couplings3D_.begin()->second->release();
}
void World3D::releaseCouplingsForBody(PhysicsBodyHandle body) noexcept {
    for (auto it = couplings3D_.begin(); it != couplings3D_.end();) {
        auto*       coupling = (it++)->second;
        const auto& d        = *coupling->data_;
        if (std::find(d.bodies.begin(), d.bodies.end(), body) != d.bodies.end()) coupling->release();
    }
}
void World3D::releaseCouplingsForJoint(PhysicsJointHandle joint) noexcept {
    for (auto it = couplings3D_.begin(); it != couplings3D_.end();) {
        auto*       coupling = (it++)->second;
        const auto& d        = *coupling->data_;
        if (d.gear && std::find(d.sources.begin(), d.sources.end(), joint) != d.sources.end()) coupling->release();
    }
}
void World3D::adoptCouplings3D(World3D& prepared) noexcept {
    std::swap(couplings3D_, prepared.couplings3D_);
    std::swap(restoredCouplings3D_, prepared.restoredCouplings3D_);
    std::swap(nextCoupling3DId_, prepared.nextCoupling3DId_);
    for (auto [id, coupling] : couplings3D_) coupling->data_->world = this;
    for (auto [id, coupling] : prepared.couplings3D_) coupling->data_->world = &prepared;
}
ResultRef<Coupling3D> World3D::resolveCoupling3D(int id) const {
    const auto found = couplings3D_.find(id);
    if (found == couplings3D_.end() || !found->second->isValid())
        return ResultRef<Coupling3D>::failure(Diagnostic::error(
            DiagnosticCode::StaleHandle, "Transmission is absent or destroyed", "physics.world3d.coupling"));
    return ResultRef<Coupling3D>::success(std::ref(*found->second));
}
}  // namespace eve::physics
