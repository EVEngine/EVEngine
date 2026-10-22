#include "physics/mechanics/Joint2D.h"
#include "common/Assert.h"
#include "physics/Body.h"
#include "physics/PhysicsLink.h"
#include "physics/World.h"

#include <Box2D/Box2D.h>
#include <cmath>
#include <limits>
#include <vector>

namespace eve::physics {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "physics.joint2d"));
}
bool         finite(JointPoint2D p) { return std::isfinite(p.x) && std::isfinite(p.y); }
b2Vec2       vec(JointPoint2D p) { return {p.x, p.y}; }
JointPoint2D point(b2Vec2 p) { return {p.x, p.y}; }
bool         valid(const Joint2DSettings& s) {
    return finite(s.anchorA) && finite(s.anchorB) && finite(s.axis) && finite(s.groundA) && finite(s.groundB) &&
           std::isfinite(s.length) && s.length > 0 && std::isfinite(s.ratio) && s.ratio > 0 &&
           std::isfinite(s.springHertz) && s.springHertz >= 0 && std::isfinite(s.dampingRatio) && s.dampingRatio >= 0;
}
}  // namespace
Joint2D::Joint2D(World& world, b2Joint& joint, Joint2DKind kind, PhysicsJointHandle handle)
    : id_(world.nextJointId_++), world_(&world), joint_(&joint), kind_(kind), handle_(handle) {
    joint_->SetUserData(this);
    world.joints_.insert(this);
    world.jointHandles_[handle] = this;
}
Joint2D::~Joint2D() { release(); }
bool Joint2D::isValid() const noexcept { return world_ != nullptr && joint_ != nullptr; }
void Joint2D::invalidate() noexcept {
    if (world_) world_->forgetJoint(this);
    world_  = nullptr;
    joint_  = nullptr;
    handle_ = PhysicsJointHandle::invalid();
}
void Joint2D::release() {
    if (!isValid()) return;
    // Public lifetime contract forbids wrapper destruction from solver callbacks.
    EV_ASSERT(!world_->raw()->IsLocked(), "Destroy Joint2D outside solver callbacks");
    const auto            handle = handle_;
    std::vector<Joint2D*> dependents;
    for (auto* candidate : world_->joints_)
        if (candidate != this && (candidate->firstSource_ == handle || candidate->secondSource_ == handle))
            dependents.push_back(candidate);
    for (auto* dependent : dependents) dependent->release();
    world_->joints_.erase(this);
    world_->raw()->DestroyJoint(joint_);
    invalidate();
}
Result<void> Joint2D::destroyChecked() {
    if (isValid() && world_->raw()->IsLocked())
        return failure<void>(DiagnosticCode::PreconditionViolation, "World is stepping");
    release();
    return Result<void>::success();
}
Result<std::unique_ptr<Joint2D>> Joint2D::create(World& w, Body& a, Body& b, const Joint2DSettings& s) {
    using R = std::unique_ptr<Joint2D>;
    if (!w.isValid() || !a.isValid() || !b.isValid() || a.getWorld() != &w || b.getWorld() != &w || &a == &b ||
        !valid(s))
        return failure<R>(DiagnosticCode::InvalidArgument,
                          "Live distinct same-world bodies and finite valid settings required");
    if (w.raw()->IsLocked()) return failure<R>(DiagnosticCode::PreconditionViolation, "World is stepping");
    if (a.raw()->GetType() != b2_dynamicBody && b.raw()->GetType() != b2_dynamicBody)
        return failure<R>(DiagnosticCode::InvalidArgument, "At least one endpoint must be dynamic");
    if (w.nextJointHandleIndex_ == PhysicsJointHandle::invalidIndex ||
        w.nextJointId_ == std::numeric_limits<int>::max())
        return failure<R>(DiagnosticCode::Failed, "Joint handle space exhausted");
    b2Vec2      axis       = vec(s.axis);
    const float axisLength = axis.Length();
    if ((s.kind == Joint2DKind::Prismatic || s.kind == Joint2DKind::Wheel) &&
        (!std::isfinite(axisLength) || axisLength < 1e-6f))
        return failure<R>(DiagnosticCode::InvalidArgument, "Joint axis must be nonzero");
    if (axisLength > 0) axis *= 1 / axisLength;
    b2Joint* raw = nullptr;
    switch (s.kind) {
        case Joint2DKind::Revolute: {
            b2RevoluteJointDef d;
            d.Initialize(a.raw(), b.raw(), vec(s.anchorA));
            d.collideConnected = s.collideConnected;
            raw                = w.raw()->CreateJoint(&d);
            break;
        }
        case Joint2DKind::Prismatic: {
            b2PrismaticJointDef d;
            d.Initialize(a.raw(), b.raw(), vec(s.anchorA), axis);
            d.collideConnected = s.collideConnected;
            raw                = w.raw()->CreateJoint(&d);
            break;
        }
        case Joint2DKind::Distance: {
            b2DistanceJointDef d;
            d.Initialize(a.raw(), b.raw(), vec(s.anchorA), vec(s.anchorB));
            d.length           = s.length;
            d.frequencyHz      = s.springHertz;
            d.dampingRatio     = s.dampingRatio;
            d.collideConnected = s.collideConnected;
            raw                = w.raw()->CreateJoint(&d);
            break;
        }
        case Joint2DKind::Pulley: {
            const float la = (vec(s.anchorA) - vec(s.groundA)).Length();
            const float lb = (vec(s.anchorB) - vec(s.groundB)).Length();
            if (s.ratio <= b2_epsilon || !std::isfinite(la) || !std::isfinite(lb) || la <= b2_linearSlop ||
                lb <= b2_linearSlop || !std::isfinite(la + s.ratio * lb))
                return failure<R>(DiagnosticCode::InvalidArgument, "Pulley segments must have positive finite lengths");
            b2PulleyJointDef d;
            d.Initialize(a.raw(), b.raw(), vec(s.groundA), vec(s.groundB), vec(s.anchorA), vec(s.anchorB), s.ratio);
            d.collideConnected = s.collideConnected;
            raw                = w.raw()->CreateJoint(&d);
            break;
        }
        case Joint2DKind::Wheel: {
            b2WheelJointDef d;
            d.Initialize(a.raw(), b.raw(), vec(s.anchorA), axis);
            d.frequencyHz      = s.springHertz;
            d.dampingRatio     = s.dampingRatio;
            d.collideConnected = s.collideConnected;
            raw                = w.raw()->CreateJoint(&d);
            break;
        }
        case Joint2DKind::Weld: {
            b2WeldJointDef d;
            d.Initialize(a.raw(), b.raw(), vec(s.anchorA));
            d.frequencyHz      = s.springHertz;
            d.dampingRatio     = s.dampingRatio;
            d.collideConnected = s.collideConnected;
            raw                = w.raw()->CreateJoint(&d);
            break;
        }
        case Joint2DKind::Motor: {
            b2MotorJointDef d;
            d.Initialize(a.raw(), b.raw());
            d.collideConnected = s.collideConnected;
            raw                = w.raw()->CreateJoint(&d);
            break;
        }
        default: return failure<R>(DiagnosticCode::Unsupported, "Use createGear for coupled constraints");
    }
    const PhysicsJointHandle handle(w.nextJointHandleIndex_++, 1u);
    R                        owned(new Joint2D(w, *raw, s.kind, handle));
    if (s.kind == Joint2DKind::Pulley) {
        owned->pulleyLocalA_ = point(a.raw()->GetLocalPoint(vec(s.anchorA)));
        owned->pulleyLocalB_ = point(b.raw()->GetLocalPoint(vec(s.anchorB)));
    }
    w.joints_.insert(owned.get());
    return Result<R>::success(std::move(owned));
}
Result<std::unique_ptr<Joint2D>> Joint2D::createGear(World& w, Joint2D& a, Joint2D& b, float ratio) {
    using R        = std::unique_ptr<Joint2D>;
    auto supported = [](Joint2DKind k) { return k == Joint2DKind::Revolute || k == Joint2DKind::Prismatic; };
    if (!w.isValid() || !a.isValid() || !b.isValid() || a.world_ != &w || b.world_ != &w || &a == &b ||
        !supported(a.kind_) || !supported(b.kind_) || !std::isfinite(ratio) || ratio == 0)
        return failure<R>(DiagnosticCode::InvalidArgument,
                          "Two distinct same-world revolute/prismatic joints and nonzero ratio required");
    if (w.raw()->IsLocked()) return failure<R>(DiagnosticCode::PreconditionViolation, "World is stepping");
    if (w.nextJointHandleIndex_ == PhysicsJointHandle::invalidIndex ||
        w.nextJointId_ == std::numeric_limits<int>::max())
        return failure<R>(DiagnosticCode::Failed, "Joint handle space exhausted");
    if (a.joint_->GetBodyB() == b.joint_->GetBodyB() || a.joint_->GetBodyB()->GetType() != b2_dynamicBody ||
        b.joint_->GetBodyB()->GetType() != b2_dynamicBody)
        return failure<R>(DiagnosticCode::InvalidArgument, "Gear body-B endpoints must be distinct dynamic bodies");
    auto coordinate = [](const Joint2D& source) {
        return source.kind_ == Joint2DKind::Revolute
                   ? static_cast<const b2RevoluteJoint*>(source.joint_)->GetJointAngle()
                   : static_cast<const b2PrismaticJoint*>(source.joint_)->GetJointTranslation();
    };
    if (!std::isfinite(coordinate(a) + ratio * coordinate(b)))
        return failure<R>(DiagnosticCode::InvalidArgument, "Gear reference constant must be finite");
    b2GearJointDef d;
    d.bodyA                      = a.joint_->GetBodyB();
    d.bodyB                      = b.joint_->GetBodyB();
    d.joint1                     = a.joint_;
    d.joint2                     = b.joint_;
    d.ratio                      = ratio;
    auto*                    raw = w.raw()->CreateJoint(&d);
    const PhysicsJointHandle handle(w.nextJointHandleIndex_++, 1u);
    R                        owned(new Joint2D(w, *raw, Joint2DKind::Gear, handle));
    owned->firstSource_  = a.handle_;
    owned->secondSource_ = b.handle_;
    const std::array<b2Body*, 4> endpoints{a.joint_->GetBodyA(), a.joint_->GetBodyB(), b.joint_->GetBodyA(),
                                           b.joint_->GetBodyB()};
    for (std::size_t i = 0; i < endpoints.size(); ++i) {
        owned->gearReferencePoses_[3 * i]     = endpoints[i]->GetPosition().x;
        owned->gearReferencePoses_[3 * i + 1] = endpoints[i]->GetPosition().y;
        owned->gearReferencePoses_[3 * i + 2] = endpoints[i]->GetAngle();
    }
    w.joints_.insert(owned.get());
    return Result<R>::success(std::move(owned));
}
Result<void> Joint2D::setMotor(bool enabled, float speed, float maximumForce) {
    if (!isValid()) return failure<void>(DiagnosticCode::StaleHandle, "Joint is dead");
    if (world_->raw()->IsLocked()) return failure<void>(DiagnosticCode::PreconditionViolation, "World is stepping");
    if (!std::isfinite(speed) || !std::isfinite(maximumForce) || maximumForce < 0)
        return failure<void>(DiagnosticCode::InvalidArgument, "Finite speed and nonnegative maximum force required");
    switch (kind_) {
        case Joint2DKind::Revolute: {
            auto& j = *static_cast<b2RevoluteJoint*>(joint_);
            j.SetMotorSpeed(speed);
            j.SetMaxMotorTorque(maximumForce);
            j.EnableMotor(enabled);
            break;
        }
        case Joint2DKind::Prismatic: {
            auto& j = *static_cast<b2PrismaticJoint*>(joint_);
            j.SetMotorSpeed(speed);
            j.SetMaxMotorForce(maximumForce);
            j.EnableMotor(enabled);
            break;
        }
        case Joint2DKind::Wheel: {
            auto& j = *static_cast<b2WheelJoint*>(joint_);
            j.SetMotorSpeed(speed);
            j.SetMaxMotorTorque(maximumForce);
            j.EnableMotor(enabled);
            break;
        }
        default: return failure<void>(DiagnosticCode::Unsupported, "Joint kind has no motor");
    }
    return Result<void>::success();
}
Result<void> Joint2D::setLimits(bool enabled, float lower, float upper) {
    if (!isValid()) return failure<void>(DiagnosticCode::StaleHandle, "Joint is dead");
    if (world_->raw()->IsLocked()) return failure<void>(DiagnosticCode::PreconditionViolation, "World is stepping");
    if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper)
        return failure<void>(DiagnosticCode::InvalidArgument, "Finite ordered limits required");
    switch (kind_) {
        case Joint2DKind::Revolute: {
            auto& j = *static_cast<b2RevoluteJoint*>(joint_);
            j.SetLimits(lower, upper);
            j.EnableLimit(enabled);
            break;
        }
        case Joint2DKind::Prismatic: {
            auto& j = *static_cast<b2PrismaticJoint*>(joint_);
            j.SetLimits(lower, upper);
            j.EnableLimit(enabled);
            break;
        }
        default: return failure<void>(DiagnosticCode::Unsupported, "Joint kind has no limits");
    }
    return Result<void>::success();
}
Result<Joint2DState> Joint2D::sample(float dt) const {
    if (!isValid()) return failure<Joint2DState>(DiagnosticCode::StaleHandle, "Joint is dead");
    if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(1 / dt))
        return failure<Joint2DState>(DiagnosticCode::InvalidArgument, "Positive finite sampling duration required");
    Joint2DState out;
    out.kind           = kind_;
    out.anchorA        = point(joint_->GetAnchorA());
    out.anchorB        = point(joint_->GetAnchorB());
    out.reactionForce  = point(joint_->GetReactionForce(1 / dt));
    out.reactionTorque = joint_->GetReactionTorque(1 / dt);
    switch (kind_) {
        case Joint2DKind::Revolute: out.coordinate = static_cast<b2RevoluteJoint*>(joint_)->GetJointAngle(); break;
        case Joint2DKind::Prismatic:
            out.coordinate = static_cast<b2PrismaticJoint*>(joint_)->GetJointTranslation();
            break;
        case Joint2DKind::Distance: out.coordinate = (joint_->GetAnchorB() - joint_->GetAnchorA()).Length(); break;
        case Joint2DKind::Pulley: {
            auto& j                 = *static_cast<b2PulleyJoint*>(joint_);
            out.coordinate          = j.GetCurrentLengthA();
            out.secondaryCoordinate = j.GetCurrentLengthB();
            break;
        }
        case Joint2DKind::Gear: out.coordinate = static_cast<b2GearJoint*>(joint_)->GetRatio(); break;
        case Joint2DKind::Wheel: out.coordinate = static_cast<b2WheelJoint*>(joint_)->GetJointTranslation(); break;
        case Joint2DKind::Weld:
        case Joint2DKind::Motor: break;
    }
    return Result<Joint2DState>::success(out);
}

void World::releaseBodyJoints2D(Body& body) {
    std::vector<Joint2D*> attached;
    for (auto* joint : joints_)
        if (joint->joint_->GetBodyA() == body.raw() || joint->joint_->GetBodyB() == body.raw())
            attached.push_back(joint);
    for (auto* joint : attached) joint->release();
}
void World::invalidateJoints2D() noexcept {
    const auto retiring = joints_;
    for (auto* joint : retiring) joint->invalidate();
    joints_.clear();
    restoredJoints2D_.clear();
}
ResultRef<Joint2D> World::resolveJoint2D(int id) const {
    for (auto* joint : joints_)
        if (joint->getId() == id) return ResultRef<Joint2D>::success(std::ref(*joint));
    return ResultRef<Joint2D>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Planar joint id is absent"));
}
Result<PhysicsJointLink> PhysicsJointLink::fromJoint(const Joint2D& joint) {
    if (!joint.isValid()) return failure<PhysicsJointLink>(DiagnosticCode::StaleHandle, "Joint is dead");
    return Result<PhysicsJointLink>::success({joint.world_->runtimeHandle(), joint.handle_});
}
ResultRef<Joint2D> PhysicsJointLink::resolve(World& owner) const {
    if (owner.isValid() && owner.runtimeHandle() == world && joint.isValid()) {
        for (auto* candidate : owner.joints_)
            if (candidate->runtimeHandle() == joint) return ResultRef<Joint2D>::success(std::ref(*candidate));
    }
    return ResultRef<Joint2D>::failure(
        Diagnostic::error(DiagnosticCode::StaleHandle, "Planar joint link is stale or has a foreign world"));
}

}  // namespace eve::physics
