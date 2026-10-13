#include "physics/Joint3D.h"
#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"
namespace eve::physics {
namespace {
template <class T>
T legacyValue(eve::Result<T> result) {
    if (!result.ok()) throw eve::Exception("%s", result.error()->message().c_str());
    return std::move(result).value();
}
void legacyValue(eve::Result<void> result) {
    if (!result.ok()) throw eve::Exception("%s", result.error()->message().c_str());
}
}  // namespace
Joint3D::Joint3D(World3D* world, Body3D* bodyA, Body3D* bodyB, std::unique_ptr<IJointBackend3D> backend,
                 PhysicsJointHandle runtimeHandle, Kind kind, int id)
    : world_(world),
      bodyA_(bodyA),
      bodyB_(bodyB),
      backend_(std::move(backend)),
      runtimeHandle_(runtimeHandle),
      kind_(kind),
      id_(id) {
    EV_PARAM_CHECK(backend_ != nullptr, "Joint3D requires a backend");
    backend_->bindOwner(*this);
}
Joint3D::~Joint3D() { destroy(); }
bool Joint3D::isValid() const { return backend_ && backend_->isValid(); }
void Joint3D::invalidate() {
    if (world_) world_->forgetJoint(this);
    if (backend_) backend_->invalidate();
    world_ = nullptr;
    bodyA_ = nullptr;
    bodyB_ = nullptr;
    runtimeHandle_ = PhysicsJointHandle::invalid();
}
void Joint3D::destroy() {
    if (backend_) backend_->destroy();
    invalidate();
}
std::string Joint3D::getKind() const {
    switch (kind_) {
        case Kind::Distance: return "distance";
        case Kind::Revolute: return "revolute";
        case Kind::Prismatic: return "prismatic";
        case Kind::Spherical: return "spherical";
        case Kind::Wheel: return "wheel";
        case Kind::Weld: return "weld";
        case Kind::Motor: return "motor";
        case Kind::Parallel: return "parallel";
        case Kind::Filter: return "filter";
    }
    return "distance";
}

int Joint3D::getBodyAId() const { return bodyA_ ? bodyA_->getId() : -1; }
int Joint3D::getBodyBId() const { return bodyB_ ? bodyB_->getId() : -1; }

eve::Result<JointFrames3D> Joint3D::localFrames() const {
    if (!backend_)
        return eve::Result<JointFrames3D>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Joint has no backend", "physics.joint3d.localFrames"));
    return backend_->localFrames();
}
void Joint3D::setCollideConnected(bool collide) {
    if (!isValid()) return;
    legacyValue(backend_->setCollideConnected(collide));
}
bool Joint3D::getCollideConnected() const {
    if (!isValid()) return false;
    return legacyValue(backend_->getCollideConnected());
}
void Joint3D::setForceThreshold(float threshold) {
    if (!isValid()) return;
    legacyValue(backend_->setForceThreshold(threshold));
}
float Joint3D::getForceThreshold() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getForceThreshold());
}
void Joint3D::setTorqueThreshold(float threshold) {
    if (!isValid()) return;
    legacyValue(backend_->setTorqueThreshold(threshold));
}
float Joint3D::getTorqueThreshold() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getTorqueThreshold());
}

float Joint3D::getConstraintForceX() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getConstraintForceX());
}
float Joint3D::getConstraintForceY() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getConstraintForceY());
}
float Joint3D::getConstraintForceZ() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getConstraintForceZ());
}
float Joint3D::getConstraintTorqueX() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getConstraintTorqueX());
}
float Joint3D::getConstraintTorqueY() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getConstraintTorqueY());
}
float Joint3D::getConstraintTorqueZ() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getConstraintTorqueZ());
}
float Joint3D::getLinearSeparation() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getLinearSeparation());
}
float Joint3D::getAngularSeparation() const {
    if (!isValid()) return 0.f;
    return legacyValue(backend_->getAngularSeparation());
}
void  Joint3D::setDistanceLength(float length) { legacyValue(backend_->setDistanceLength(length)); }
float Joint3D::getDistanceLength() const { return legacyValue(backend_->getDistanceLength()); }
float Joint3D::getDistanceCurrentLength() const { return legacyValue(backend_->getDistanceCurrentLength()); }
void Joint3D::setDistanceSpring(bool enabled, float hertz, float dampingRatio) {
    legacyValue(backend_->setDistanceSpring(enabled, hertz, dampingRatio));
}
void Joint3D::setDistanceLimits(bool enabled, float minimum, float maximum) {
    legacyValue(backend_->setDistanceLimits(enabled, minimum, maximum));
}
void Joint3D::setDistanceMotor(bool enabled, float speed, float maxForce) {
    legacyValue(backend_->setDistanceMotor(enabled, speed, maxForce));
}
void Joint3D::setRevoluteSpring(bool enabled, float targetAngle, float hertz,
                                float dampingRatio) {
    legacyValue(backend_->setRevoluteSpring(enabled, targetAngle, hertz, dampingRatio));
}
void Joint3D::setRevoluteLimits(bool enabled, float lower, float upper) {
    legacyValue(backend_->setRevoluteLimits(enabled, lower, upper));
}
void Joint3D::setRevoluteMotor(bool enabled, float speed, float maxTorque) {
    legacyValue(backend_->setRevoluteMotor(enabled, speed, maxTorque));
}
float Joint3D::getRevoluteAngle() const { return legacyValue(backend_->getRevoluteAngle()); }
float Joint3D::getRevoluteMotorTorque() const { return legacyValue(backend_->getRevoluteMotorTorque()); }
void Joint3D::setPrismaticSpring(bool enabled, float targetTranslation, float hertz,
                                 float dampingRatio) {
    legacyValue(backend_->setPrismaticSpring(enabled, targetTranslation, hertz, dampingRatio));
}
void Joint3D::setPrismaticLimits(bool enabled, float lower, float upper) {
    legacyValue(backend_->setPrismaticLimits(enabled, lower, upper));
}
void Joint3D::setPrismaticMotor(bool enabled, float speed, float maxForce) {
    legacyValue(backend_->setPrismaticMotor(enabled, speed, maxForce));
}
float Joint3D::getPrismaticTranslation() const { return legacyValue(backend_->getPrismaticTranslation()); }
float Joint3D::getPrismaticSpeed() const { return legacyValue(backend_->getPrismaticSpeed()); }
float Joint3D::getPrismaticMotorForce() const { return legacyValue(backend_->getPrismaticMotorForce()); }
void Joint3D::setSphericalConeLimit(bool enabled, float angle) {
    legacyValue(backend_->setSphericalConeLimit(enabled, angle));
}
void Joint3D::setSphericalTwistLimits(bool enabled, float lower, float upper) {
    legacyValue(backend_->setSphericalTwistLimits(enabled, lower, upper));
}
void Joint3D::setSphericalMotor(bool enabled, float velocityX, float velocityY,
                                float velocityZ, float maxTorque) {
    legacyValue(backend_->setSphericalMotor(enabled, velocityX, velocityY, velocityZ, maxTorque));
}
float Joint3D::getSphericalConeAngle() const { return legacyValue(backend_->getSphericalConeAngle()); }
float Joint3D::getSphericalTwistAngle() const { return legacyValue(backend_->getSphericalTwistAngle()); }
void Joint3D::setWheelSuspension(bool enabled, float hertz, float dampingRatio) {
    legacyValue(backend_->setWheelSuspension(enabled, hertz, dampingRatio));
}
void Joint3D::setWheelSuspensionLimits(bool enabled, float lower, float upper) {
    legacyValue(backend_->setWheelSuspensionLimits(enabled, lower, upper));
}
void Joint3D::setWheelSpinMotor(bool enabled, float speed, float maxTorque) {
    legacyValue(backend_->setWheelSpinMotor(enabled, speed, maxTorque));
}
void Joint3D::setWheelSteering(bool enabled, float targetAngle, float hertz,
                               float dampingRatio, float maxTorque) {
    legacyValue(backend_->setWheelSteering(enabled, targetAngle, hertz, dampingRatio, maxTorque));
}
void Joint3D::setWheelSteeringLimits(bool enabled, float lower, float upper) {
    legacyValue(backend_->setWheelSteeringLimits(enabled, lower, upper));
}
float Joint3D::getWheelSpinSpeed() const { return legacyValue(backend_->getWheelSpinSpeed()); }
float Joint3D::getWheelSpinTorque() const { return legacyValue(backend_->getWheelSpinTorque()); }
float Joint3D::getWheelSteeringAngle() const { return legacyValue(backend_->getWheelSteeringAngle()); }
float Joint3D::getWheelSteeringTorque() const { return legacyValue(backend_->getWheelSteeringTorque()); }
void Joint3D::setWeldLinearSpring(float hertz, float dampingRatio) {
    legacyValue(backend_->setWeldLinearSpring(hertz, dampingRatio));
}
void Joint3D::setWeldAngularSpring(float hertz, float dampingRatio) {
    legacyValue(backend_->setWeldAngularSpring(hertz, dampingRatio));
}
float Joint3D::getWeldLinearHertz() const { return legacyValue(backend_->getWeldLinearHertz()); }
float Joint3D::getWeldLinearDampingRatio() const { return legacyValue(backend_->getWeldLinearDampingRatio()); }
float Joint3D::getWeldAngularHertz() const { return legacyValue(backend_->getWeldAngularHertz()); }
float Joint3D::getWeldAngularDampingRatio() const { return legacyValue(backend_->getWeldAngularDampingRatio()); }
void Joint3D::setMotorLinearVelocity(float vx, float vy, float vz) {
    legacyValue(backend_->setMotorLinearVelocity(vx, vy, vz));
}
void Joint3D::setMotorAngularVelocity(float wx, float wy, float wz) {
    legacyValue(backend_->setMotorAngularVelocity(wx, wy, wz));
}
void Joint3D::setMotorVelocityLimits(float maxForce, float maxTorque) {
    legacyValue(backend_->setMotorVelocityLimits(maxForce, maxTorque));
}
void Joint3D::setMotorLinearSpring(float hertz, float dampingRatio, float maxForce) {
    legacyValue(backend_->setMotorLinearSpring(hertz, dampingRatio, maxForce));
}
void Joint3D::setMotorAngularSpring(float hertz, float dampingRatio, float maxTorque) {
    legacyValue(backend_->setMotorAngularSpring(hertz, dampingRatio, maxTorque));
}
float Joint3D::getMotorLinearVelocityX() const { return legacyValue(backend_->getMotorLinearVelocityX()); }
float Joint3D::getMotorLinearVelocityY() const { return legacyValue(backend_->getMotorLinearVelocityY()); }
float Joint3D::getMotorLinearVelocityZ() const { return legacyValue(backend_->getMotorLinearVelocityZ()); }
float Joint3D::getMotorAngularVelocityX() const { return legacyValue(backend_->getMotorAngularVelocityX()); }
float Joint3D::getMotorAngularVelocityY() const { return legacyValue(backend_->getMotorAngularVelocityY()); }
float Joint3D::getMotorAngularVelocityZ() const { return legacyValue(backend_->getMotorAngularVelocityZ()); }
void Joint3D::setParallelSpring(float hertz, float dampingRatio, float maxTorque) {
    legacyValue(backend_->setParallelSpring(hertz, dampingRatio, maxTorque));
}
float Joint3D::getParallelHertz() const { return legacyValue(backend_->getParallelHertz()); }
float Joint3D::getParallelDampingRatio() const { return legacyValue(backend_->getParallelDampingRatio()); }
float Joint3D::getParallelMaxTorque() const { return legacyValue(backend_->getParallelMaxTorque()); }
}  // namespace eve::physics
