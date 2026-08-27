#include "physics/Joint3D.h"

#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "common/Exception.h"

#include <box3d/box3d.h>

#include <cmath>

namespace eve::physics {
namespace {

void nonNegative(float value, const char *operation, const char *name) {
    if (!std::isfinite(value) || value < 0.f)
        throw eve::Exception("%s: %s must be finite and >= 0", operation, name);
}

void finite(float value, const char *operation, const char *name) {
    if (!std::isfinite(value)) throw eve::Exception("%s: %s must be finite", operation, name);
}

}  // namespace

Joint3D::Joint3D(World3D *world, Body3D *bodyA, Body3D *bodyB, b3JointId jointId,
                 PhysicsJointHandle runtimeHandle, Kind kind, int id)
    : world_(world), bodyA_(bodyA), bodyB_(bodyB), jointId_(jointId),
      runtimeHandle_(runtimeHandle), kind_(kind), id_(id) {}

Joint3D::~Joint3D() { destroy(); }

bool Joint3D::isValid() const { return b3Joint_IsValid(jointId_); }

void Joint3D::invalidate() {
    if (world_) world_->forgetJoint(this);
    jointId_ = {};
    world_ = nullptr;
    bodyA_ = nullptr;
    bodyB_ = nullptr;
    runtimeHandle_ = PhysicsJointHandle::invalid();
}

void Joint3D::destroy() {
    if (isValid()) b3DestroyJoint(jointId_, true);
    if (world_) world_->forgetJoint(this);
    invalidate();
}

std::string Joint3D::getKind() const {
    switch (kind_) {
        case Kind::Distance: return "distance";
        case Kind::Revolute: return "revolute";
        case Kind::Prismatic: return "prismatic";
        case Kind::Spherical: return "spherical";
        case Kind::Wheel: return "wheel";
    }
    return "distance";
}

int Joint3D::getBodyAId() const { return bodyA_ ? bodyA_->getId() : -1; }
int Joint3D::getBodyBId() const { return bodyB_ ? bodyB_->getId() : -1; }

void Joint3D::setCollideConnected(bool collide) {
    if (isValid()) b3Joint_SetCollideConnected(jointId_, collide);
}

bool Joint3D::getCollideConnected() const {
    return isValid() ? b3Joint_GetCollideConnected(jointId_) : false;
}

void Joint3D::setForceThreshold(float threshold) {
    if (!isValid()) return;
    nonNegative(threshold, "Joint3D.setForceThreshold", "threshold");
    b3Joint_SetForceThreshold(jointId_, threshold);
}
float Joint3D::getForceThreshold() const {
    return isValid() ? b3Joint_GetForceThreshold(jointId_) : 0.f;
}
void Joint3D::setTorqueThreshold(float threshold) {
    if (!isValid()) return;
    nonNegative(threshold, "Joint3D.setTorqueThreshold", "threshold");
    b3Joint_SetTorqueThreshold(jointId_, threshold);
}
float Joint3D::getTorqueThreshold() const {
    return isValid() ? b3Joint_GetTorqueThreshold(jointId_) : 0.f;
}

#define EV_JOINT_VEC_GETTER(name, expression, member) \
    float Joint3D::name() const { return isValid() ? expression(jointId_).member : 0.f; }
EV_JOINT_VEC_GETTER(getConstraintForceX, b3Joint_GetConstraintForce, x)
EV_JOINT_VEC_GETTER(getConstraintForceY, b3Joint_GetConstraintForce, y)
EV_JOINT_VEC_GETTER(getConstraintForceZ, b3Joint_GetConstraintForce, z)
EV_JOINT_VEC_GETTER(getConstraintTorqueX, b3Joint_GetConstraintTorque, x)
EV_JOINT_VEC_GETTER(getConstraintTorqueY, b3Joint_GetConstraintTorque, y)
EV_JOINT_VEC_GETTER(getConstraintTorqueZ, b3Joint_GetConstraintTorque, z)
#undef EV_JOINT_VEC_GETTER

float Joint3D::getLinearSeparation() const {
    return isValid() ? b3Joint_GetLinearSeparation(jointId_) : 0.f;
}
float Joint3D::getAngularSeparation() const {
    return isValid() ? b3Joint_GetAngularSeparation(jointId_) : 0.f;
}

void Joint3D::requireKind(Kind expected, const char *operation) const {
    if (!isValid()) throw eve::Exception("%s: joint destroyed", operation);
    if (kind_ != expected) throw eve::Exception("%s: incompatible joint kind", operation);
}

void Joint3D::setDistanceLength(float length) {
    requireKind(Kind::Distance, "Joint3D.setDistanceLength");
    nonNegative(length, "Joint3D.setDistanceLength", "length");
    b3DistanceJoint_SetLength(jointId_, length);
}
float Joint3D::getDistanceLength() const {
    requireKind(Kind::Distance, "Joint3D.getDistanceLength");
    return b3DistanceJoint_GetLength(jointId_);
}
float Joint3D::getDistanceCurrentLength() const {
    requireKind(Kind::Distance, "Joint3D.getDistanceCurrentLength");
    return b3DistanceJoint_GetCurrentLength(jointId_);
}
void Joint3D::setDistanceSpring(bool enabled, float hertz, float dampingRatio) {
    requireKind(Kind::Distance, "Joint3D.setDistanceSpring");
    nonNegative(hertz, "Joint3D.setDistanceSpring", "hertz");
    nonNegative(dampingRatio, "Joint3D.setDistanceSpring", "dampingRatio");
    b3DistanceJoint_SetSpringHertz(jointId_, hertz);
    b3DistanceJoint_SetSpringDampingRatio(jointId_, dampingRatio);
    b3DistanceJoint_EnableSpring(jointId_, enabled);
}
void Joint3D::setDistanceLimits(bool enabled, float minimum, float maximum) {
    requireKind(Kind::Distance, "Joint3D.setDistanceLimits");
    nonNegative(minimum, "Joint3D.setDistanceLimits", "minimum");
    nonNegative(maximum, "Joint3D.setDistanceLimits", "maximum");
    if (minimum > maximum)
        throw eve::Exception("Joint3D.setDistanceLimits: minimum must be <= maximum");
    b3DistanceJoint_SetLengthRange(jointId_, minimum, maximum);
    b3DistanceJoint_EnableLimit(jointId_, enabled);
}
void Joint3D::setDistanceMotor(bool enabled, float speed, float maxForce) {
    requireKind(Kind::Distance, "Joint3D.setDistanceMotor");
    finite(speed, "Joint3D.setDistanceMotor", "speed");
    nonNegative(maxForce, "Joint3D.setDistanceMotor", "maxForce");
    b3DistanceJoint_SetMotorSpeed(jointId_, speed);
    b3DistanceJoint_SetMaxMotorForce(jointId_, maxForce);
    b3DistanceJoint_EnableMotor(jointId_, enabled);
}

void Joint3D::setRevoluteSpring(bool enabled, float targetAngle, float hertz,
                                float dampingRatio) {
    requireKind(Kind::Revolute, "Joint3D.setRevoluteSpring");
    finite(targetAngle, "Joint3D.setRevoluteSpring", "targetAngle");
    nonNegative(hertz, "Joint3D.setRevoluteSpring", "hertz");
    nonNegative(dampingRatio, "Joint3D.setRevoluteSpring", "dampingRatio");
    b3RevoluteJoint_SetTargetAngle(jointId_, targetAngle);
    b3RevoluteJoint_SetSpringHertz(jointId_, hertz);
    b3RevoluteJoint_SetSpringDampingRatio(jointId_, dampingRatio);
    b3RevoluteJoint_EnableSpring(jointId_, enabled);
}
void Joint3D::setRevoluteLimits(bool enabled, float lower, float upper) {
    requireKind(Kind::Revolute, "Joint3D.setRevoluteLimits");
    finite(lower, "Joint3D.setRevoluteLimits", "lower");
    finite(upper, "Joint3D.setRevoluteLimits", "upper");
    if (lower > upper)
        throw eve::Exception("Joint3D.setRevoluteLimits: lower must be <= upper");
    b3RevoluteJoint_SetLimits(jointId_, lower, upper);
    b3RevoluteJoint_EnableLimit(jointId_, enabled);
}
void Joint3D::setRevoluteMotor(bool enabled, float speed, float maxTorque) {
    requireKind(Kind::Revolute, "Joint3D.setRevoluteMotor");
    finite(speed, "Joint3D.setRevoluteMotor", "speed");
    nonNegative(maxTorque, "Joint3D.setRevoluteMotor", "maxTorque");
    b3RevoluteJoint_SetMotorSpeed(jointId_, speed);
    b3RevoluteJoint_SetMaxMotorTorque(jointId_, maxTorque);
    b3RevoluteJoint_EnableMotor(jointId_, enabled);
}
float Joint3D::getRevoluteAngle() const {
    requireKind(Kind::Revolute, "Joint3D.getRevoluteAngle");
    return b3RevoluteJoint_GetAngle(jointId_);
}
float Joint3D::getRevoluteMotorTorque() const {
    requireKind(Kind::Revolute, "Joint3D.getRevoluteMotorTorque");
    return b3RevoluteJoint_GetMotorTorque(jointId_);
}

void Joint3D::setPrismaticSpring(bool enabled, float targetTranslation, float hertz,
                                 float dampingRatio) {
    requireKind(Kind::Prismatic, "Joint3D.setPrismaticSpring");
    finite(targetTranslation, "Joint3D.setPrismaticSpring", "targetTranslation");
    nonNegative(hertz, "Joint3D.setPrismaticSpring", "hertz");
    nonNegative(dampingRatio, "Joint3D.setPrismaticSpring", "dampingRatio");
    b3PrismaticJoint_SetTargetTranslation(jointId_, targetTranslation);
    b3PrismaticJoint_SetSpringHertz(jointId_, hertz);
    b3PrismaticJoint_SetSpringDampingRatio(jointId_, dampingRatio);
    b3PrismaticJoint_EnableSpring(jointId_, enabled);
}
void Joint3D::setPrismaticLimits(bool enabled, float lower, float upper) {
    requireKind(Kind::Prismatic, "Joint3D.setPrismaticLimits");
    finite(lower, "Joint3D.setPrismaticLimits", "lower");
    finite(upper, "Joint3D.setPrismaticLimits", "upper");
    if (lower > upper)
        throw eve::Exception("Joint3D.setPrismaticLimits: lower must be <= upper");
    b3PrismaticJoint_SetLimits(jointId_, lower, upper);
    b3PrismaticJoint_EnableLimit(jointId_, enabled);
}
void Joint3D::setPrismaticMotor(bool enabled, float speed, float maxForce) {
    requireKind(Kind::Prismatic, "Joint3D.setPrismaticMotor");
    finite(speed, "Joint3D.setPrismaticMotor", "speed");
    nonNegative(maxForce, "Joint3D.setPrismaticMotor", "maxForce");
    b3PrismaticJoint_SetMotorSpeed(jointId_, speed);
    b3PrismaticJoint_SetMaxMotorForce(jointId_, maxForce);
    b3PrismaticJoint_EnableMotor(jointId_, enabled);
}
float Joint3D::getPrismaticTranslation() const {
    requireKind(Kind::Prismatic, "Joint3D.getPrismaticTranslation");
    return b3PrismaticJoint_GetTranslation(jointId_);
}
float Joint3D::getPrismaticSpeed() const {
    requireKind(Kind::Prismatic, "Joint3D.getPrismaticSpeed");
    return b3PrismaticJoint_GetSpeed(jointId_);
}
float Joint3D::getPrismaticMotorForce() const {
    requireKind(Kind::Prismatic, "Joint3D.getPrismaticMotorForce");
    return b3PrismaticJoint_GetMotorForce(jointId_);
}

void Joint3D::setSphericalConeLimit(bool enabled, float angle) {
    requireKind(Kind::Spherical, "Joint3D.setSphericalConeLimit");
    finite(angle, "Joint3D.setSphericalConeLimit", "angle");
    if (angle < 0.f || angle > B3_PI)
        throw eve::Exception("Joint3D.setSphericalConeLimit: angle must be in [0, pi]");
    b3SphericalJoint_SetConeLimit(jointId_, angle);
    b3SphericalJoint_EnableConeLimit(jointId_, enabled);
}
void Joint3D::setSphericalTwistLimits(bool enabled, float lower, float upper) {
    requireKind(Kind::Spherical, "Joint3D.setSphericalTwistLimits");
    finite(lower, "Joint3D.setSphericalTwistLimits", "lower");
    finite(upper, "Joint3D.setSphericalTwistLimits", "upper");
    if (lower > upper || lower < -0.99f * B3_PI || upper > 0.99f * B3_PI)
        throw eve::Exception(
            "Joint3D.setSphericalTwistLimits: require -0.99*pi <= lower <= upper <= 0.99*pi");
    b3SphericalJoint_SetTwistLimits(jointId_, lower, upper);
    b3SphericalJoint_EnableTwistLimit(jointId_, enabled);
}
void Joint3D::setSphericalMotor(bool enabled, float velocityX, float velocityY,
                                float velocityZ, float maxTorque) {
    requireKind(Kind::Spherical, "Joint3D.setSphericalMotor");
    finite(velocityX, "Joint3D.setSphericalMotor", "velocityX");
    finite(velocityY, "Joint3D.setSphericalMotor", "velocityY");
    finite(velocityZ, "Joint3D.setSphericalMotor", "velocityZ");
    nonNegative(maxTorque, "Joint3D.setSphericalMotor", "maxTorque");
    b3SphericalJoint_SetMotorVelocity(jointId_, {velocityX, velocityY, velocityZ});
    b3SphericalJoint_SetMaxMotorTorque(jointId_, maxTorque);
    b3SphericalJoint_EnableMotor(jointId_, enabled);
}
float Joint3D::getSphericalConeAngle() const {
    requireKind(Kind::Spherical, "Joint3D.getSphericalConeAngle");
    return b3SphericalJoint_GetConeAngle(jointId_);
}
float Joint3D::getSphericalTwistAngle() const {
    requireKind(Kind::Spherical, "Joint3D.getSphericalTwistAngle");
    return b3SphericalJoint_GetTwistAngle(jointId_);
}

void Joint3D::setWheelSuspension(bool enabled, float hertz, float dampingRatio) {
    requireKind(Kind::Wheel, "Joint3D.setWheelSuspension");
    nonNegative(hertz, "Joint3D.setWheelSuspension", "hertz");
    nonNegative(dampingRatio, "Joint3D.setWheelSuspension", "dampingRatio");
    b3WheelJoint_SetSuspensionHertz(jointId_, hertz);
    b3WheelJoint_SetSuspensionDampingRatio(jointId_, dampingRatio);
    b3WheelJoint_EnableSuspension(jointId_, enabled);
}
void Joint3D::setWheelSuspensionLimits(bool enabled, float lower, float upper) {
    requireKind(Kind::Wheel, "Joint3D.setWheelSuspensionLimits");
    finite(lower, "Joint3D.setWheelSuspensionLimits", "lower");
    finite(upper, "Joint3D.setWheelSuspensionLimits", "upper");
    if (lower > upper)
        throw eve::Exception("Joint3D.setWheelSuspensionLimits: lower must be <= upper");
    b3WheelJoint_SetSuspensionLimits(jointId_, lower, upper);
    b3WheelJoint_EnableSuspensionLimit(jointId_, enabled);
}
void Joint3D::setWheelSpinMotor(bool enabled, float speed, float maxTorque) {
    requireKind(Kind::Wheel, "Joint3D.setWheelSpinMotor");
    finite(speed, "Joint3D.setWheelSpinMotor", "speed");
    nonNegative(maxTorque, "Joint3D.setWheelSpinMotor", "maxTorque");
    b3WheelJoint_SetSpinMotorSpeed(jointId_, speed);
    b3WheelJoint_SetMaxSpinTorque(jointId_, maxTorque);
    b3WheelJoint_EnableSpinMotor(jointId_, enabled);
}
void Joint3D::setWheelSteering(bool enabled, float targetAngle, float hertz,
                               float dampingRatio, float maxTorque) {
    requireKind(Kind::Wheel, "Joint3D.setWheelSteering");
    finite(targetAngle, "Joint3D.setWheelSteering", "targetAngle");
    nonNegative(hertz, "Joint3D.setWheelSteering", "hertz");
    nonNegative(dampingRatio, "Joint3D.setWheelSteering", "dampingRatio");
    nonNegative(maxTorque, "Joint3D.setWheelSteering", "maxTorque");
    b3WheelJoint_SetTargetSteeringAngle(jointId_, targetAngle);
    b3WheelJoint_SetSteeringHertz(jointId_, hertz);
    b3WheelJoint_SetSteeringDampingRatio(jointId_, dampingRatio);
    b3WheelJoint_SetMaxSteeringTorque(jointId_, maxTorque);
    b3WheelJoint_EnableSteering(jointId_, enabled);
}
void Joint3D::setWheelSteeringLimits(bool enabled, float lower, float upper) {
    requireKind(Kind::Wheel, "Joint3D.setWheelSteeringLimits");
    finite(lower, "Joint3D.setWheelSteeringLimits", "lower");
    finite(upper, "Joint3D.setWheelSteeringLimits", "upper");
    if (lower > upper)
        throw eve::Exception("Joint3D.setWheelSteeringLimits: lower must be <= upper");
    b3WheelJoint_SetSteeringLimits(jointId_, lower, upper);
    b3WheelJoint_EnableSteeringLimit(jointId_, enabled);
}
float Joint3D::getWheelSpinSpeed() const {
    requireKind(Kind::Wheel, "Joint3D.getWheelSpinSpeed");
    return b3WheelJoint_GetSpinSpeed(jointId_);
}
float Joint3D::getWheelSpinTorque() const {
    requireKind(Kind::Wheel, "Joint3D.getWheelSpinTorque");
    return b3WheelJoint_GetSpinTorque(jointId_);
}
float Joint3D::getWheelSteeringAngle() const {
    requireKind(Kind::Wheel, "Joint3D.getWheelSteeringAngle");
    return b3WheelJoint_GetSteeringAngle(jointId_);
}
float Joint3D::getWheelSteeringTorque() const {
    requireKind(Kind::Wheel, "Joint3D.getWheelSteeringTorque");
    return b3WheelJoint_GetSteeringTorque(jointId_);
}

}  // namespace eve::physics
