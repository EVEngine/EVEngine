// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <algorithm>
#include "physics/jolt/JoltJoint.h"
namespace eve::physics::jolt {
using A = JPH::SixDOFConstraint::EAxis;
eve::Result<void> JointBackend::setForceThreshold(float threshold) {
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(threshold) || threshold < 0)
        return rejected<void>("Invalid threshold", eve::DiagnosticCode::InvalidArgument);
    joint->forceThreshold = threshold;
    return wakeBodies();
}
eve::Result<float> JointBackend::getForceThreshold() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(joint->forceThreshold);
}
eve::Result<void> JointBackend::setTorqueThreshold(float threshold) {
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(threshold) || threshold < 0)
        return rejected<void>("Invalid threshold", eve::DiagnosticCode::InvalidArgument);
    joint->torqueThreshold = threshold;
    return wakeBodies();
}
eve::Result<float> JointBackend::getTorqueThreshold() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(joint->torqueThreshold);
}
eve::Result<float> JointBackend::getConstraintForceX() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(force(false).GetX());
}
eve::Result<float> JointBackend::getConstraintForceY() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(force(false).GetY());
}
eve::Result<float> JointBackend::getConstraintForceZ() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(force(false).GetZ());
}
eve::Result<float> JointBackend::getConstraintTorqueX() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(force(true).GetX());
}
eve::Result<float> JointBackend::getConstraintTorqueY() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(force(true).GetY());
}
eve::Result<float> JointBackend::getConstraintTorqueZ() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(force(true).GetZ());
}
eve::Result<float> JointBackend::getLinearSeparation() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(joint->native ? JPH::Vec3(frame(true).GetTranslation() - frame(false).GetTranslation()).Length()
                                    : 0.f);
}
eve::Result<float> JointBackend::getAngularSeparation() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    return R::success(joint->native && joint->kind != JointKind3D::Distance
                          ? 2.f * std::acos(std::clamp(std::abs(six().GetRotationInConstraintSpace().GetW()), 0.f, 1.f))
                          : 0.f);
}
eve::Result<void> JointBackend::setDistanceLength(float length) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Distance);
    if (!checked) return R::failure(checked.status());
    if (!std::isfinite(length) || length < 0)
        return rejected<void>("Invalid length", eve::DiagnosticCode::InvalidArgument);
    joint->length = length;
    jolt::setDistanceLength(*joint->native, length);
    return wakeBodies();
}
eve::Result<float> JointBackend::getDistanceLength() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Distance);
    if (!checked) return R::failure(checked.status());
    return R::success(joint->length);
}
eve::Result<float> JointBackend::getDistanceCurrentLength() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Distance);
    if (!checked) return R::failure(checked.status());
    return R::success(JPH::Vec3(frame(true).GetTranslation() - frame(false).GetTranslation()).Length());
}
eve::Result<void> JointBackend::setDistanceSpring(bool enabled, float hertz, float dampingRatio) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Distance);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(hertz, dampingRatio, 0) || hertz < 0 || dampingRatio < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);
    jolt::setDistanceSpring(*joint->native, enabled, hertz, dampingRatio);
    return wakeBodies();
}
eve::Result<void> JointBackend::setDistanceLimits(bool enabled, float minimum, float maximum) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Distance);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(minimum, maximum, 0) || minimum < 0 || minimum > maximum)
        return rejected<void>("Invalid limits", eve::DiagnosticCode::InvalidArgument);
    jolt::setDistanceLimits(*joint->native, enabled, minimum, maximum);
    return wakeBodies();
}
eve::Result<void> JointBackend::setDistanceMotor(bool enabled, float speed, float maxForce) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Distance);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(speed, maxForce, 0) || maxForce < 0)
        return rejected<void>("Invalid motor", eve::DiagnosticCode::InvalidArgument);
    jolt::setDistanceMotor(*joint->native, enabled, speed, maxForce);
    return wakeBodies();
}
eve::Result<void> JointBackend::setRevoluteSpring(bool enabled, float targetAngle, float hertz, float dampingRatio) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Revolute);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(targetAngle, hertz, dampingRatio) || hertz < 0 || dampingRatio < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);
    six().GetMotorSettings(A(3)).mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz, dampingRatio};
    six().SetTargetOrientationCS(JPH::Quat::sRotation(JPH::Vec3::sAxisX(), targetAngle));
    if (enabled || six().GetMotorState(A(3)) == JPH::EMotorState::Position)
        six().SetMotorState(A(3), enabled ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    return wakeBodies();
}
eve::Result<void> JointBackend::setRevoluteLimits(bool enabled, float lower, float upper) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Revolute);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(lower, upper, 0) || lower > upper || lower < -JPH::JPH_PI || upper > JPH::JPH_PI)
        return rejected<void>("Invalid limits", eve::DiagnosticCode::InvalidArgument);
    // Keep the nonzero reference angle: SixDOF interprets an exactly equal range as a zero lock.
    if (enabled && lower == upper && lower != 0) {
        if (upper < JPH::JPH_PI)
            upper = std::nextafter(upper, JPH::JPH_PI);
        else
            lower = std::nextafter(lower, -JPH::JPH_PI);
    }
    auto lo = six().GetRotationLimitsMin(), hi = six().GetRotationLimitsMax();
    lo.SetComponent(0, enabled ? lower : -FLT_MAX);
    hi.SetComponent(0, enabled ? upper : FLT_MAX);
    six().SetRotationLimits(lo, hi);
    return wakeBodies();
}
eve::Result<void> JointBackend::setRevoluteMotor(bool enabled, float speed, float maxTorque) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Revolute);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(speed, maxTorque, 0) || maxTorque < 0)
        return rejected<void>("Invalid motor", eve::DiagnosticCode::InvalidArgument);
    auto& settings = drive().GetMotorSettings(A(3));
    if (enabled || drive().GetMotorState(A(3)) == JPH::EMotorState::Velocity) settings.SetTorqueLimit(maxTorque);
    auto velocity = drive().GetTargetAngularVelocityCS();
    velocity.SetX(speed);
    drive().SetTargetAngularVelocityCS(velocity);
    if (enabled || drive().GetMotorState(A(3)) == JPH::EMotorState::Velocity)
        drive().SetMotorState(A(3), enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
    return wakeBodies();
}
eve::Result<float> JointBackend::getRevoluteAngle() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Revolute);
    if (!checked) return R::failure(checked.status());
    const auto q = six().GetRotationInConstraintSpace();
    return R::success(2.f * std::atan2(q.GetX(), q.GetW()));
}
eve::Result<float> JointBackend::getRevoluteMotorTorque() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Revolute);
    if (!checked) return R::failure(checked.status());
    return R::success(drive().GetTotalLambdaMotorRotation().GetX() * joint->world.lock()->inverseStep);
}
eve::Result<void> JointBackend::setPrismaticSpring(bool enabled, float targetTranslation, float hertz,
                                                   float dampingRatio) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Prismatic);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(targetTranslation, hertz, dampingRatio) || hertz < 0 || dampingRatio < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);
    six().GetMotorSettings(A(0)).mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz, dampingRatio};
    six().SetTargetPositionCS({targetTranslation, 0, 0});
    if (enabled || six().GetMotorState(A(0)) == JPH::EMotorState::Position)
        six().SetMotorState(A(0), enabled ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    return wakeBodies();
}
eve::Result<void> JointBackend::setPrismaticLimits(bool enabled, float lower, float upper) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Prismatic);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(lower, upper, 0) || lower > upper)
        return rejected<void>("Invalid limits", eve::DiagnosticCode::InvalidArgument);
    // One representable step keeps a nonzero target without triggering SixDOF's zero-lock shortcut.
    if (enabled && lower == upper && lower != 0) {
        if (upper < FLT_MAX)
            upper = std::nextafter(upper, FLT_MAX);
        else
            lower = std::nextafter(lower, -FLT_MAX);
    }
    auto lo = six().GetTranslationLimitsMin(), hi = six().GetTranslationLimitsMax();
    lo.SetComponent(0, enabled ? lower : -FLT_MAX);
    hi.SetComponent(0, enabled ? upper : FLT_MAX);
    six().SetTranslationLimits(lo, hi);
    return wakeBodies();
}
eve::Result<void> JointBackend::setPrismaticMotor(bool enabled, float speed, float maxForce) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Prismatic);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(speed, maxForce, 0) || maxForce < 0)
        return rejected<void>("Invalid motor", eve::DiagnosticCode::InvalidArgument);
    auto& settings = drive().GetMotorSettings(A(0));
    if (enabled || drive().GetMotorState(A(0)) == JPH::EMotorState::Velocity) settings.SetForceLimit(maxForce);
    auto velocity = drive().GetTargetVelocityCS();
    velocity.SetX(speed);
    drive().SetTargetVelocityCS(velocity);
    if (enabled || drive().GetMotorState(A(0)) == JPH::EMotorState::Velocity)
        drive().SetMotorState(A(0), enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
    return wakeBodies();
}
eve::Result<float> JointBackend::getPrismaticTranslation() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Prismatic);
    if (!checked) return R::failure(checked.status());
    return R::success(
        frame(false)
            .Multiply3x3Transposed(JPH::Vec3(frame(true).GetTranslation() - frame(false).GetTranslation()))
            .GetX());
}
eve::Result<float> JointBackend::getPrismaticSpeed() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Prismatic);
    if (!checked) return R::failure(checked.status());
    const auto a = joint->a.lock(), b = joint->b.lock();
    return R::success(frame(false)
                          .Multiply3x3Transposed(b->api().GetPointVelocity(b->id, frame(true).GetTranslation()) -
                                                 a->api().GetPointVelocity(a->id, frame(false).GetTranslation()))
                          .GetX());
}
eve::Result<float> JointBackend::getPrismaticMotorForce() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Prismatic);
    if (!checked) return R::failure(checked.status());
    return R::success(drive().GetTotalLambdaMotorTranslation().GetX() * joint->world.lock()->inverseStep);
}
eve::Result<void> JointBackend::setSphericalConeLimit(bool enabled, float angle) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Spherical);
    if (!checked) return R::failure(checked.status());
    if (!std::isfinite(angle) || angle < 0 || angle > JPH::JPH_PI)
        return rejected<void>("Invalid cone", eve::DiagnosticCode::InvalidArgument);
    auto        lo = six().GetRotationLimitsMin(), hi = six().GetRotationLimitsMax();
    const float limit = enabled ? angle : JPH::JPH_PI;
    lo.SetY(-limit);
    lo.SetZ(-limit);
    hi.SetY(limit);
    hi.SetZ(limit);
    six().SetRotationLimits(lo, hi);
    return wakeBodies();
}
eve::Result<void> JointBackend::setSphericalTwistLimits(bool enabled, float lower, float upper) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Spherical);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(lower, upper, 0) || lower > upper || lower < -JPH::JPH_PI || upper > JPH::JPH_PI)
        return rejected<void>("Invalid limits", eve::DiagnosticCode::InvalidArgument);
    // Keep the nonzero reference angle: SixDOF interprets an exactly equal range as a zero lock.
    if (enabled && lower == upper && lower != 0) {
        if (upper < JPH::JPH_PI)
            upper = std::nextafter(upper, JPH::JPH_PI);
        else
            lower = std::nextafter(lower, -JPH::JPH_PI);
    }
    auto lo = six().GetRotationLimitsMin(), hi = six().GetRotationLimitsMax();
    lo.SetComponent(0, enabled ? lower : -FLT_MAX);
    hi.SetComponent(0, enabled ? upper : FLT_MAX);
    six().SetRotationLimits(lo, hi);
    return wakeBodies();
}
eve::Result<void> JointBackend::setSphericalMotor(bool enabled, float velocityX, float velocityY, float velocityZ,
                                                  float maxTorque) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Spherical);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(velocityX, velocityY, velocityZ) || !std::isfinite(maxTorque) || maxTorque < 0)
        return rejected<void>("Invalid motor", eve::DiagnosticCode::InvalidArgument);
    joint->worldAngularVelocity = {velocityX, velocityY, velocityZ};
    six().SetTargetAngularVelocityCS(frame(false).Multiply3x3Transposed(joint->worldAngularVelocity));
    for (int i = 3; i < 6; ++i) {
        six().GetMotorSettings(A(i)).SetTorqueLimit(maxTorque);
        six().SetMotorState(A(i), enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
    }
    return wakeBodies();
}
eve::Result<float> JointBackend::getSphericalConeAngle() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Spherical);
    if (!checked) return R::failure(checked.status());
    return R::success(
        std::acos(std::clamp((six().GetRotationInConstraintSpace() * JPH::Vec3::sAxisX()).GetX(), -1.f, 1.f)));
}
eve::Result<float> JointBackend::getSphericalTwistAngle() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Spherical);
    if (!checked) return R::failure(checked.status());
    const auto q = six().GetRotationInConstraintSpace();
    return R::success(2.f * std::atan2(q.GetX(), q.GetW()));
}
eve::Result<void> JointBackend::setWheelSuspension(bool enabled, float hertz, float dampingRatio) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(hertz, dampingRatio, 0) || hertz < 0 || dampingRatio < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);
    six().GetMotorSettings(A::TranslationY).mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz,
                                                               dampingRatio};
    six().SetMotorState(A::TranslationY, enabled ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    return wakeBodies();
}
eve::Result<void> JointBackend::setWheelSuspensionLimits(bool enabled, float lower, float upper) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(lower, upper, 0) || lower > upper)
        return rejected<void>("Invalid limits", eve::DiagnosticCode::InvalidArgument);
    // One representable step keeps a nonzero target without triggering SixDOF's zero-lock shortcut.
    if (enabled && lower == upper && lower != 0) {
        if (upper < FLT_MAX)
            upper = std::nextafter(upper, FLT_MAX);
        else
            lower = std::nextafter(lower, -FLT_MAX);
    }
    auto lo = six().GetTranslationLimitsMin(), hi = six().GetTranslationLimitsMax();
    lo.SetComponent(1, enabled ? lower : -FLT_MAX);
    hi.SetComponent(1, enabled ? upper : FLT_MAX);
    six().SetTranslationLimits(lo, hi);
    return wakeBodies();
}
eve::Result<void> JointBackend::setWheelSpinMotor(bool enabled, float speed, float maxTorque) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(speed, maxTorque, 0) || maxTorque < 0)
        return rejected<void>("Invalid motor", eve::DiagnosticCode::InvalidArgument);
    auto& settings = six().GetMotorSettings(A(3));
    if (enabled || six().GetMotorState(A(3)) == JPH::EMotorState::Velocity) settings.SetTorqueLimit(maxTorque);
    auto velocity = six().GetTargetAngularVelocityCS();
    velocity.SetX(speed);
    six().SetTargetAngularVelocityCS(velocity);
    six().SetMotorState(A(3), enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
    return wakeBodies();
}
eve::Result<void> JointBackend::setWheelSteering(bool enabled, float targetAngle, float hertz, float dampingRatio,
                                                 float maxTorque) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(targetAngle, hertz, dampingRatio) || !std::isfinite(maxTorque) || hertz < 0 || dampingRatio < 0 ||
        maxTorque < 0)
        return rejected<void>("Invalid steering", eve::DiagnosticCode::InvalidArgument);
    auto& m           = six().GetMotorSettings(A::RotationY);
    m.mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz, dampingRatio};
    m.SetTorqueLimit(maxTorque);
    six().SetTargetOrientationCS(JPH::Quat::sRotation(JPH::Vec3::sAxisY(), targetAngle));
    six().SetMotorState(A::RotationY, enabled ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    return wakeBodies();
}
eve::Result<void> JointBackend::setWheelSteeringLimits(bool enabled, float lower, float upper) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(lower, upper, 0) || lower > upper || lower < -JPH::JPH_PI || upper > JPH::JPH_PI)
        return rejected<void>("Invalid limits", eve::DiagnosticCode::InvalidArgument);
    // Keep the nonzero reference angle: SixDOF interprets an exactly equal range as a zero lock.
    if (enabled && lower == upper && lower != 0) {
        if (upper < JPH::JPH_PI)
            upper = std::nextafter(upper, JPH::JPH_PI);
        else
            lower = std::nextafter(lower, -JPH::JPH_PI);
    }
    auto lo = six().GetRotationLimitsMin(), hi = six().GetRotationLimitsMax();
    lo.SetComponent(1, enabled ? lower : -FLT_MAX);
    hi.SetComponent(1, enabled ? upper : FLT_MAX);
    six().SetRotationLimits(lo, hi);
    return wakeBodies();
}
eve::Result<float> JointBackend::getWheelSpinSpeed() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    auto a = joint->a.lock(), b = joint->b.lock();
    return R::success(
        frame(false)
            .Multiply3x3Transposed(b->api().GetAngularVelocity(b->id) - a->api().GetAngularVelocity(a->id))
            .GetX());
}
eve::Result<float> JointBackend::getWheelSpinTorque() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetTotalLambdaMotorRotation().GetX() * joint->world.lock()->inverseStep);
}
eve::Result<float> JointBackend::getWheelSteeringAngle() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    const auto q = six().GetRotationInConstraintSpace();
    return R::success(2.f * std::atan2(q.GetY(), q.GetW()));
}
eve::Result<float> JointBackend::getWheelSteeringTorque() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Wheel);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetTotalLambdaMotorRotation().GetY() * joint->world.lock()->inverseStep);
}
eve::Result<void> JointBackend::setWeldLinearSpring(float hertz, float dampingRatio) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Weld);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(hertz, dampingRatio, 0) || hertz < 0 || dampingRatio < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);
    six().SetTranslationLimits(JPH::Vec3::sReplicate(hertz > 0 ? -FLT_MAX : FLT_MAX),
                               JPH::Vec3::sReplicate(hertz > 0 ? FLT_MAX : -FLT_MAX));
    for (int i = 0; i < 3; ++i) {
        auto& m           = six().GetMotorSettings(A(i));
        m.mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz, dampingRatio};
        m.SetForceLimit(FLT_MAX);
        six().SetMotorState(A(i), hertz > 0 ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    }
    return wakeBodies();
}
eve::Result<void> JointBackend::setWeldAngularSpring(float hertz, float dampingRatio) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Weld);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(hertz, dampingRatio, 0) || hertz < 0 || dampingRatio < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);
    six().SetRotationLimits(JPH::Vec3::sReplicate(hertz > 0 ? -FLT_MAX : FLT_MAX),
                            JPH::Vec3::sReplicate(hertz > 0 ? FLT_MAX : -FLT_MAX));
    for (int i = 3; i < 6; ++i) {
        auto& m           = six().GetMotorSettings(A(i));
        m.mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz, dampingRatio};
        m.SetTorqueLimit(FLT_MAX);
        six().SetMotorState(A(i), hertz > 0 ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    }
    return wakeBodies();
}
eve::Result<float> JointBackend::getWeldLinearHertz() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Weld);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetMotorSettings(A(0)).mSpringSettings.mFrequency);
}
eve::Result<float> JointBackend::getWeldLinearDampingRatio() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Weld);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetMotorSettings(A(0)).mSpringSettings.mDamping);
}
eve::Result<float> JointBackend::getWeldAngularHertz() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Weld);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetMotorSettings(A(3)).mSpringSettings.mFrequency);
}
eve::Result<float> JointBackend::getWeldAngularDampingRatio() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Weld);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetMotorSettings(A(3)).mSpringSettings.mDamping);
}
eve::Result<void> JointBackend::setMotorLinearVelocity(float vx, float vy, float vz) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(vx, vy, vz)) return rejected<void>("Invalid velocity", eve::DiagnosticCode::InvalidArgument);
    joint->worldLinearVelocity = {vx, vy, vz};
    for (int i = 0; i < 3; ++i) drive().GetMotorSettings(A(i)).SetForceLimit(joint->maxVelocityForce);
    drive().SetTargetVelocityCS(frame(false).Multiply3x3Transposed(joint->worldLinearVelocity));
    for (int i = 0; i < 3; ++i) drive().SetMotorState(A(i), JPH::EMotorState::Velocity);
    return wakeBodies();
}
eve::Result<void> JointBackend::setMotorAngularVelocity(float wx, float wy, float wz) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(wx, wy, wz)) return rejected<void>("Invalid velocity", eve::DiagnosticCode::InvalidArgument);
    joint->worldAngularVelocity = {wx, wy, wz};
    for (int i = 3; i < 6; ++i) drive().GetMotorSettings(A(i)).SetTorqueLimit(joint->maxVelocityTorque);
    drive().SetTargetAngularVelocityCS(frame(false).Multiply3x3Transposed(joint->worldAngularVelocity));
    for (int i = 3; i < 6; ++i) drive().SetMotorState(A(i), JPH::EMotorState::Velocity);
    return wakeBodies();
}
eve::Result<void> JointBackend::setMotorVelocityLimits(float maxForce, float maxTorque) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(maxForce, maxTorque, 0) || maxForce < 0 || maxTorque < 0)
        return rejected<void>("Invalid motor limits", eve::DiagnosticCode::InvalidArgument);
    joint->maxVelocityForce  = maxForce;
    joint->maxVelocityTorque = maxTorque;
    for (int i = 0; i < 6; ++i)
        if (drive().GetMotorState(A(i)) == JPH::EMotorState::Velocity) {
            drive().GetMotorSettings(A(i)).SetForceLimit(maxForce);
            drive().GetMotorSettings(A(i)).SetTorqueLimit(maxTorque);
        }
    return wakeBodies();
}
eve::Result<void> JointBackend::setMotorLinearSpring(float hertz, float dampingRatio, float maxForce) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(hertz, dampingRatio, maxForce) || hertz < 0 || dampingRatio < 0 || maxForce < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);

    for (int i = 0; i < 3; ++i) {
        auto& m           = six().GetMotorSettings(A(i));
        m.mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz, dampingRatio};
        if (hertz > 0 || six().GetMotorState(A(i)) == JPH::EMotorState::Position) m.SetForceLimit(maxForce);
        if (hertz > 0 || six().GetMotorState(A(i)) == JPH::EMotorState::Position)
            six().SetMotorState(A(i), hertz > 0 ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    }
    return wakeBodies();
}
eve::Result<void> JointBackend::setMotorAngularSpring(float hertz, float dampingRatio, float maxTorque) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(hertz, dampingRatio, maxTorque) || hertz < 0 || dampingRatio < 0 || maxTorque < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);

    for (int i = 3; i < 6; ++i) {
        auto& m           = six().GetMotorSettings(A(i));
        m.mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz, dampingRatio};
        if (hertz > 0 || six().GetMotorState(A(i)) == JPH::EMotorState::Position) m.SetTorqueLimit(maxTorque);
        if (hertz > 0 || six().GetMotorState(A(i)) == JPH::EMotorState::Position)
            six().SetMotorState(A(i), hertz > 0 ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    }
    return wakeBodies();
}
eve::Result<float> JointBackend::getMotorLinearVelocityX() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    return R::success(joint->worldLinearVelocity.GetX());
}
eve::Result<float> JointBackend::getMotorLinearVelocityY() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    return R::success(joint->worldLinearVelocity.GetY());
}
eve::Result<float> JointBackend::getMotorLinearVelocityZ() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    return R::success(joint->worldLinearVelocity.GetZ());
}
eve::Result<float> JointBackend::getMotorAngularVelocityX() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    return R::success(joint->worldAngularVelocity.GetX());
}
eve::Result<float> JointBackend::getMotorAngularVelocityY() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    return R::success(joint->worldAngularVelocity.GetY());
}
eve::Result<float> JointBackend::getMotorAngularVelocityZ() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Motor);
    if (!checked) return R::failure(checked.status());
    return R::success(joint->worldAngularVelocity.GetZ());
}
eve::Result<void> JointBackend::setParallelSpring(float hertz, float dampingRatio, float maxTorque) {
    using R = eve::Result<void>;
    if (!isValid()) return rejected<void>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Parallel);
    if (!checked) return R::failure(checked.status());
    if (!isFinite(hertz, dampingRatio, maxTorque) || hertz < 0 || dampingRatio < 0 || maxTorque < 0)
        return rejected<void>("Invalid spring", eve::DiagnosticCode::InvalidArgument);
    six().SetRotationLimits({-FLT_MAX, hertz > 0 ? -FLT_MAX : FLT_MAX, hertz > 0 ? -FLT_MAX : FLT_MAX},
                            {FLT_MAX, hertz > 0 ? FLT_MAX : -FLT_MAX, hertz > 0 ? FLT_MAX : -FLT_MAX});
    for (int i = 4; i < 6; ++i) {
        auto& m           = six().GetMotorSettings(A(i));
        m.mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, hertz, dampingRatio};
        m.SetTorqueLimit(maxTorque);
        six().SetMotorState(A(i), hertz > 0 ? JPH::EMotorState::Position : JPH::EMotorState::Off);
    }
    return wakeBodies();
}
eve::Result<float> JointBackend::getParallelHertz() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Parallel);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetMotorSettings(A::RotationY).mSpringSettings.mFrequency);
}
eve::Result<float> JointBackend::getParallelDampingRatio() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Parallel);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetMotorSettings(A::RotationY).mSpringSettings.mDamping);
}
eve::Result<float> JointBackend::getParallelMaxTorque() const {
    using R = eve::Result<float>;
    if (!isValid()) return rejected<float>("Joint expired", eve::DiagnosticCode::StaleHandle);
    auto checked = requireKind(JointKind3D::Parallel);
    if (!checked) return R::failure(checked.status());
    return R::success(six().GetMotorSettings(A::RotationY).mMaxTorqueLimit);
}
}  // namespace eve::physics::jolt
