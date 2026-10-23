// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <algorithm>
#include "physics/jolt/JoltJoint.h"
namespace eve::physics::jolt {
JPH::uint64 pairKey(JPH::BodyID a, JPH::BodyID b) {
    auto x = a.GetIndexAndSequenceNumber(), y = b.GetIndexAndSequenceNumber();
    return (JPH::uint64(std::min(x, y)) << 32) | std::max(x, y);
}
JointBackend::~JointBackend() { destroy(); }
void JointBackend::bindOwner(Joint3D& owner) noexcept { joint->owner = &owner; }
bool JointBackend::isValid() const noexcept {
    auto world = joint->world.lock();
    auto a     = joint->a.lock();
    auto b     = joint->b.lock();
    return joint->alive && world && world->alive && a && b && a->valid() && b->valid();
}
void JointBackend::invalidate() noexcept { destroy(); }
void JointBackend::destroy() noexcept {
    if (joint->alive) {
        auto world = joint->world.lock();
        auto a     = joint->a.lock();
        auto b     = joint->b.lock();
        if (world && world->alive) {
            if (joint->native) world->system.RemoveConstraint(joint->native);
            if (joint->velocity) world->system.RemoveConstraint(joint->velocity);
            if (!joint->collide && a && b) {
                auto i = world->suppressedPairs.find(pairKey(a->id, b->id));
                if (i != world->suppressedPairs.end() && --i->second == 0) world->suppressedPairs.erase(i);
                if (a->valid()) a->api().InvalidateContactCache(a->id);
                if (b->valid()) b->api().InvalidateContactCache(b->id);
            }
        }
    }
    joint->alive    = false;
    joint->owner    = nullptr;
    joint->native   = nullptr;
    joint->velocity = nullptr;
}
eve::Result<void> JointBackend::wakeBodies() const {
    auto a = joint->a.lock(), b = joint->b.lock();
    if (a && a->active) a->api().ActivateBody(a->id);
    if (b && b->active) b->api().ActivateBody(b->id);
    return success();
}
eve::Result<void> JointBackend::requireKind(JointKind3D kind) const {
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    if (joint->kind != kind) return rejected<void>("Wrong joint kind", eve::DiagnosticCode::InvalidArgument);
    return success();
}
JPH::SixDOFConstraint& JointBackend::six() const { return static_cast<JPH::SixDOFConstraint&>(*joint->native); }
JPH::SixDOFConstraint& JointBackend::drive() const { return *joint->velocity; }
JPH::RMat44            JointBackend::frame(bool second) const {
    auto body = second ? joint->b.lock() : joint->a.lock();
    return body->api().GetCenterOfMassTransform(body->id) *
           (second ? joint->native->GetConstraintToBody2Matrix() : joint->native->GetConstraintToBody1Matrix());
}
eve::Result<JointFrames3D> JointBackend::localFrames() const {
    if (!isValid()) return rejected<JointFrames3D>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<JointFrames3D>::success(joint->frames);
}
eve::Result<void> JointBackend::setCollideConnected(bool collide) {
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    if (collide == joint->collide) return success();
    auto world = joint->world.lock();
    auto a     = joint->a.lock();
    auto b     = joint->b.lock();
    auto key   = pairKey(a->id, b->id);
    if (!collide)
        ++world->suppressedPairs[key];
    else {
        auto i = world->suppressedPairs.find(key);
        if (i != world->suppressedPairs.end() && --i->second == 0) world->suppressedPairs.erase(i);
    }
    joint->collide = collide;
    a->api().InvalidateContactCache(a->id);
    b->api().InvalidateContactCache(b->id);
    if (a->active) a->api().ActivateBody(a->id);
    if (b->active) b->api().ActivateBody(b->id);
    return success();
}
eve::Result<bool> JointBackend::getCollideConnected() const {
    if (!isValid()) return rejected<bool>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<bool>::success(joint->collide);
}
JPH::Vec3 JointBackend::force(bool torque) const {
    if (!joint->native) return JPH::Vec3::sZero();
    const float inverse = joint->world.lock()->inverseStep;
    if (joint->kind == JointKind3D::Distance) {
        if (torque) return JPH::Vec3::sZero();
        const auto axis =
            JPH::Vec3(frame(true).GetTranslation() - frame(false).GetTranslation()).NormalizedOr(JPH::Vec3::sAxisX());
        return axis * distanceImpulse(*joint->native) * inverse;
    }
    auto value = torque ? six().GetTotalLambdaRotation() + six().GetTotalLambdaMotorRotation()
                        : six().GetTotalLambdaPosition() + six().GetTotalLambdaMotorTranslation();
    // Fully locked SixDOF point/rotation parts report world-space impulses; individual axes report constraint space.
    const bool locked = torque ? six().IsFixedAxis(JPH::SixDOFConstraint::EAxis::RotationX) &&
                                     six().IsFixedAxis(JPH::SixDOFConstraint::EAxis::RotationY) &&
                                     six().IsFixedAxis(JPH::SixDOFConstraint::EAxis::RotationZ)
                               : six().IsFixedAxis(JPH::SixDOFConstraint::EAxis::TranslationX) &&
                                     six().IsFixedAxis(JPH::SixDOFConstraint::EAxis::TranslationY) &&
                                     six().IsFixedAxis(JPH::SixDOFConstraint::EAxis::TranslationZ);
    auto       world  = locked ? value : frame(false).Multiply3x3(value);
    if (joint->velocity)
        world += frame(false).Multiply3x3(torque ? drive().GetTotalLambdaMotorRotation()
                                                 : drive().GetTotalLambdaMotorTranslation());
    return world * inverse;
}
void notifyJointShapeChanged(State& state, JPH::BodyID id, JPH::Vec3Arg delta) {
    for (auto& weak : state.joints)
        if (auto joint = weak.lock(); joint && joint->alive && joint->native) {
            auto a = joint->a.lock(), b = joint->b.lock();
            if ((a && a->id == id) || (b && b->id == id)) {
                joint->native->NotifyShapeChanged(id, delta);
                if (joint->velocity) joint->velocity->NotifyShapeChanged(id, delta);
            }
        }
}
}  // namespace eve::physics::jolt
